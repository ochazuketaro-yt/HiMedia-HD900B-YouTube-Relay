// SPDX-License-Identifier: GPL-3.0-or-later
//
// hdrelay -- on-device YouTube relay for the HiMedia HD900B (Realtek RTD1186, Linux 2.6.34).
//
// Runs on the player itself.  The stock "YouTube" RSS screen talks plain HTTP to this daemon on
// 127.0.0.1:8810; the daemon talks TLS 1.3 to YouTube (libcurl + OpenSSL 3.5), turns the answer into
// things the stock firmware understands (RSS 2.0, HLS), and the stock DvdPlayer does the decoding.
//
//   /search?q=..  /home.rss  RSS 2.0 for the stock RSS UI       /v/<id>.m3u8  VOD playlist (video + audio muxed)
//   /v/<id>/<n>.ts           video TS + packed AAC -> one TS     /t/<id>.jpg   thumbnail (TLS -> plain HTTP)
//   /set?h=480&max=20        runtime tuning (loopback only)      /health       liveness
//
// YouTube side : WEB InnerTube search (MeeTube parsers) and VISIONOS /player -> HLS (video TS + packed-AAC
//                audio rendition).  Only allow-listed hosts are contacted; signed URLs are never logged.
// Build        : static MIPS32r2 o32, GCC 13 / uClibc-ng, linked with src/compat/hd900b_compat.c (see docs/).
// Caveat       : no timed pthread waits (see the time64 notes in src/compat/hd900b_compat.c).
#include <arpa/inet.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "parsers/playerparser.h"
#include "parsers/rendererparser.h"

using Bytes = std::vector<uint8_t>;
static std::string g_ca = "/tmp/hd900b/tls/cacert.pem";
static FILE *g_log = nullptr;
static std::mutex g_logmu;
static int g_maxmin = 20;
static int g_height = 480;           // preferred rendition height (closest avc1+mp4a variant)           // long videos (1000+ segment playlists) froze DvdPlayer's HLS parser in tests

static void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf(const char *fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logmu);
    FILE *f = g_log ? g_log : stderr;
    time_t t = time(nullptr); struct tm tm; gmtime_r(&t, &tm);
    fprintf(f, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fflush(f);
}

// ------------------------------------------------------------------------------------------------ small helpers
static bool validId(const std::string &s) {
    if (s.size() != 11) return false;
    for (char c : s) if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
}
static std::string urlDecode(const std::string &s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) { o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
        else if (s[i] == '+') o += ' ';
        else o += s[i];
    }
    return o;
}
static std::string xmlEsc(const std::string &s) {
    std::string o;
    for (char c : s) { if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if ((unsigned char)c >= 0x20 || c == '\n') o += c; }
    return o;
}
static std::string jsonEsc(const std::string &s) {
    std::string o;
    for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c; }
    return o;
}
static bool startsWith(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }
static std::string hostOf(const std::string &url) {
    size_t a = url.find("://"); if (a == std::string::npos) return "";
    size_t b = url.find_first_of("/?#", a + 3); std::string h = url.substr(a + 3, b == std::string::npos ? std::string::npos : b - a - 3);
    if (h.find('@') != std::string::npos) return "";
    size_t c = h.find(':'); if (c != std::string::npos) { if (h.substr(c) != ":443") return ""; h = h.substr(0, c); }
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char ch) { return tolower(ch); });
    return h;
}
static bool hostAllowed(const std::string &url, bool post) {
    if (!startsWith(url, "https://")) return false;
    std::string h = hostOf(url);
    if (h == "www.youtube.com") return true;
    if (post) return false;
    static const std::string suf = ".googlevideo.com";
    if (h.size() > suf.size() && h.compare(h.size() - suf.size(), suf.size(), suf) == 0) return true;
    return h == "i.ytimg.com";
}
static std::string urlJoin(const std::string &base, const std::string &ref) {
    if (startsWith(ref, "https://") || startsWith(ref, "http://")) return ref;
    size_t a = base.find("://"); size_t b = base.find('/', a + 3);
    std::string origin = base.substr(0, b == std::string::npos ? base.size() : b);
    if (!ref.empty() && ref[0] == '/') return origin + ref;
    std::string path = base.substr(0, base.find_first_of("?#")); size_t sl = path.rfind('/');
    return (sl == std::string::npos || sl < a + 3 ? origin + "/" : path.substr(0, sl + 1)) + ref;
}

// ------------------------------------------------------------------------------------------------ HTTPS client (libcurl)
struct Resp { long code = 0; std::string body; std::string err; bool truncated = false; };
struct WriteCtx { std::string *out; size_t cap; bool trunc; };
static size_t wcb(char *p, size_t s, size_t n, void *u) {
    WriteCtx *c = (WriteCtx *)u; size_t len = s * n;
    if (c->out->size() + len > c->cap) { c->trunc = true; return 0; }
    c->out->append(p, len); return len;
}
struct CurlTL { CURL *h = nullptr; ~CurlTL() { if (h) curl_easy_cleanup(h); } };
static thread_local CurlTL tl_curl;

