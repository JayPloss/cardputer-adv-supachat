// Finding the audio in an MP3 file: skip tags, find a real frame, time and seek it.
// Plain C++ with no Arduino types, so firmware/tools/mp3-probe-test.cpp runs it on the PC.
//
// What it handles, because real files have all of it:
// - An ID3v2 tag at the start (any version, with or without a footer), however big: cover art makes it
//   30-300 KB, and its bytes can look like a frame header to a plain sync search.
// - Junk or padding between the tag and the audio.
// - A Xing/Info (LAME) or VBRI header frame, which gives the real frame count of a VBR file, and a seek
//   table (Xing TOC) when there is one.
// - MPEG 1 and 2 Layer III, mono or stereo, 16 to 48 kHz.
// What it refuses, saying why: MPEG 2.5 (8-12 kHz; the pinned libhelix is built without it, mp3common.h),
// Layer I and II (MP2), free-format bitrate, anything without two frames in a row.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mp3probe {

struct Frame {
  uint8_t version = 0;   // 1 = MPEG 1, 2 = MPEG 2, 3 = MPEG 2.5
  uint8_t layer = 0;     // 1, 2 or 3
  bool mono = false;
  uint32_t bitrate = 0;  // bits per second
  uint32_t sampleRate = 0;
  uint32_t samples = 0;  // per frame
  uint32_t bytes = 0;    // frame length, padding included
};

// The ID3v2 tag's full length (header, body, footer), or 0 when the bytes don't start one.
inline uint32_t id3v2Length(const uint8_t *b, size_t n) {
  if (n < 10 || b[0] != 'I' || b[1] != 'D' || b[2] != '3') return 0;
  if (b[3] == 0xFF || b[4] == 0xFF) return 0;
  if ((b[6] | b[7] | b[8] | b[9]) & 0x80) return 0;  // sizes are 7 bits a byte
  const uint32_t body = (uint32_t(b[6]) << 21) | (uint32_t(b[7]) << 14) | (uint32_t(b[8]) << 7) | b[9];
  const bool footer = b[3] >= 4 && (b[5] & 0x10);
  return 10 + body + (footer ? 10 : 0);
}

// Reads a frame header at b (4 bytes). Any layer, so an MP2 file can be named as one.
inline bool parseHeader(const uint8_t *b, Frame &f) {
  if (b[0] != 0xFF || (b[1] & 0xE0) != 0xE0) return false;
  const uint8_t versionBits = (b[1] >> 3) & 3, layerBits = (b[1] >> 1) & 3;
  const uint8_t bitrateIndex = b[2] >> 4, rateIndex = (b[2] >> 2) & 3, padding = (b[2] >> 1) & 1;
  if (versionBits == 1 || layerBits == 0 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3) return false;
  if ((b[3] & 3) == 2) return false;  // reserved emphasis
  f.version = versionBits == 3 ? 1 : versionBits == 2 ? 2 : 3;
  f.layer = 4 - layerBits;
  f.mono = (b[3] >> 6) == 3;
  static const uint16_t kbps[2][3][15] = {
      {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
       {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
       {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}},
      {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}}};
  static const uint16_t rates[3] = {44100, 48000, 32000};
  f.bitrate = kbps[f.version == 1 ? 0 : 1][f.layer - 1][bitrateIndex] * 1000u;
  f.sampleRate = rates[rateIndex] >> (f.version - 1);
  if (f.layer == 1) {
    f.samples = 384;
    f.bytes = (12 * f.bitrate / f.sampleRate + padding) * 4;
  } else {
    f.samples = (f.layer == 3 && f.version != 1) ? 576 : 1152;
    f.bytes = f.samples / 8 * f.bitrate / f.sampleRate + padding;
  }
  return true;
}

inline bool sameStream(const Frame &a, const Frame &b) {
  return a.version == b.version && a.layer == b.layer && a.sampleRate == b.sampleRate;
}

enum class Found { Frame, NeedMore, None };

// Looks in b[0..n) for a frame header that a second one follows exactly one frame later. A frame whose
// successor lies past n gives NeedMore with `at` set, so the caller can read on from there and ask again.
// `layer` reports what the first plausible frame was, so a Layer II file can be told apart.
inline Found findFrame(const uint8_t *b, size_t n, size_t &at, Frame &f, uint8_t *layer = nullptr) {
  for (size_t i = 0; i + 4 <= n; i++) {
    Frame first;
    if (!parseHeader(b + i, first)) continue;
    if (layer && !*layer) *layer = first.layer;
    if (first.layer != 3) continue;
    const size_t next = i + first.bytes;
    if (next + 4 > n) { at = i; f = first; return Found::NeedMore; }
    Frame second;
    if (parseHeader(b + next, second) && sameStream(first, second)) { at = i; f = first; return Found::Frame; }
  }
  return Found::None;
}

struct VbrInfo {
  bool present = false;  // a Xing, Info or VBRI frame: it carries no audio
  uint32_t frames = 0;
  uint32_t bytes = 0;
  bool hasToc = false;
  uint8_t toc[100] = {};
};

inline uint32_t be32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

