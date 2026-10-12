#include "playerparser.h"
#include "ytjson.h"
#include <QString>
namespace yt {

using namespace gj;

// The /player response is a fixed root-level schema — no tree walking needed,
// one typed partial read per entry point (unknown keys skipped).
namespace pj {

struct PlayabilityStatus {
    std::optional<std::string> status;
    std::optional<std::string> reason;
};
struct Format {
    std::optional<std::string> url;
    std::optional<std::string> qualityLabel;
    std::optional<std::string> mimeType;
    std::optional<std::string> audioQuality;
    std::optional<FlexInt> itag;
    std::optional<FlexInt> width;
    std::optional<FlexInt> height;
    std::optional<FlexInt> bitrate;
};
struct StreamingData {
    std::optional<std::string> hlsManifestUrl;
    std::optional<std::string> serverAbrStreamingUrl;   // SABR offered; may coexist with fetchable urls
    std::optional<std::vector<Format>> formats;
    std::optional<std::vector<Format>> adaptiveFormats;
};
struct VideoDetails {
    std::optional<std::string> videoId;
    std::optional<std::string> title;
    std::optional<std::string> author;
    std::optional<std::string> channelId;
    std::optional<std::string> shortDescription;
    std::optional<FlexInt> viewCount;                   // arrives as a quoted string
    std::optional<ThumbSet> thumbnail;
};
struct CaptionTrack {
    std::optional<std::string> baseUrl;
    std::optional<std::string> languageCode;
    std::optional<Text> name;
};
struct TracklistR {
    std::optional<std::vector<CaptionTrack>> captionTracks;
};
struct Captions {
    std::optional<TracklistR> playerCaptionsTracklistRenderer;
};
struct PlayerRoot {
    std::optional<PlayabilityStatus> playabilityStatus;
    std::optional<StreamingData> streamingData;
    std::optional<VideoDetails> videoDetails;
    std::optional<Captions> captions;
};

} // namespace pj

static QString qstr(const std::string &s) { return QString::fromUtf8(s.data(), (int)s.size()); }
static QString qstr(const std::optional<std::string> &s) { return s ? qstr(*s) : QString(); }

// ---------------------------------------------------------------------------
// Static helpers — each takes an already-parsed root, contains verbatim logic
// ---------------------------------------------------------------------------

static bool playableOf(const pj::PlayerRoot &root, QString *reason)
{
    if (root.playabilityStatus) {
        const QString st = qstr(root.playabilityStatus->status);
        if (!st.isEmpty() && st != "OK") {
            if (reason) *reason = st + ": " + qstr(root.playabilityStatus->reason);
            return false;
        }
    }
    return true;
}

// Build one CT::Stream from a parsed format (muxed or adaptive). `muxed` = came
// from streamingData.formats (video+audio in one file). Returns false if the
// format has no fetchable url (ciphered / SABR-withheld).
static bool streamOf(const pj::Format &f, bool muxed, CT::Stream &s)
{
    const QString url = qstr(f.url);
    if (url.isEmpty()) return false;
    const QString mime = qstr(f.mimeType);
    s.id = QString::number(toInt64(f.itag));
    s.url = url;
    s.mimeType = mime;
    s.width = (int)toInt64(f.width);
    s.height = (int)toInt64(f.height);
    s.bitrate = (int)toInt64(f.bitrate);
    s.hasAudio = muxed || mime.startsWith(QLatin1String("audio/"));
    // Human label: "360p" for video, else the audio quality tier / mime subtype.
    if (!qstr(f.qualityLabel).isEmpty())      s.description = qstr(f.qualityLabel);
    else if (s.width > 0)                     s.description = QString::number(s.height) + "p";
    else if (!qstr(f.audioQuality).isEmpty()) s.description = qstr(f.audioQuality);
    else                                      s.description = mime.section(QLatin1Char(';'), 0, 0);
    return true;
}

// The FULL stream catalog: HLS manifest + every url-ful muxed / video-only /
// audio-only format, each tagged (see CT::Stream). StreamSet slices this into
// its video/audio lists and default picks; consumers can select any entry.
static QList<CT::Stream> streamsOf(const pj::PlayerRoot &root, bool *sawCipheredOnly)
{
    QList<CT::Stream> out;
    if (sawCipheredOnly) *sawCipheredOnly = false;
    if (!root.streamingData) return out;
    const pj::StreamingData &sd = *root.streamingData;
    const QString hls = qstr(sd.hlsManifestUrl);
    if (!hls.isEmpty()) {
        CT::Stream s; s.id = "hls"; s.description = "HLS (adaptive)"; s.url = hls; s.hasAudio = true;
        out << s;
    }
    int formatsSeen = 0;
    if (sd.formats)         // muxed video+audio (itag 18/22)
        for (const pj::Format &f : *sd.formats) {
            ++formatsSeen;
            CT::Stream s; if (streamOf(f, true, s)) out << s;
        }
    if (sd.adaptiveFormats) // separate video-only + audio-only tracks
        for (const pj::Format &f : *sd.adaptiveFormats) {
            ++formatsSeen;
            CT::Stream s; if (streamOf(f, false, s)) out << s;
        }
    // Nothing playable but formats were present → every format was ciphered.
    if (sawCipheredOnly) *sawCipheredOnly = out.isEmpty() && formatsSeen > 0;
    return out;
}

static CT::Video detailsOf(const pj::PlayerRoot &root)
{
    CT::Video v;
    if (!root.videoDetails) return v;
    const pj::VideoDetails &d = *root.videoDetails;
    v.id = qstr(d.videoId);
    v.title = qstr(d.title);
    v.username = qstr(d.author);
    v.userId = qstr(d.channelId);
    v.description = qstr(d.shortDescription);
    v.viewCount = toInt64(d.viewCount);
    if (d.thumbnail) v.thumbnailUrl = qstr(lastThumbUrl(*d.thumbnail));
    if (v.thumbnailUrl.isEmpty() && !v.id.isEmpty())
        v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
    v.largeThumbnailUrl = v.thumbnailUrl;
    return v;
}

static QList<CT::Subtitle> captionsOf(const pj::PlayerRoot &root)
{
    QList<CT::Subtitle> out;
    if (!root.captions || !root.captions->playerCaptionsTracklistRenderer) return out;
    const pj::TracklistR &tl = *root.captions->playerCaptionsTracklistRenderer;
    if (!tl.captionTracks) return out;
    for (const pj::CaptionTrack &t : *tl.captionTracks) {
        CT::Subtitle s;
        s.url = qstr(t.baseUrl);
        s.language = qstr(t.languageCode);
        s.title = qstr(textOf(t.name));
        s.id = s.language;
        if (!s.url.isEmpty()) out << s;
    }
    return out;
}

// ---------------------------------------------------------------------------
// parsePlayer — ONE typed read, all four sections
// ---------------------------------------------------------------------------

static PlayerResult parsePlayerRoot(const pj::PlayerRoot &root)
{
    PlayerResult r;
    r.playable = playableOf(root, &r.reason);
    r.streams  = streamsOf(root, &r.cipheredOnly);
    if (root.streamingData) {
        const pj::StreamingData &sd = *root.streamingData;
        r.sabr = sd.serverAbrStreamingUrl && !sd.serverAbrStreamingUrl->empty();
        r.formatsSeen  = sd.formats ? (int)sd.formats->size() : 0;
        r.adaptiveSeen = sd.adaptiveFormats ? (int)sd.adaptiveFormats->size() : 0;
    }
    r.details  = detailsOf(root);
    r.captions = captionsOf(root);
    return r;
}

PlayerResult parsePlayer(std::string_view p)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    return parsePlayerRoot(root);
}