static Resp httpFetch(const std::string &url0, const std::vector<std::string> &headers, const std::string *post, size_t cap, long timeoutSec = 40) {
    Resp r; std::string url = url0;
    for (int hop = 0; hop < 4; hop++) {
        if (!hostAllowed(url, post != nullptr)) { r.err = "blocked host"; return r; }
        if (!tl_curl.h) tl_curl.h = curl_easy_init();
        CURL *c = tl_curl.h; curl_easy_reset(c);
        r.body.clear(); WriteCtx wc{&r.body, cap, false}; struct curl_slist *hl = nullptr;
        for (auto &h : headers) hl = curl_slist_append(hl, h.c_str());
        curl_easy_setopt(c, CURLOPT_URL, url.c_str()); curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wcb); curl_easy_setopt(c, CURLOPT_WRITEDATA, &wc);
        curl_easy_setopt(c, CURLOPT_CAINFO, g_ca.c_str()); curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutSec); curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L); curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
        if (post) { curl_easy_setopt(c, CURLOPT_POST, 1L); curl_easy_setopt(c, CURLOPT_POSTFIELDS, post->c_str()); curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)post->size()); }
        CURLcode rc = curl_easy_perform(c);
        curl_slist_free_all(hl);
        r.truncated = wc.trunc;
        if (rc != CURLE_OK && !wc.trunc) { r.err = curl_easy_strerror(rc); return r; }
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.code);
        if (r.code >= 300 && r.code < 400 && !post) {
            char *loc = nullptr; curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &loc);
            if (!loc) return r;
            url = loc; continue;
        }
        return r;
    }
    r.err = "too many redirects"; return r;
}

// ------------------------------------------------------------------------------------------------ YouTube source
struct Seg { double dur; std::string uri; };
struct HlsInfo { std::string vb, ab; std::vector<Seg> vsegs, asegs; int height = 0; bool live = false; time_t t = 0; };
static const char *WEB_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";
static const char *VOS_UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15";
static const char *CTX_TAIL = ",\"user\":{\"lockedSafetyMode\":false},\"request\":{\"useSsl\":true,\"internalExperimentFlags\":[]}}";