// Reads the Xing/Info or VBRI header in the first frame, if it has one. b holds the whole frame.
inline VbrInfo readVbr(const uint8_t *b, const Frame &f) {
  VbrInfo v;
  const size_t side = f.version == 1 ? (f.mono ? 17 : 32) : (f.mono ? 9 : 17);
  const uint8_t *x = b + 4 + side;
  if (4 + side + 8 <= f.bytes && (!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4))) {
    v.present = true;
    const uint32_t flags = be32(x + 4);
    size_t p = 8;
    if ((flags & 1) && 4 + side + p + 4 <= f.bytes) { v.frames = be32(x + p); p += 4; }
    if ((flags & 2) && 4 + side + p + 4 <= f.bytes) { v.bytes = be32(x + p); p += 4; }
    if ((flags & 4) && 4 + side + p + 100 <= f.bytes) { memcpy(v.toc, x + p, 100); v.hasToc = true; }
    return v;
  }
  if (4 + 32 + 18 <= f.bytes && !memcmp(b + 4 + 32, "VBRI", 4)) {
    v.present = true;
    v.bytes = be32(b + 4 + 32 + 10);
    v.frames = be32(b + 4 + 32 + 14);
  }
  return v;
}

// Timing and seeking for a file whose audio starts at `audioStart` (the first frame, header frame included)
// and ends at `audioEnd`.
struct Timeline {
  uint32_t audioStart = 0, audioEnd = 0, firstAudio = 0;  // firstAudio skips a Xing/VBRI frame
  uint32_t seconds = 0;
  double bytesPerSecond = 0;
  VbrInfo vbr;
};

inline Timeline timeline(const Frame &f, const VbrInfo &vbr, uint32_t audioStart, uint32_t fileSize) {
  Timeline t;
  t.vbr = vbr;
  t.audioStart = audioStart;
  t.audioEnd = fileSize > audioStart ? fileSize : audioStart;
  t.firstAudio = audioStart + (vbr.present ? f.bytes : 0);
  if (vbr.present && vbr.bytes > f.bytes && t.audioStart + vbr.bytes <= fileSize + f.bytes) t.audioEnd = t.audioStart + vbr.bytes;
  const uint32_t span = t.audioEnd - t.firstAudio;
  if (vbr.present && vbr.frames) {
    const double duration = double(vbr.frames) * f.samples / f.sampleRate;
    t.seconds = uint32_t(duration);
    t.bytesPerSecond = duration > 0 ? span / duration : f.bitrate / 8.0;
  } else {
    t.bytesPerSecond = f.bitrate / 8.0;
    t.seconds = uint32_t(span / t.bytesPerSecond);
  }
  return t;
}

// The byte to read from to start `at` seconds in. The decoder finds the frame after it.
inline uint32_t seekByte(const Timeline &t, uint32_t at) {
  if (!at) return t.firstAudio;
  if (t.seconds && at >= t.seconds) return t.audioEnd;
  if (t.vbr.hasToc && t.seconds) {
    const double percent = 100.0 * at / t.seconds;
    const int i = int(percent);
    const double a = t.vbr.toc[i], b = i < 99 ? t.vbr.toc[i + 1] : 256.0;
    const double fraction = (a + (b - a) * (percent - i)) / 256.0;
    return t.audioStart + uint32_t(fraction * (t.audioEnd - t.audioStart));
  }
  return t.firstAudio + uint32_t(at * t.bytesPerSecond);
}

// The whole open: past the tag and any junk to two frames in a row, then the VBR header and timeline.
// `read(pos, buf, n)` returns the bytes read. `scratch` must hold the largest frame (2881 bytes).
// On failure `reason` says why, for the screen.
template <typename Read>
bool locate(Read read, uint32_t fileSize, uint8_t *scratch, size_t scratchBytes, Timeline &line, Frame &frame,
            const char *&reason, uint32_t searchBytes = 256 * 1024) {
  reason = "NOT AN MP3";
  uint8_t head[10] = {};
  const uint32_t tag = read(0, head, sizeof head) == int(sizeof head) ? id3v2Length(head, sizeof head) : 0;
  uint32_t pos = tag;
  uint8_t layerSeen = 0;
  while (pos < fileSize && pos - tag < searchBytes) {
    const int got = read(pos, scratch, scratchBytes);
    if (got < 4) break;
    size_t at = 0;
    const Found result = findFrame(scratch, got, at, frame, &layerSeen);
    if (result == Found::Frame) {
      if (frame.version == 3) { reason = "8-12 KHZ MP3: UNSUPPORTED"; return false; }
      const uint32_t start = pos + uint32_t(at);
      const size_t frameBytes = frame.bytes < scratchBytes ? frame.bytes : scratchBytes;
      const VbrInfo vbr = read(start, scratch, frameBytes) == int(frameBytes) ? readVbr(scratch, frame) : VbrInfo();
      line = timeline(frame, vbr, start, fileSize);
      return true;
    }
    if (result == Found::NeedMore && at > 0) { pos += uint32_t(at); continue; }  // read on from that frame
    if (size_t(got) < scratchBytes) break;                                      // the file ended
    pos += uint32_t(got) - 3;  // a header can straddle the boundary
  }
  if (layerSeen && layerSeen != 3) reason = "MP2, NOT MP3";
  return false;
}

}  // namespace mp3probe
