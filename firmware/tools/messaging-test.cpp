// Host test of the outbox and the walkie codec.
//   g++ -std=c++11 -I firmware/include firmware/tools/messaging-test.cpp -o /tmp/messaging-test && /tmp/messaging-test
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "outbox.h"
#include "voice_codec.h"

static double snrDb(const std::vector<int16_t> &a, const std::vector<int16_t> &b) {
  double signal = 0, noise = 0;
  for (size_t i = 0; i < a.size(); i++) { signal += double(a[i]) * a[i]; const double d = double(a[i]) - b[i]; noise += d * d; }
  return 10 * std::log10(signal / (noise + 1e-9));
}

static void testOutbox() {
  using namespace outbox;
  // What each answer means.
  assert(classify(201) == Outcome::Accepted && classify(200) == Outcome::Accepted);
  for (int s : {-1, -5, -11, 0, 408, 429, 500, 502, 503}) assert(classify(s) == Outcome::Retry);
  for (int s : {400, 401, 403, 404, 409, 413}) assert(classify(s) == Outcome::Failed);
  // Backoff doubles from 5 s and stops at 5 minutes.
  assert(backoffMs(1) == 5000 && backoffMs(2) == 10000 && backoffMs(3) == 20000 && backoffMs(20) == 300000);
  // A transport error waits; a refusal fails and stops; retry starts it again.
  Entry e; e.clientId = "naomie-1-1"; e.roomId = "wolfpack"; e.body = "salut";
  recordAttempt(e, -5, 1000); assert(e.state == State::Sending && e.nextAttemptAt == 6000 && !due(5999, e.nextAttemptAt) && due(6000, e.nextAttemptAt));
  recordAttempt(e, 403, 7000); assert(e.state == State::Failed && e.lastStatus == 403);
  retry(e, 9000); assert(e.state == State::Sending && e.attempts == 0 && due(9000, e.nextAttemptAt));
  // due() across the millis() wrap.
  assert(due(5, 0xFFFFFFF0u) && !due(0xFFFFFFF0u, 5));
  // Storage keeps every field, including tabs, newlines, backslashes and accents in the text.
  std::vector<Entry> list(2);
  list[0] = e; list[0].body = "a\tb\\n c\nd \xC3\xA9t\xC3\xA9"; list[0].replyToId = 1208; list[0].createdAt = 1794074409090LL; list[0].lastStatus = -5;
  list[1].clientId = "andrew-2-7"; list[1].roomId = "wolfpack"; list[1].body = "ok"; list[1].state = State::Failed; list[1].lastStatus = 401;
  const auto back = parse(serialize(list));
  assert(back.size() == 2);
  assert(back[0].body == list[0].body && back[0].clientId == e.clientId && back[0].replyToId == 1208 && back[0].createdAt == 1794074409090LL && back[0].lastStatus == -5);
  assert(back[1].state == State::Failed && back[1].lastStatus == 401 && back[1].body == "ok");
  // A line cut off by a power loss is dropped, and the whole ones kept.
  std::string torn = serialize(list); torn = torn.substr(0, torn.size() - 6);
  assert(parse(torn).size() == 1);
  // The 16-entry limit at 140 characters fits one NVS string (under 4000 bytes).
  std::vector<Entry> full(kMaxEntries);
  for (auto &x : full) { x.clientId = "emmanuelle-ffffffff-65535"; x.roomId = "wolfpack"; x.body = std::string(140, 'x'); x.createdAt = 1794074409090LL; }
  printf("outbox: 16 full entries serialize to %zu bytes\n", serialize(full).size());
  assert(serialize(full).size() < 4000);
}

static void testCodec() {
  using namespace voice;
  // A voice-like signal: two tones with a slow envelope.
  std::vector<int16_t> in(kFrameSamples * 40), out(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    const double t = double(i) / kSampleRate, env = 0.5 + 0.5 * std::sin(2 * M_PI * 3 * t);
    in[i] = int16_t(env * (6000 * std::sin(2 * M_PI * 300 * t) + 3000 * std::sin(2 * M_PI * 1100 * t)));
  }
  AdpcmState enc;
  for (size_t f = 0; f < in.size() / kFrameSamples; f++) {
    Frame frame{}; encodeFrame(enc, &in[f * kFrameSamples], frame); decodeFrame(frame, &out[f * kFrameSamples]);
  }
  const double snr = snrDb(in, out);
  printf("codec: ADPCM round trip SNR %.1f dB, %zu bytes a 30 ms frame (raw PCM: %zu)\n", snr, sizeof(Frame), kFrameSamples * 2);
  assert(snr > 20);
  assert(sizeof(Frame) <= 142);  // The ESP-NOW packet's payload.
}