struct SearchItem { std::string id, title, dur; };
class Yt {
public:
    std::vector<SearchItem> search(const std::string &q) {
        { std::lock_guard<std::mutex> lk(mu_); auto it = scache_.find(q); if (it != scache_.end() && time(nullptr) - it->second.first < 300) return it->second.second; }
        seed();
        std::string body = std::string("{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260626.01.00\",\"hl\":\"en\",\"gl\":\"US\"") +
                           (visitor_.empty() ? "" : ",\"visitorData\":\"" + jsonEsc(visitor_) + "\"") + "}" + CTX_TAIL + ",\"query\":\"" + jsonEsc(q) + "\"}";
        Resp r = httpFetch("https://www.youtube.com/youtubei/v1/search?prettyPrint=false", webHeaders(), &body, 6 << 20);
        if (r.code != 200) throw std::runtime_error("search HTTP " + std::to_string(r.code) + " " + r.err);
        QString tok; auto vs = yt::parseVideoList(r.body, &tok);
        std::vector<SearchItem> out;
        for (auto &v : vs) if (validId(v.id.s_)) out.push_back({v.id.s_, v.title.s_, v.duration.s_});
        std::lock_guard<std::mutex> lk(mu_); scache_[q] = {time(nullptr), out}; return out;
    }
    std::shared_ptr<HlsInfo> hls(const std::string &vid0, int wantH = 0) {
        if (wantH <= 0) wantH = g_height;
        const std::string vid = vid0; const std::string ckey = vid + "@" + std::to_string(wantH);
        std::lock_guard<std::mutex> klk(hmu_);                  // one /player at a time (also serialises duplicate warm-ups)
        { std::lock_guard<std::mutex> lk(mu_); auto it = hcache_.find(ckey); if (it != hcache_.end() && time(nullptr) - it->second->t < 1200) return it->second; }
        seed();
        std::string body = std::string("{\"context\":{\"client\":{\"clientName\":\"VISIONOS\",\"clientVersion\":\"1.02\",\"hl\":\"en\",\"gl\":\"US\",\"deviceMake\":\"Apple\",\"deviceModel\":\"RealityDevice17,1\",\"osName\":\"visionOS\",\"osVersion\":\"26.5.23O471\"") +
                           (visitor_.empty() ? "" : ",\"visitorData\":\"" + jsonEsc(visitor_) + "\"") + "}" + CTX_TAIL + ",\"videoId\":\"" + vid + "\",\"contentCheckOk\":true,\"racyCheckOk\":true}";
        std::vector<std::string> h = {"Content-Type: application/json", "X-YouTube-Client-Name: 101", "X-YouTube-Client-Version: 1.02", std::string("User-Agent: ") + VOS_UA, "Cookie: SOCS=CAISAiAD"};
        if (!visitor_.empty()) h.push_back("X-Goog-Visitor-Id: " + visitor_);
        Resp r = httpFetch("https://www.youtube.com/youtubei/v1/player?prettyPrint=false", h, &body, 6 << 20);
        if (r.code != 200) throw std::runtime_error("player HTTP " + std::to_string(r.code) + " " + r.err);
        auto pr = yt::parsePlayer(r.body);
        if (!pr.playable) throw std::runtime_error("not playable: " + pr.reason.s_.substr(0, 80));
        std::string hlsUrl; for (auto &s : pr.streams) if (s.id.s_ == "hls") { hlsUrl = s.url.s_; break; }
        if (hlsUrl.empty()) throw std::runtime_error("no HLS (live/other?)");
        std::vector<std::string> gh = {std::string("User-Agent: ") + VOS_UA};
        Resp m = httpFetch(hlsUrl, gh, nullptr, 2 << 20);
        if (m.code != 200) throw std::runtime_error("master HTTP " + std::to_string(m.code));
        // variants / audio renditions
        struct Var { int bw = 0, h = 0; std::string codecs, audio, uri; };
        std::vector<Var> vars; std::map<std::string, std::string> aud;   // group -> uri
        std::vector<std::string> lines = splitLines(m.body);
        for (size_t i = 0; i < lines.size(); i++) {
            if (startsWith(lines[i], "#EXT-X-STREAM-INF")) {
                Var v; v.bw = atoi(attr(lines[i], "BANDWIDTH").c_str()); std::string res = attr(lines[i], "RESOLUTION"); size_t x = res.find('x'); if (x != std::string::npos) v.h = atoi(res.c_str() + x + 1);
                v.codecs = attr(lines[i], "CODECS"); v.audio = attr(lines[i], "AUDIO");
                for (size_t j = i + 1; j < lines.size(); j++) if (!lines[j].empty() && lines[j][0] != '#') { v.uri = lines[j]; break; }
                vars.push_back(v);
            } else if (startsWith(lines[i], "#EXT-X-MEDIA") && attr(lines[i], "TYPE") == "AUDIO" && !attr(lines[i], "URI").empty()) aud[attr(lines[i], "GROUP-ID")] = attr(lines[i], "URI");
        }
        const Var *best = nullptr;
        for (auto &v : vars) {
            if (v.codecs.find("avc1") == std::string::npos || v.codecs.find("mp4a") == std::string::npos || v.h <= 0 || v.uri.empty() || !aud.count(v.audio)) continue;
            if (!best || abs(v.h - wantH) < abs(best->h - wantH) || (abs(v.h - wantH) == abs(best->h - wantH) && v.bw < best->bw)) best = &v;
        }
        if (!best) throw std::runtime_error("no avc1+mp4a variant");
        auto info = std::make_shared<HlsInfo>(); info->height = best->h; info->t = time(nullptr);
        info->vb = urlJoin(hlsUrl, best->uri); info->ab = urlJoin(hlsUrl, aud[best->audio]);
        Resp vp = httpFetch(info->vb, gh, nullptr, 4 << 20), ap = httpFetch(info->ab, gh, nullptr, 4 << 20);
        if (vp.code != 200 || ap.code != 200) throw std::runtime_error("media playlist HTTP " + std::to_string(vp.code) + "/" + std::to_string(ap.code));
        bool vend = false, aend = false, enc = false;
        info->vsegs = parseMedia(vp.body, vend, enc); info->asegs = parseMedia(ap.body, aend, enc);
        if (enc || info->vsegs.empty() || info->vsegs.size() != info->asegs.size()) throw std::runtime_error("unsupported playlist (encrypted/fMP4/mismatch)");
        info->live = !vend;
        std::lock_guard<std::mutex> lk(mu_); hcache_[ckey] = info; return info;
    }
    std::string seg(const HlsInfo &i, bool video, size_t n) {
        const auto &segs = video ? i.vsegs : i.asegs; if (n >= segs.size()) throw std::out_of_range("seg");
        std::vector<std::string> gh = {std::string("User-Agent: ") + VOS_UA};
        Resp r = httpFetch(urlJoin(video ? i.vb : i.ab, segs[n].uri), gh, nullptr, 6 << 20);
        if (r.code != 200) throw std::runtime_error("segment HTTP " + std::to_string(r.code));
        return std::move(r.body);
    }
    bool thumb(const std::string &id, std::string &out) {
        Resp r = httpFetch("https://i.ytimg.com/vi/" + id + "/mqdefault.jpg", {"User-Agent: Mozilla/5.0"}, nullptr, 512 << 10);
        if (r.code != 200) return false; out = std::move(r.body); return true;
    }
    void clear() { std::lock_guard<std::mutex> lk(mu_); scache_.clear(); hcache_.clear(); }
private:
    static std::vector<std::string> splitLines(const std::string &s) {
        std::vector<std::string> o; size_t p = 0;
        while (p < s.size()) { size_t e = s.find('\n', p); if (e == std::string::npos) e = s.size(); std::string l = s.substr(p, e - p); if (!l.empty() && l.back() == '\r') l.pop_back(); o.push_back(l); p = e + 1; }
        return o;
    }
    static std::string attr(const std::string &line, const char *key) {
        size_t c = line.find(':'); if (c == std::string::npos) return "";
        std::string k = std::string(key) + "="; size_t p = c + 1;
        while (p < line.size()) {
            size_t eq = line.find('=', p); if (eq == std::string::npos) break;
            std::string name = line.substr(p, eq - p); size_t vs = eq + 1, ve;
            if (vs < line.size() && line[vs] == '"') { ve = line.find('"', vs + 1); if (ve == std::string::npos) ve = line.size(); if (name == key) return line.substr(vs + 1, ve - vs - 1); p = ve + 2; }
            else { ve = line.find(',', vs); if (ve == std::string::npos) ve = line.size(); if (name == key) return line.substr(vs, ve - vs); p = ve + 1; }
        }
        return "";
    }
    static std::vector<Seg> parseMedia(const std::string &t, bool &endlist, bool &enc) {
        std::vector<Seg> o; double cur = -1;
        for (auto &l : splitLines(t)) {
            if (startsWith(l, "#EXTINF:")) cur = atof(l.c_str() + 8);
            else if (startsWith(l, "#EXT-X-ENDLIST")) endlist = true;
            else if (startsWith(l, "#EXT-X-KEY") || startsWith(l, "#EXT-X-MAP") || startsWith(l, "#EXT-X-BYTERANGE")) enc = true;
            else if (!l.empty() && l[0] != '#' && cur >= 0) { o.push_back({cur, l}); cur = -1; }
        }
        return o;
    }
    std::vector<std::string> webHeaders() {
        std::vector<std::string> h = {"Content-Type: application/json", "X-YouTube-Client-Name: 1", "X-YouTube-Client-Version: 2.20260626.01.00", std::string("User-Agent: ") + WEB_UA, "Cookie: SOCS=CAISAiAD"};
        if (!visitor_.empty()) h.push_back("X-Goog-Visitor-Id: " + visitor_);
        return h;
    }
    void seed() {
        std::lock_guard<std::mutex> lk(smu_);
        if (!visitor_.empty() && time(nullptr) - vtime_ < 3600) return;
        std::string body = std::string("{\"context\":{\"client\":{\"clientName\":\"WEB\",\"clientVersion\":\"2.20260626.01.00\",\"hl\":\"en\",\"gl\":\"US\"}") + CTX_TAIL + ",\"query\":\"music\"}";
        std::vector<std::string> h = {"Content-Type: application/json", "X-YouTube-Client-Name: 1", "X-YouTube-Client-Version: 2.20260626.01.00", std::string("User-Agent: ") + WEB_UA, "Cookie: SOCS=CAISAiAD"};
        Resp r = httpFetch("https://www.youtube.com/youtubei/v1/search?prettyPrint=false", h, &body, 6 << 20);
        if (r.code != 200) throw std::runtime_error("seed HTTP " + std::to_string(r.code) + " " + r.err);
        size_t a = r.body.find("\"responseContext\""); if (a == std::string::npos) a = 0;
        size_t b = r.body.find("\"visitorData\":\"", a); if (b == std::string::npos) throw std::runtime_error("no visitorData");
        b += 15; size_t e = r.body.find('"', b); visitor_ = r.body.substr(b, e - b); vtime_ = time(nullptr);
    }
    std::mutex mu_, hmu_, smu_; std::string visitor_; time_t vtime_ = 0;
    std::map<std::string, std::pair<time_t, std::vector<SearchItem>>> scache_;
    std::map<std::string, std::shared_ptr<HlsInfo>> hcache_;
};
static Yt g_yt;

