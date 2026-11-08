#pragma once
// Walkie audio over ESP-NOW: IMA ADPCM frames, a jitter buffer with loss concealment, and a
// microphone level control. No Arduino dependency, so firmware/tools/voice-codec-test.cpp runs it
// on the PC.
//
// Why: until v0.57 nearby walkie audio was raw 16-bit PCM at 8 kHz, cut into 71-sample pieces,
// three broadcast packets every 20 ms (150 a second, no acknowledgement), each played as soon as
// it landed with at most two pieces (about 18 ms) queued at the speaker. Any lost packet was a
// hole and any stall of the main loop longer than 18 ms was another. ADPCM is a quarter of the
// size, so one packet carries 30 ms, each packet is sent twice, and the receiver holds 90 ms before
// it starts and covers a lost frame by repeating the last one, quieter.
#include <stdint.h>
#include <string.h>

namespace voice {

constexpr uint32_t kSampleRate = 8000;
constexpr size_t kFrameSamples = 240;               // 30 ms at 8 kHz.
constexpr size_t kFrameBytes = kFrameSamples / 2;   // 4 bits a sample.

// --- IMA ADPCM (the standard step and index tables) ---
constexpr int16_t kStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
constexpr int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

struct AdpcmState { int16_t predictor = 0; uint8_t index = 0; };

inline int16_t clamp16(int32_t value) { return value > 32767 ? 32767 : value < -32768 ? -32768 : static_cast<int16_t>(value); }

inline uint8_t adpcmEncodeSample(AdpcmState &state, int16_t sample) {
  const int step = kStepTable[state.index];
  int diff = sample - state.predictor;
  uint8_t code = 0;
  if (diff < 0) { code = 8; diff = -diff; }
  int delta = step >> 3;
  if (diff >= step) { code |= 4; diff -= step; delta += step; }
  if (diff >= (step >> 1)) { code |= 2; diff -= step >> 1; delta += step >> 1; }
  if (diff >= (step >> 2)) { code |= 1; delta += step >> 2; }
  state.predictor = clamp16(code & 8 ? state.predictor - delta : state.predictor + delta);
  const int index = state.index + kIndexTable[code];
  state.index = static_cast<uint8_t>(index < 0 ? 0 : index > 88 ? 88 : index);
  return code;
}

inline int16_t adpcmDecodeSample(AdpcmState &state, uint8_t code) {
  const int step = kStepTable[state.index];
  int delta = step >> 3;
  if (code & 4) delta += step;
  if (code & 2) delta += step >> 1;
  if (code & 1) delta += step >> 2;
  state.predictor = clamp16(code & 8 ? state.predictor - delta : state.predictor + delta);
  const int index = state.index + kIndexTable[code & 15];
  state.index = static_cast<uint8_t>(index < 0 ? 0 : index > 88 ? 88 : index);
  return state.predictor;
}

// One frame on the air. Each frame carries the coder's state at its start, so it decodes on its
// own: a lost frame costs 30 ms, never the rest of the talk.
struct __attribute__((packed)) Frame {
  uint16_t sequence;     // Counts frames within one press of the talk key.
  uint16_t talk;         // Changes on every press, so a new talk never waits behind an old one.
  int16_t predictor;
  uint8_t index;
  uint8_t flags;         // Bit 0: last frame of the talk.
  uint8_t data[kFrameBytes];
};
static_assert(sizeof(Frame) == 8 + kFrameBytes, "Frame layout");

inline void encodeFrame(AdpcmState &state, const int16_t *samples, Frame &frame) {
  frame.predictor = state.predictor; frame.index = state.index;
  for (size_t i = 0; i < kFrameSamples; i += 2) {
    const uint8_t low = adpcmEncodeSample(state, samples[i]);
    const uint8_t high = adpcmEncodeSample(state, samples[i + 1]);
    frame.data[i / 2] = static_cast<uint8_t>(low | (high << 4));
  }
}

inline void decodeFrame(const Frame &frame, int16_t *samples) {
  AdpcmState state; state.predictor = frame.predictor; state.index = frame.index > 88 ? 88 : frame.index;
  for (size_t i = 0; i < kFrameSamples; i += 2) {
    samples[i] = adpcmDecodeSample(state, frame.data[i / 2] & 15);
    samples[i + 1] = adpcmDecodeSample(state, frame.data[i / 2] >> 4);
  }
}

// --- Microphone level ---
// A DC blocker, then a gain that rises slowly towards a target level and falls at once when the
// voice gets loud, then a hard limit. It keeps a quiet voice audible and a loud one from clipping
// the 4-bit coder. It can't undo clipping that happened before it, in the microphone path.
struct Agc {
  int32_t dcIn = 0, dcOut = 0;     // DC blocker state (Q0).
  int32_t gainQ8 = 256;            // 1.0 in Q8.
  static constexpr int32_t kTargetPeak = 12000;   // About -8.7 dBFS.
  static constexpr int32_t kMaxGainQ8 = 256 * 6;  // At most 6x (15.6 dB) for a far or quiet voice.
  static constexpr int32_t kMinGainQ8 = 64;       // At least 0.25x.
  static constexpr int32_t kNoiseFloor = 300;     // Blocks quieter than this don't raise the gain.

  void reset() { dcIn = dcOut = 0; gainQ8 = 256; }