static void testJitter() {
  using namespace voice;
  std::vector<int16_t> pcm(kFrameSamples * 30);
  for (size_t i = 0; i < pcm.size(); i++) pcm[i] = int16_t(8000 * std::sin(2 * M_PI * 440 * double(i) / kSampleRate));
  std::vector<Frame> frames(30); AdpcmState enc;
  for (size_t f = 0; f < frames.size(); f++) { frames[f].sequence = uint16_t(f + 65530); frames[f].talk = 7; frames[f].flags = f + 1 == frames.size(); encodeFrame(enc, &pcm[f * kFrameSamples], frames[f]); }

  // In order, every packet twice (as sent), with frame 10 lost both times and 14/15 swapped.
  JitterBuffer jb; int16_t outFrame[kFrameSamples]; uint32_t now = 0; size_t pulled = 0;
  std::vector<size_t> order;
  for (size_t f = 0; f < frames.size(); f++) order.push_back(f == 14 ? 15 : f == 15 ? 14 : f);
  for (size_t k = 0; k < order.size(); k++) {
    const size_t f = order[k];
    if (f != 10) { jb.push(frames[f], now); jb.push(frames[f], now); }
    now += 30;
    if (jb.pull(outFrame, now)) pulled++;  // The speaker takes one 30 ms frame every 30 ms.
  }
  for (int i = 0; i < 40 && jb.active(); i++) { now += 30; jb.pull(outFrame, now); }
  (void)pulled;
  printf("jitter: received %u, duplicates %u, late %u, concealed %u, played %u\n", jb.stats.received, jb.stats.duplicates, jb.stats.late, jb.stats.concealed, jb.stats.played);
  assert(jb.stats.received == 29 && jb.stats.duplicates == 29 && jb.stats.played == 29 && jb.stats.concealed == 1);
  assert(!jb.active());  // The last frame ended the talk.
  // A copy of that talk arriving late doesn't start it again.
  assert(!jb.push(frames[29], now + 10));

  // Waits 90 ms (three frames) before the first frame plays.
  JitterBuffer start; Frame a = frames[0]; a.talk = 9;
  start.push(a, 0); assert(!start.pull(outFrame, 10));
  a.sequence++; start.push(a, 30); assert(!start.pull(outFrame, 40));
  a.sequence++; start.push(a, 60); assert(start.pull(outFrame, 70));

  // Concealment fades: half, quarter, then silence; and stops after 400 ms with nothing arriving.
  JitterBuffer gap; Frame b = frames[3]; b.talk = 11; b.flags = 0;
  for (int i = 0; i < 3; i++) { gap.push(b, 0); b.sequence++; }
  for (int i = 0; i < 3; i++) assert(gap.pull(outFrame, 0));
  int peaks[3];
  for (int i = 0; i < 3; i++) { assert(gap.pull(outFrame, 100)); int p = 0; for (auto s : outFrame) p = std::max(p, std::abs(int(s))); peaks[i] = p; }
  assert(peaks[1] <= peaks[0] / 2 + 1 && peaks[2] == 0);
  assert(!gap.pull(outFrame, 600) && !gap.active());

  // Somebody else talking over an active talk is ignored until it ends.
  JitterBuffer busy; Frame c = frames[0]; c.talk = 1; busy.push(c, 0);
  Frame d = frames[0]; d.talk = 2; assert(!busy.push(d, 100)); assert(busy.push(d, 600));
}

static void testAgc() {
  using namespace voice;
  // A quiet voice is raised over time; a loud one is brought under the limit at once.
  Agc agc; std::vector<int16_t> block(160);
  for (int n = 0; n < 400; n++) {
    for (size_t i = 0; i < block.size(); i++) block[i] = int16_t(1500 * std::sin(2 * M_PI * 300 * double(n * 160 + i) / kSampleRate));
    agc.process(block.data(), block.size());
  }
  int quietPeak = 0; for (auto s : block) quietPeak = std::max(quietPeak, std::abs(int(s)));
  for (size_t i = 0; i < block.size(); i++) block[i] = int16_t(32000 * std::sin(2 * M_PI * 300 * double(i) / kSampleRate));
  agc.process(block.data(), block.size());
  int loudPeak = 0; for (auto s : block) loudPeak = std::max(loudPeak, std::abs(int(s)));
  printf("agc: quiet 1500 peak raised to %d; loud 32000 peak held at %d\n", quietPeak, loudPeak);
  assert(quietPeak > 6000 && loudPeak <= 30000);
}

int main() {
  testOutbox(); testCodec(); testJitter(); testAgc();
  puts("messaging-test: all passed");
}