// ------------------------------------------------------------------------------------------------ TS muxer (port of tsmux.py)
namespace ts {
static uint32_t crc32mpeg(const uint8_t *d, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) { crc ^= (uint32_t)d[i] << 24; for (int k = 0; k < 8; k++) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : (crc << 1); }
    return crc;
}
using Pkt = std::array<uint8_t, 188>;
static int pidOf(const uint8_t *p) { return ((p[1] & 0x1F) << 8) | p[2]; }
static const uint8_t *payloadOf(const uint8_t *p, size_t &n) {
    int afc = (p[3] >> 4) & 3; size_t pos = 4;
    if (afc & 2) pos += 1 + p[4];
    if (!(afc & 1) || pos >= 188) { n = 0; return p + 188; }
    n = 188 - pos; return p + pos;
}
static bool ptsAt(const uint8_t *q, uint64_t &v) {
    v = ((uint64_t)((q[0] >> 1) & 7) << 30) | ((uint64_t)q[1] << 22) | ((uint64_t)(q[2] >> 1) << 15) | ((uint64_t)q[3] << 7) | (q[4] >> 1); return true;
}
static void encPts(uint8_t *o, uint64_t v, int marker) {
    o[0] = (uint8_t)((marker << 4) | (((v >> 30) & 7) << 1) | 1); o[1] = (v >> 22) & 0xFF; o[2] = (uint8_t)((((v >> 15) & 0x7F) << 1) | 1); o[3] = (v >> 7) & 0xFF; o[4] = (uint8_t)(((v & 0x7F) << 1) | 1);
}
static Pkt psiPacket(int pid, const Bytes &section) {
    Pkt p; p.fill(0xFF); p[0] = 0x47; p[1] = 0x40 | (pid >> 8); p[2] = pid & 0xFF; p[3] = 0x10; p[4] = 0;
    memcpy(&p[5], section.data(), section.size()); return p;
}
static Bytes withCrc(Bytes s) { uint32_t c = crc32mpeg(s.data(), s.size()); for (int i = 3; i >= 0; i--) s.push_back((c >> (8 * i)) & 0xFF); return s; }
static Bytes buildPat(int prog, int pmtPid) {
    Bytes body = {0, 1, 0xC1, 0, 0, (uint8_t)(prog >> 8), (uint8_t)prog, (uint8_t)(0xE0 | (pmtPid >> 8)), (uint8_t)pmtPid};
    int len = (int)body.size() + 4; Bytes s = {0x00, (uint8_t)(0xB0 | ((len >> 8) & 0xF)), (uint8_t)len}; s.insert(s.end(), body.begin(), body.end()); return withCrc(s);
}
static Bytes buildPmt(int prog, int pcrPid, const std::vector<std::pair<int, int>> &streams) {
    Bytes b = {(uint8_t)(prog >> 8), (uint8_t)prog, 0xC1, 0, 0, (uint8_t)(0xE0 | (pcrPid >> 8)), (uint8_t)pcrPid, 0xF0, 0x00};
    for (auto &st : streams) { b.push_back(st.first); b.push_back(0xE0 | (st.second >> 8)); b.push_back(st.second & 0xFF); b.push_back(0xF0); b.push_back(0x00); }
    int len = (int)b.size() + 4; Bytes s = {0x02, (uint8_t)(0xB0 | ((len >> 8) & 0xF)), (uint8_t)len}; s.insert(s.end(), b.begin(), b.end()); return withCrc(s);
}
static void pesToTs(int pid, const Bytes &pes, int &cc, std::vector<Pkt> &out) {
    size_t pos = 0; bool first = true;
    while (pos < pes.size()) {
        size_t chunk = std::min<size_t>(184, pes.size() - pos); Pkt p; p.fill(0xFF);
        p[0] = 0x47; p[1] = (first ? 0x40 : 0) | (pid >> 8); p[2] = pid & 0xFF;
        if (chunk < 184) { size_t stuff = 184 - chunk; p[3] = 0x30 | (cc & 15); p[4] = (uint8_t)(stuff - 1); if (stuff >= 2) { p[5] = 0x00; /* flags, rest 0xFF */ } memcpy(&p[4 + stuff], &pes[pos], chunk); }
        else { p[3] = 0x10 | (cc & 15); memcpy(&p[4], &pes[pos], chunk); }
        out.push_back(p); cc++; pos += chunk; first = false;
    }
}
static const int SR[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
// returns muxed TS or empty on failure
static bool mux(const std::string &video, const std::string &audio, std::string &out, std::string &err, int audioPid = 257) {
    const uint8_t *v = (const uint8_t *)video.data(); size_t nv = video.size() / 188;
    int prog = -1, pmtPid = -1, pcrPid = -1, vpid = -1;
    for (size_t i = 0; i < nv; i++) {                                  // PAT
        const uint8_t *p = v + i * 188; if (p[0] != 0x47) { err = "lost sync"; return false; }
        if (pidOf(p) == 0) { size_t n; const uint8_t *pl = payloadOf(p, n); const uint8_t *sec = pl + 1 + pl[0]; prog = (sec[8] << 8) | sec[9]; pmtPid = ((sec[10] & 0x1F) << 8) | sec[11]; break; }
    }
    if (pmtPid < 0) { err = "no PAT"; return false; }
    for (size_t i = 0; i < nv; i++) {                                  // PMT
        const uint8_t *p = v + i * 188;
        if (pidOf(p) == pmtPid) {
            size_t n; const uint8_t *pl = payloadOf(p, n); const uint8_t *sec = pl + 1 + pl[0];
            pcrPid = ((sec[8] & 0x1F) << 8) | sec[9]; int pil = ((sec[10] & 0xF) << 8) | sec[11]; size_t k = 12 + pil, end = 3 + (((sec[1] & 0xF) << 8) | sec[2]) - 4;
            while (k + 5 <= end) { int st = sec[k], ep = ((sec[k + 1] & 0x1F) << 8) | sec[k + 2], il = ((sec[k + 3] & 0xF) << 8) | sec[k + 4]; if (st == 0x1B) vpid = ep; k += 5 + il; }
            break;
        }
    }
    if (vpid < 0) { err = "no H.264 stream"; return false; }
    std::vector<std::pair<uint64_t, Pkt>> items; uint64_t key = 0;
    for (size_t i = 0; i < nv; i++) {
        const uint8_t *p = v + i * 188; if (pidOf(p) != vpid) continue;
        if (p[1] & 0x40) {
            size_t n; const uint8_t *pl = payloadOf(p, n);
            if (n >= 14 && pl[0] == 0 && pl[1] == 0 && pl[2] == 1) {
                uint64_t t = 0, d = 0;
                if (pl[7] & 0x80) ptsAt(pl + 9, t);
                if ((pl[7] & 0xC0) == 0xC0 && n >= 19) { ptsAt(pl + 14, d); key = d; } else if (pl[7] & 0x80) key = t;
            }
        }
        Pkt pk; memcpy(pk.data(), p, 188); items.push_back({key, pk});
    }
    // audio: ID3 timestamp + ADTS frames
    const uint8_t *a = (const uint8_t *)audio.data(); size_t na = audio.size(), off = 0; uint64_t base = items.empty() ? 0 : items[0].first; bool haveBase = false;
    if (na > 10 && !memcmp(a, "ID3", 3)) {
        size_t sz = ((a[6] & 0x7F) << 21) | ((a[7] & 0x7F) << 14) | ((a[8] & 0x7F) << 7) | (a[9] & 0x7F); off = 10 + sz;
        size_t i = 10;
        while (i + 10 <= std::min(off, na)) {
            uint32_t fsz = ((uint32_t)a[i + 4] << 24) | (a[i + 5] << 16) | (a[i + 6] << 8) | a[i + 7];
            if (!memcmp(a + i, "PRIV", 4) && i + 10 + fsz <= na) {
                const uint8_t *body = a + i + 10; size_t z = 0; while (z < fsz && body[z]) z++;
                std::string owner((const char *)body, z); for (auto &ch : owner) ch = tolower(ch);
                if (owner.size() >= 24 && owner.compare(owner.size() - 24, 24, "transportstreamtimestamp") == 0 && fsz >= z + 9) {
                    uint64_t t = 0; for (int k = 0; k < 8; k++) t = (t << 8) | body[z + 1 + k]; base = t & ((1ULL << 33) - 1); haveBase = true;
                }
            }
            if (fsz == 0) break; i += 10 + fsz;
        }
    }
    (void)haveBase;
    struct Fr { size_t o, l; int sr; }; std::vector<Fr> frames;
    for (size_t i = off; i + 7 <= na;) {
        if (a[i] != 0xFF || (a[i + 1] & 0xF0) != 0xF0) { i++; continue; }
        size_t fl = ((a[i + 3] & 3) << 11) | (a[i + 4] << 3) | (a[i + 5] >> 5); int sri = (a[i + 2] >> 2) & 15;
        if (fl < 7 || i + fl > na || sri > 12) break;
        frames.push_back({i, fl, SR[sri]}); i += fl;
    }
    std::vector<std::pair<uint64_t, Pkt>> aitems; int acc = 0;
    for (size_t g = 0; g < frames.size();) {
        size_t e = g, total = 0; while (e < frames.size() && e - g < 6 && total + frames[e].l < 60000) { total += frames[e].l; e++; }
        if (e == g) e = g + 1;
        uint64_t pts = (base + (uint64_t)g * 1024ULL * 90000ULL / frames[g].sr) & ((1ULL << 33) - 1);
        Bytes pes = {0, 0, 1, 0xC0, (uint8_t)((3 + 5 + total) >> 8), (uint8_t)(3 + 5 + total), 0x80, 0x80, 5, 0, 0, 0, 0, 0};
        encPts(&pes[9], pts, 2);
        for (size_t k = g; k < e; k++) pes.insert(pes.end(), a + frames[k].o, a + frames[k].o + frames[k].l);
        std::vector<Pkt> pk; pesToTs(audioPid, pes, acc, pk); for (auto &p : pk) aitems.push_back({pts, p});
        g = e;
    }
    items.insert(items.end(), aitems.begin(), aitems.end());
    std::stable_sort(items.begin(), items.end(), [](const std::pair<uint64_t, Pkt> &x, const std::pair<uint64_t, Pkt> &y) { return x.first < y.first; });
    Bytes pat = buildPat(prog, pmtPid), pmt = buildPmt(prog, pcrPid, {{0x1B, vpid}, {0x0F, audioPid}});
    out.clear(); out.reserve((items.size() + 2) * 188);
    Pkt p0 = psiPacket(0, pat), p1 = psiPacket(pmtPid, pmt);
    out.append((const char *)p0.data(), 188); out.append((const char *)p1.data(), 188);
    for (auto &it : items) out.append((const char *)it.second.data(), 188);
    return true;
}
}  // namespace ts

