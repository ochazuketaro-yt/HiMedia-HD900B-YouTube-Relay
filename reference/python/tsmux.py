#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""tsmux.py -- reference implementation (Python) of the per-segment muxer that hdrelay performs in C++.

Mux ONE YouTube video-TS segment (H.264 only) with the matching packed-AAC audio segment (ID3 + ADTS) into ONE MPEG-TS segment.
Output: [PAT][PMT(video+audio)] then video and audio packets interleaved by PTS.
Audio PTS = ID3 PRIV 'com.apple.streaming.transportStreamTimestamp' (8-byte big-endian, 90 kHz) + frame_index*1024/sample_rate.

    python3 tsmux.py video_seg.ts audio_seg.aac out.ts       # then: ffprobe out.ts ; ffmpeg -v warning -i out.ts -f null -
"""
import struct, sys

SYNC = 0x47
def crc32_mpeg(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc

def packets(ts):
    n = len(ts) // 188
    for i in range(n):
        p = ts[i * 188:(i + 1) * 188]
        if p[0] != SYNC: raise ValueError("lost TS sync at packet %d" % i)
        yield p

def pid_of(p): return ((p[1] & 0x1F) << 8) | p[2]

def payload_of(p):
    afc = (p[3] >> 4) & 3
    pos = 4
    if afc & 2: pos += 1 + p[4]
    return p[pos:] if (afc & 1) and pos < 188 else b""

def read_pes_pts(pl):
    if pl[:3] != b"\x00\x00\x01" or len(pl) < 14 or not (pl[7] & 0x80): return None
    return (((pl[9] >> 1) & 7) << 30) | (pl[10] << 22) | ((pl[11] >> 1) << 15) | (pl[12] << 7) | (pl[13] >> 1)

def read_pes_dts(pl):
    if pl[:3] != b"\x00\x00\x01" or len(pl) < 19 or (pl[7] & 0xC0) != 0xC0: return None
    return (((pl[14] >> 1) & 7) << 30) | (pl[15] << 22) | ((pl[16] >> 1) << 15) | (pl[17] << 7) | (pl[18] >> 1)

def enc_pts(v, marker):
    return bytes([(marker << 4) | (((v >> 30) & 7) << 1) | 1, (v >> 22) & 0xFF, (((v >> 15) & 0x7F) << 1) | 1, (v >> 7) & 0xFF, ((v & 0x7F) << 1) | 1])

def psi_packet(pid, section, cc):
    pl = b"\x00" + section                       # pointer_field = 0
    assert len(pl) <= 184
    return bytes([SYNC, 0x40 | (pid >> 8), pid & 0xFF, 0x10 | (cc & 15)]) + pl + b"\xFF" * (184 - len(pl))

def build_pmt(program, pmt_pid, pcr_pid, streams, version=0):
    es = b"".join(bytes([st, 0xE0 | (pid >> 8), pid & 0xFF, 0xF0, 0x00]) for st, pid in streams)
    body = struct.pack(">HBBBHH", program, 0xC1 | (version << 1), 0, 0, 0xE000 | pcr_pid, 0xF000) + es
    # section_length covers everything after the length field up to and incl. CRC
    sec = bytes([0x02]) + struct.pack(">H", 0xB000 | (len(body) - 3 + 5 - 2 + 0)) if False else None
    after_len = struct.pack(">H", program) + bytes([0xC1 | (version << 1), 0, 0]) + struct.pack(">H", 0xE000 | pcr_pid) + struct.pack(">H", 0xF000) + es
    section = bytes([0x02]) + struct.pack(">H", 0xB000 | (len(after_len) + 4)) + after_len
    return section + struct.pack(">I", crc32_mpeg(section))

def parse_id3_ts(aac):
    if aac[:3] != b"ID3": return None, 0
    size = ((aac[6] & 0x7F) << 21) | ((aac[7] & 0x7F) << 14) | ((aac[8] & 0x7F) << 7) | (aac[9] & 0x7F)
    tag = aac[10:10 + size]; ts = None; i = 0
    while i + 10 <= len(tag):
        fid = tag[i:i + 4]; fsz = struct.unpack(">I", tag[i + 4:i + 8])[0]
        if fid == b"PRIV":
            body = tag[i + 10:i + 10 + fsz]; z = body.find(b"\x00")
            if body[:z].decode("latin1").lower().endswith("transportstreamtimestamp") and len(body) >= z + 9:
                ts = struct.unpack(">Q", body[z + 1:z + 9])[0] & ((1 << 33) - 1)
        if fsz == 0: break
        i += 10 + fsz
    return ts, 10 + size

SR = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]
def adts_frames(buf, start):
    i, out = start, []
    while i + 7 <= len(buf):
        if buf[i] != 0xFF or (buf[i + 1] & 0xF0) != 0xF0: i += 1; continue
        fl = ((buf[i + 3] & 3) << 11) | (buf[i + 4] << 3) | (buf[i + 5] >> 5)
        if fl < 7 or i + fl > len(buf): break
        out.append((i, fl, SR[(buf[i + 2] >> 2) & 15])); i += fl
    return out

def pes_to_ts(pid, pes, cc, first_pcr=None):
    out = []; pos = 0; first = True
    while pos < len(pes):
        room = 184
        chunk = pes[pos:pos + room]
        if len(chunk) < room:                      # last packet: stuff with an adaptation field
            stuff = room - len(chunk)
            af = bytes([stuff - 1]) + (b"\x00" + b"\xFF" * (stuff - 2) if stuff >= 2 else b"")
            p = bytes([SYNC, (0x40 if first else 0) | (pid >> 8), pid & 0xFF, 0x30 | (cc & 15)]) + af + chunk
        else:
            p = bytes([SYNC, (0x40 if first else 0) | (pid >> 8), pid & 0xFF, 0x10 | (cc & 15)]) + chunk
        out.append(p); cc += 1; pos += len(chunk); first = False
    return out, cc

def mux(video_ts, audio, audio_pid=257, frames_per_pes=6):
    pkts = list(packets(video_ts))
    pat = next(p for p in pkts if pid_of(p) == 0)
    # program / PMT pid from the PAT
    pl = payload_of(pat); sec = pl[1 + pl[0]:]
    prog = (sec[8] << 8) | sec[9]; pmt_pid = ((sec[10] & 0x1F) << 8) | sec[11]
    pmt = next(p for p in pkts if pid_of(p) == pmt_pid)
    pl = payload_of(pmt); sec = pl[1 + pl[0]:]
    pcr_pid = ((sec[8] & 0x1F) << 8) | sec[9]; pil = ((sec[10] & 0xF) << 8) | sec[11]
    k = 12 + pil; vstreams = []
    end = 3 + (((sec[1] & 0xF) << 8) | sec[2]) - 4
    while k + 5 <= end:
        st, epid, il = sec[k], ((sec[k + 1] & 0x1F) << 8) | sec[k + 2], ((sec[k + 3] & 0xF) << 8) | sec[k + 4]
        vstreams.append((st, epid)); k += 5 + il
    vpid = next(e for s, e in vstreams if s == 0x1B)
    # video packets with a sort key = PTS (or DTS) of the PES they belong to
    vitems = []; key = 0
    for p in pkts:
        if pid_of(p) != vpid: continue
        if p[1] & 0x40:
            pl = payload_of(p); d = read_pes_dts(pl); t = read_pes_pts(pl)
            key = d if d is not None else (t if t is not None else key)
        vitems.append((key, p))
    # audio
    base, off = parse_id3_ts(audio)
    if base is None: base = vitems[0][0] if vitems else 0      # fallback: align with the first video timestamp
    frames = adts_frames(audio, off); aitems = []; cc = 0
    for g in range(0, len(frames), frames_per_pes):
        grp = frames[g:g + frames_per_pes]; sr = grp[0][2]
        pts = (base + (g * 1024 * 90000) // sr) & ((1 << 33) - 1)
        data = b"".join(audio[i:i + fl] for i, fl, _ in grp)
        pes = b"\x00\x00\x01\xC0" + struct.pack(">H", 3 + 5 + len(data)) + bytes([0x80, 0x80, 5]) + enc_pts(pts, 2) + data
        tsp, cc = pes_to_ts(audio_pid, pes, cc)
        aitems += [(pts, p) for p in tsp]
    merged = sorted(vitems + aitems, key=lambda kv: kv[0])      # stable: video before audio at equal keys is not guaranteed -> sort is stable on input order (video first)
    newpmt = build_pmt(prog, pmt_pid, pcr_pid, [(0x1B, vpid), (0x0F, audio_pid)])
    out = [psi_packet(0, bytes(sec_pat(prog, pmt_pid)), 0), psi_packet(pmt_pid, newpmt, 0)] + [p for _, p in merged]
    return b"".join(out), {"video_packets": len(vitems), "audio_packets": len(aitems), "audio_frames": len(frames), "audio_base_pts": base, "video_first_key": vitems[0][0] if vitems else None}

def sec_pat(prog, pmt_pid):
    body = struct.pack(">HBBB", 1, 0xC1, 0, 0) + struct.pack(">HH", prog, 0xE000 | pmt_pid)
    section = bytes([0x00]) + struct.pack(">H", 0xB000 | (len(body) + 4)) + body
    return section + struct.pack(">I", crc32_mpeg(section))

if __name__ == "__main__":
    v = open(sys.argv[1], "rb").read(); a = open(sys.argv[2], "rb").read()
    out, info = mux(v, a); open(sys.argv[3], "wb").write(out); print(info, len(out))