  void process(int16_t *samples, size_t count) {
    int32_t peak = 0;
    for (size_t i = 0; i < count; i++) {
      // y[n] = x[n] - x[n-1] + 0.995 y[n-1]: removes DC and rumble under about 6 Hz.
      const int32_t x = samples[i];
      const int32_t y = x - dcIn + (dcOut * 255) / 256;
      dcIn = x; dcOut = y;
      samples[i] = clamp16(y);
      const int32_t magnitude = y < 0 ? -y : y;
      if (magnitude > peak) peak = magnitude;
    }
    // Attack: the gain that puts this block's peak at the target, if that is lower.
    if (peak > 0 && (peak * gainQ8) / 256 > kTargetPeak) gainQ8 = (kTargetPeak * 256) / peak;
    else if (peak > kNoiseFloor && gainQ8 < kMaxGainQ8) gainQ8 += 4;  // Release: about +1.5% a block.
    if (gainQ8 < kMinGainQ8) gainQ8 = kMinGainQ8;
    if (gainQ8 > kMaxGainQ8) gainQ8 = kMaxGainQ8;
    for (size_t i = 0; i < count; i++) {
      int32_t v = (samples[i] * gainQ8) / 256;
      if (v > 30000) v = 30000; else if (v < -30000) v = -30000;  // The limiter.
      samples[i] = static_cast<int16_t>(v);
    }
  }
};

// --- Jitter buffer ---
// Frames go in by sequence number in any order, duplicates ignored. Playback pulls one frame at a
// time, paced by the speaker. It waits for kStartFrames before the first, so late packets still
// make it, and covers a missing frame by repeating the last one at half the level, twice at most,
// then silence. A talk ends when its last frame is played or nothing has arrived for kIdleMs.
class JitterBuffer {
 public:
  static constexpr size_t kSlots = 8;           // 240 ms: RAM is short; a frame later than that is no use anyway.
  static constexpr size_t kStartFrames = 3;     // 90 ms held before the first frame plays.
  static constexpr uint32_t kStartWaitMs = 150; // Or this long after the first one arrives.
  static constexpr uint32_t kIdleMs = 400;

  struct Stats { uint32_t received = 0, duplicates = 0, late = 0, concealed = 0, played = 0; };

  // Returns false for a duplicate, a frame too late to play, or one from an older talk.
  bool push(const Frame &frame, uint32_t nowMs) {
    // Late copies of a talk that has finished don't start it again.
    if (!active_ && endedValid_ && frame.talk == endedTalk_ && nowMs - endedAt_ < 2000) { stats.late++; return false; }
    if (!active_ || frame.talk != talk_) {
      // Someone else is mid-talk: the first talker keeps the channel until they stop.
      if (active_ && nowMs - lastArrivalAt_ <= kIdleMs) { stats.late++; return false; }
      reset(); active_ = true; talk_ = frame.talk; next_ = frame.sequence; firstAt_ = nowMs; lastArrivalAt_ = nowMs;
    }
    const int16_t ahead = static_cast<int16_t>(frame.sequence - next_);
    if (ahead < 0) { stats.late++; return false; }
    if (ahead >= static_cast<int16_t>(kSlots)) {  // Far ahead: we fell behind; skip forward.
      for (auto &slot : slots_) slot.used = false;
      next_ = static_cast<uint16_t>(frame.sequence - (kStartFrames - 1));
    }
    Slot &slot = slots_[frame.sequence % kSlots];
    if (slot.used && slot.frame.sequence == frame.sequence) { stats.duplicates++; return false; }
    slot.frame = frame; slot.used = true; lastArrivalAt_ = nowMs; stats.received++;
    if (frame.flags & 1) { endSeen_ = true; endSequence_ = frame.sequence; }
    return true;
  }

  // True when a frame (real or concealed) was written to `out`; false when nothing is due.
  bool pull(int16_t *out, uint32_t nowMs) {
    if (!active_) return false;
    if (!playing_) {
      if (buffered() < kStartFrames && nowMs - firstAt_ < kStartWaitMs && !endSeen_) return false;
      playing_ = true;
    }
    if (endSeen_ && static_cast<int16_t>(next_ - endSequence_) > 0) { finish(nowMs); return false; }
    Slot &slot = slots_[next_ % kSlots];
    if (slot.used && slot.frame.sequence == next_) {
      decodeFrame(slot.frame, out); slot.used = false;
      memcpy(last_, out, sizeof(last_)); concealedRun_ = 0; stats.played++;
    } else {
      if (nowMs - lastArrivalAt_ > kIdleMs && buffered() == 0) { finish(nowMs); return false; }
      concealedRun_++; stats.concealed++;
      if (concealedRun_ <= 2) {
        for (size_t i = 0; i < kFrameSamples; i++) { last_[i] /= 2; out[i] = last_[i]; }
      } else memset(out, 0, kFrameSamples * sizeof(int16_t));
    }
    next_++;
    return true;
  }

  bool active() const { return active_; }
  size_t buffered() const { size_t n = 0; for (const auto &slot : slots_) n += slot.used; return n; }
  void reset() {
    for (auto &slot : slots_) slot.used = false;
    active_ = playing_ = endSeen_ = false; concealedRun_ = 0; memset(last_, 0, sizeof(last_));
  }
  Stats stats;

 private:
  void finish(uint32_t nowMs) { endedTalk_ = talk_; endedAt_ = nowMs; endedValid_ = true; reset(); }
  struct Slot { Frame frame; bool used = false; };
  Slot slots_[kSlots];
  int16_t last_[kFrameSamples]{};
  uint16_t talk_ = 0, next_ = 0, endSequence_ = 0;
  uint32_t firstAt_ = 0, lastArrivalAt_ = 0, endedAt_ = 0;
  uint16_t endedTalk_ = 0;
  bool endedValid_ = false;
  uint8_t concealedRun_ = 0;
  bool active_ = false, playing_ = false, endSeen_ = false;
};

}  // namespace voice