// ------------------------------------------------------------------------------------------------ segment cache
static std::mutex g_segmu; static std::list<std::pair<std::string, std::shared_ptr<std::string>>> g_seglru; static size_t g_segbytes = 0;
static const size_t SEG_CACHE_BYTES = 10u << 20;
static std::mutex g_buildmu;                         // one mux at a time (RAM/CPU on the 24Kc)
static std::shared_ptr<std::string> getSeg(const std::string &vid, size_t n) {
    std::string key = vid + "@" + std::to_string(g_height) + "/" + std::to_string(n);
    { std::lock_guard<std::mutex> lk(g_segmu); for (auto it = g_seglru.begin(); it != g_seglru.end(); ++it) if (it->first == key) { auto v = it->second; g_seglru.splice(g_seglru.begin(), g_seglru, it); return v; } }
    std::lock_guard<std::mutex> bl(g_buildmu);
    { std::lock_guard<std::mutex> lk(g_segmu); for (auto it = g_seglru.begin(); it != g_seglru.end(); ++it) if (it->first == key) return it->second; }
    auto info = g_yt.hls(vid);
    std::string v = g_yt.seg(*info, true, n), a = g_yt.seg(*info, false, n), out, err;
    if (!ts::mux(v, a, out, err)) throw std::runtime_error("mux: " + err);
    auto sp = std::make_shared<std::string>(std::move(out));
    std::lock_guard<std::mutex> lk(g_segmu);
    g_seglru.push_front({key, sp}); g_segbytes += sp->size();
    while (g_segbytes > SEG_CACHE_BYTES && g_seglru.size() > 1) { g_segbytes -= g_seglru.back().second->size(); g_seglru.pop_back(); }
    return sp;
}
static long durSec(const std::string &d) {
    int p[3] = {0, 0, 0}, n = 0; const char *s = d.c_str();
    while (*s && n < 3) { if (isdigit((unsigned char)*s)) { p[n++] = atoi(s); while (isdigit((unsigned char)*s)) s++; } else s++; }
    if (n == 2) return p[0] * 60 + p[1]; if (n == 3) return p[0] * 3600 + p[1] * 60 + p[2]; return 1L << 30;
}
static void warm(std::vector<std::string> ids) {
    std::thread([ids] {
        for (auto &id : ids) { try { auto i = g_yt.hls(id); (void)i; getSeg(id, 0); } catch (std::exception &e) { logf("WARM %s %s", id.c_str(), e.what()); } }
    }).detach();
}