// Whole-document overload: *r.body is NUL-terminated, so read via the sentinel
// path (kInDoc). Behavior-identical to the string_view form (golden diff gate).
PlayerResult parsePlayer(const std::string &p)
{
    pj::PlayerRoot root{};
    readJsonDoc(root, p);
    return parsePlayerRoot(root);
}

// ---------------------------------------------------------------------------
// Public entry points — thin wrappers, each does its own single read
// ---------------------------------------------------------------------------

bool isPlayable(std::string_view p, QString *reason)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    return playableOf(root, reason);
}
bool isPlayable(const std::string &p, QString *reason)
{
    pj::PlayerRoot root{};
    readJsonDoc(root, p);
    return playableOf(root, reason);
}

QList<CT::Stream> parseStreams(std::string_view p, bool *sawCipheredOnly)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    return streamsOf(root, sawCipheredOnly);
}
QList<CT::Stream> parseStreams(const std::string &p, bool *sawCipheredOnly)
{
    pj::PlayerRoot root{};
    readJsonDoc(root, p);
    return streamsOf(root, sawCipheredOnly);
}

CT::Video parseVideoDetails(std::string_view p)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    return detailsOf(root);
}
CT::Video parseVideoDetails(const std::string &p)
{
    pj::PlayerRoot root{};
    readJsonDoc(root, p);
    return detailsOf(root);
}

QList<CT::Subtitle> parseCaptions(std::string_view p)
{
    pj::PlayerRoot root{};
    (void)glz::read<kIn>(root, p);
    return captionsOf(root);
}
QList<CT::Subtitle> parseCaptions(const std::string &p)
{
    pj::PlayerRoot root{};
    readJsonDoc(root, p);
    return captionsOf(root);
}
}