// ------------------------------------------------------------------------------------------------ HTTP server
struct Req { std::string method, path, query, host, range; bool keep = true; };
static bool readReq(int fd, Req &r) {
    std::string buf; char tmp[2048];
    for (;;) {
        size_t p = buf.find("\r\n\r\n"); if (p != std::string::npos) { buf.resize(p); break; }
        if (buf.size() > 16384) return false;
        struct pollfd pf{fd, POLLIN, 0}; int pr = poll(&pf, 1, 30000); if (pr <= 0) return false;
        ssize_t n = recv(fd, tmp, sizeof tmp, 0); if (n <= 0) return false; buf.append(tmp, n);
    }
    size_t e = buf.find("\r\n"); std::string line = buf.substr(0, e);
    size_t s1 = line.find(' '), s2 = line.rfind(' '); if (s1 == std::string::npos || s2 <= s1) return false;
    r.method = line.substr(0, s1); std::string target = line.substr(s1 + 1, s2 - s1 - 1);
    size_t q = target.find('?'); r.path = target.substr(0, q); r.query = q == std::string::npos ? "" : target.substr(q + 1);
    r.keep = line.find("HTTP/1.0") == std::string::npos;
    size_t pos = e + 2;
    while (pos < buf.size()) {
        size_t le = buf.find("\r\n", pos); if (le == std::string::npos) le = buf.size();
        std::string h = buf.substr(pos, le - pos); pos = le + 2; size_t c = h.find(':'); if (c == std::string::npos) continue;
        std::string k = h.substr(0, c), v = h.substr(c + 1); while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        for (auto &ch : k) ch = tolower(ch);
        if (k == "host") r.host = v; else if (k == "range") r.range = v; else if (k == "connection") { std::string lv = v; for (auto &ch : lv) ch = tolower(ch); if (lv == "close") r.keep = false; else if (lv.find("keep-alive") != std::string::npos) r.keep = true; }
    }
    return true;
}
static bool sendAll(int fd, const char *d, size_t n) {
    while (n) { struct pollfd pf{fd, POLLOUT, 0}; if (poll(&pf, 1, 30000) <= 0) return false; ssize_t w = send(fd, d, n, MSG_NOSIGNAL); if (w <= 0) return false; d += w; n -= w; }
    return true;
}
static bool respond(int fd, const Req &rq, int code, const char *ctype, const std::string &body) {
    size_t a = 0, b = body.empty() ? 0 : body.size() - 1; const char *st = code == 200 ? "OK" : code == 404 ? "Not Found" : code == 416 ? "Range Not Satisfiable" : code == 502 ? "Bad Gateway" : "Error";
    std::string extra;
    if (code == 200 && !rq.range.empty() && startsWith(rq.range, "bytes=") && !body.empty()) {
        const char *s = rq.range.c_str() + 6; char *end; unsigned long x = strtoul(s, &end, 10); bool hasA = end != s; const char *t = (*end == '-') ? end + 1 : end; unsigned long y = strtoul(t, &end, 10); bool hasB = end != t;
        if (hasA) { a = x; b = hasB ? std::min<size_t>(y, body.size() - 1) : body.size() - 1; } else if (hasB) { a = y >= body.size() ? 0 : body.size() - y; b = body.size() - 1; }
        if (a > b || a >= body.size()) { std::string h = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + std::to_string(body.size()) + "\r\nContent-Length: 0\r\n\r\n"; return sendAll(fd, h.data(), h.size()); }
        code = 206; st = "Partial Content"; extra = "Content-Range: bytes " + std::to_string(a) + "-" + std::to_string(b) + "/" + std::to_string(body.size()) + "\r\n";
    }
    size_t len = body.empty() ? 0 : b - a + 1;
    std::string h = "HTTP/1.1 " + std::to_string(code) + " " + st + "\r\nContent-Type: " + ctype + "\r\nContent-Length: " + std::to_string(len) + "\r\nAccept-Ranges: bytes\r\n" + extra + (rq.keep ? "Connection: keep-alive\r\n" : "Connection: close\r\n") + "\r\n";
    if (!sendAll(fd, h.data(), h.size())) return false;
    if (rq.method == "HEAD" || len == 0) return true;
    return sendAll(fd, body.data() + a, len);
}
static std::string rssOf(const std::vector<SearchItem> &its, const std::string &host, const std::string &title) {
    std::string o = "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<rss version=\"2.0\" xmlns:media=\"http://search.yahoo.com/mrss/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n<channel>\n<title>" + xmlEsc(title) + "</title>\n";
    for (auto &i : its)
        o += "<item><title>" + xmlEsc(i.title) + "</title><description>Duration " + xmlEsc(i.dur.empty() ? "?" : i.dur) + "</description><pubDate>Sun, 11 Oct 2026 00:00:00 GMT</pubDate>"
             "<media:thumbnail url=\"http://" + host + "/t/" + i.id + ".jpg\" width=\"320\" height=\"180\" />"
             "<enclosure url=\"http://" + host + "/v/" + i.id + ".m3u8\" type=\"application/vnd.apple.mpegurl\" length=\"0\" /></item>\n";
    return o + "</channel>\n</rss>\n";
}
static void handle(int fd) {
    sockaddr_in peer{}; socklen_t pl = sizeof peer; bool loopback = getpeername(fd, (sockaddr *)&peer, &pl) == 0 && (ntohl(peer.sin_addr.s_addr) >> 24) == 127;
    for (int served = 0; served < 200; served++) {
        Req rq; if (!readReq(fd, rq)) break;
        time_t t0 = time(nullptr); int code = 200; bool ok = true;
        try {
            if (rq.method != "GET" && rq.method != "HEAD") { code = 405; ok = respond(fd, rq, 404, "text/plain", "method\n"); }
            else if (rq.path == "/search" || rq.path == "/home.rss") {
                std::string q = "popular music";
                if (rq.path == "/search") { q = "nasa"; size_t p = rq.query.find("q="); if (p != std::string::npos) { size_t e = rq.query.find('&', p); q = urlDecode(rq.query.substr(p + 2, e == std::string::npos ? std::string::npos : e - p - 2)); } if (q.size() > 80) q.resize(80); }
                auto its = g_yt.search(q); std::vector<SearchItem> f; for (auto &i : its) if (durSec(i.dur) <= g_maxmin * 60L && f.size() < 25) f.push_back(i);
                ok = respond(fd, rq, 200, "application/rss+xml; charset=utf-8", rssOf(f, rq.host.empty() ? "127.0.0.1:8810" : rq.host, "HD900B YouTube: " + q));
                if (rq.method == "GET") { std::vector<std::string> ids; for (size_t i = 0; i < f.size() && i < 3; i++) ids.push_back(f[i].id); warm(ids); }
            } else if (rq.path.size() == 3 + 11 + 5 && startsWith(rq.path, "/v/") && rq.path.compare(14, 5, ".m3u8") == 0 && validId(rq.path.substr(3, 11))) {
                std::string vid = rq.path.substr(3, 11); auto info = g_yt.hls(vid);
                if (info->live) throw std::runtime_error("live streams are not supported");
                double mx = 0; for (auto &s : info->vsegs) mx = std::max(mx, s.dur);
                std::string pl = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:" + std::to_string((int)(mx + 0.999)) + "\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n";
                char b[64]; for (size_t i = 0; i < info->vsegs.size(); i++) { snprintf(b, sizeof b, "#EXTINF:%.3f,\n", info->vsegs[i].dur); pl += b; pl += vid + "/" + std::to_string(i) + ".ts\n"; }
                pl += "#EXT-X-ENDLIST\n"; ok = respond(fd, rq, 200, "application/vnd.apple.mpegurl", pl);
            } else if (startsWith(rq.path, "/v/") && rq.path.size() > 17 && rq.path[14] == '/' && validId(rq.path.substr(3, 11)) && rq.path.compare(rq.path.size() - 3, 3, ".ts") == 0) {
                std::string vid = rq.path.substr(3, 11); std::string num = rq.path.substr(15, rq.path.size() - 18);
                if (num.empty() || num.size() > 5 || num.find_first_not_of("0123456789") != std::string::npos) { code = 404; ok = respond(fd, rq, 404, "text/plain", "not found\n"); }
                else { size_t n = (size_t)atoi(num.c_str()); auto sp = getSeg(vid, n); ok = respond(fd, rq, 200, "video/mp2t", *sp);
                       if (rq.method == "GET") { auto info = g_yt.hls(vid); if (n + 1 < info->vsegs.size()) std::thread([vid, n] { try { getSeg(vid, n + 1); } catch (...) {} }).detach(); } }
            } else if (startsWith(rq.path, "/t/") && rq.path.size() == 3 + 11 + 4 && validId(rq.path.substr(3, 11))) {
                std::string img; if (g_yt.thumb(rq.path.substr(3, 11), img)) ok = respond(fd, rq, 200, "image/jpeg", img); else { code = 404; ok = respond(fd, rq, 404, "text/plain", "not found\n"); }
            } else if (rq.path == "/set" && loopback) {            // runtime tuning from the device itself: /set?h=480&max=30
                auto num = [&](const char *k) { size_t p = rq.query.find(std::string(k) + "="); return p == std::string::npos ? -1 : atoi(rq.query.c_str() + p + strlen(k) + 1); };
                int h = num("h"), m = num("max"); if (h >= 144 && h <= 1080) g_height = h; if (m >= 1 && m <= 1000) g_maxmin = m; g_yt.clear();
                ok = respond(fd, rq, 200, "text/plain", "height=" + std::to_string(g_height) + " maxmin=" + std::to_string(g_maxmin) + "\n");
            } else if (rq.path == "/health") ok = respond(fd, rq, 200, "text/plain", "ok\n");
            else { code = 404; ok = respond(fd, rq, 404, "text/plain", "not found\n"); }
        } catch (std::out_of_range &) { code = 404; ok = respond(fd, rq, 404, "text/plain", "not found\n"); }
        catch (std::exception &e) { code = 502; std::string m = std::string(e.what()).substr(0, 200); logf("ERR %s %s", rq.path.c_str(), m.c_str()); ok = respond(fd, rq, 502, "text/plain", m + "\n"); }
        logf("%d %s%s%s %lds", code, rq.path.c_str(), rq.path == "/search" ? "?" : "", rq.path == "/search" ? rq.query.c_str() : "", (long)(time(nullptr) - t0));
        if (!ok || !rq.keep) break;
    }
    close(fd);
}

int main(int argc, char **argv) {
    int port = 8810; std::string bind_ = "127.0.0.1", logfile;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-p" && i + 1 < argc) port = atoi(argv[++i]); else if (a == "-b" && i + 1 < argc) bind_ = argv[++i];
        else if (a == "-c" && i + 1 < argc) g_ca = argv[++i]; else if (a == "-l" && i + 1 < argc) logfile = argv[++i];
        else if (a == "-m" && i + 1 < argc) g_maxmin = atoi(argv[++i]); else if (a == "-H" && i + 1 < argc) g_height = atoi(argv[++i]);
        else { fprintf(stderr, "usage: hdrelay [-p port] [-b addr] [-c cacert.pem] [-l logfile] [-m maxminutes]\n"); return 2; }
    }
    signal(SIGPIPE, SIG_IGN);
    if (!logfile.empty()) g_log = fopen(logfile.c_str(), "a");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    int ls = socket(AF_INET, SOCK_STREAM, 0); int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_port = htons(port); inet_pton(AF_INET, bind_.c_str(), &sa.sin_addr);
    if (bind(ls, (sockaddr *)&sa, sizeof sa) != 0 || listen(ls, 16) != 0) { perror("bind/listen"); return 1; }
    logf("--- hdrelay start %s:%d maxmin=%d height=%d ---", bind_.c_str(), port, g_maxmin, g_height);
    static std::mutex cm; static int active = 0;
    for (;;) {
        int fd = accept(ls, nullptr, nullptr); if (fd < 0) continue;
        { std::lock_guard<std::mutex> lk(cm); if (active >= 12) { close(fd); continue; } active++; }
        std::thread([fd] { handle(fd); std::lock_guard<std::mutex> lk(cm); active--; }).detach();
    }
}
