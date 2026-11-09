#include "media_player.h"
#include "os.h"

#include <HTTPClient.h>
#include <SD.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <libhelix-mp3/mp3dec.h>

#include <algorithm>

// The MP3 library's own allocator spins forever when RAM runs out, which the watchdog turns
// into a restart. Its decoder already handles a NULL, so it gets a plain allocation instead.
extern "C" void *__wrap_helix_malloc(int size) { return calloc(1, size ? size : 1); }

namespace media {
namespace {
// Chat voice uses channel 0, and so do the key and notification tones.
constexpr uint8_t kSpeakerChannel = 1;
// The card never stalls the way Wi-Fi does, so it needs far less held in RAM.
constexpr size_t kStreamInputBytes = 16 * 1024;
constexpr size_t kCardInputBytes = 6 * 1024;
// Chat's last connection can hold RAM for a while, and this runs behind a BUFFERING screen.
constexpr uint8_t kAllocationTries = 20;
constexpr size_t kFileChunkBytes = 2048;
constexpr size_t kPcmBuffers = 3;
constexpr size_t kMaxFrameSamples = 1152 * 2;
constexpr size_t kPcmSamples = kMaxFrameSamples;
constexpr uint32_t kStallTimeoutMs = 15000;
constexpr uint32_t kConnectTimeoutMs = 8000;

struct Command { String url; String path; uint32_t startSeconds = 0; };

SemaphoreHandle_t commandMutex = nullptr;
TaskHandle_t taskHandle = nullptr;
volatile bool endRequested = false;
Command pending;
volatile bool hasPending = false;
volatile bool stopRequested = false;
volatile bool pauseRequested = false;
volatile State currentState = State::Idle;
volatile uint32_t startPosition = 0;
volatile uint64_t samplesPlayed = 0;
volatile uint32_t sampleRate = 0;
volatile uint32_t totalSeconds = 0;
volatile uint32_t lowestFreeHeap = UINT32_MAX;
volatile uint32_t underrunCount = 0;
const char *volatile failureText = "";

char memoryFailure[32];

void fail(const char *reason) { failureText = reason; currentState = State::Failed; }

bool interrupted() { return stopRequested || hasPending; }

// Reads the first frame header so a constant-bitrate file can be timed and seeked.
bool openAt(File &file, uint32_t seconds, uint8_t *scratch, HMP3Decoder decoder) {
  const int got = file.read(scratch, 4096);
  const int sync = got > 0 ? MP3FindSyncWord(scratch, got) : -1;
  MP3FrameInfo info;
  const bool found = sync >= 0 && MP3GetNextFrameInfo(decoder, &info, scratch + sync) == ERR_MP3_NONE && info.bitrate > 0;
  if (found) {
    totalSeconds = static_cast<uint64_t>(file.size()) * 8 / info.bitrate;
    file.seek(sync + static_cast<uint64_t>(seconds) * info.bitrate / 8);
  }
  return found;
}

void playSource(const Command &command) {
  const bool local = command.path.length() > 0;
  const size_t kInputBytes = local ? kCardInputBytes : kStreamInputBytes;
  const size_t kPrebufferBytes = kInputBytes / 2;
  uint8_t *input = nullptr;
  int16_t *pcm = nullptr;
  HMP3Decoder decoder = nullptr;
  // Chat's TLS work holds a lot of RAM for a few seconds at a time, so wait it out.
  for (uint8_t attempt = 0; attempt < kAllocationTries && !interrupted(); attempt++) {
    // Plain malloc, so the ledger charges these to the app that is playing. No PSRAM here: it is internal.
    input = static_cast<uint8_t *>(malloc(kInputBytes));
    pcm = static_cast<int16_t *>(malloc(kPcmBuffers * kPcmSamples * sizeof(int16_t)));
    decoder = MP3InitDecoder();
    if (input && pcm && decoder) break;
    if (decoder) MP3FreeDecoder(decoder);
    free(input); free(pcm);
    input = nullptr; pcm = nullptr; decoder = nullptr;
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  if (!input || !pcm || !decoder) {
    if (!interrupted()) {
      snprintf(memoryFailure, sizeof(memoryFailure), "NO MEMORY: %uK FREE", static_cast<unsigned>(ESP.getFreeHeap() / 1024));
      fail(memoryFailure);
    }
    return;
  }

  File file;
  HTTPClient http;
  WiFiClient *stream = nullptr;
  if (local) {
    file = SD.open(command.path, FILE_READ);
    if (!file) fail("FILE NOT FOUND");
    else if (!openAt(file, command.startSeconds, input, decoder)) fail("NOT AN MP3");
  } else {
    // HTTP/1.0: no chunk markers inside the MP3 data, and the server closes at the end.
    http.useHTTP10(true);
    http.setConnectTimeout(kConnectTimeoutMs);
    http.setTimeout(kStallTimeoutMs);
    http.begin(command.url);
    const int status = http.GET();
    if (status == HTTP_CODE_OK || status == HTTP_CODE_PARTIAL_CONTENT) {
      WiFi.setSleep(false);  // Modem sleep starves a continuous stream.
      stream = http.getStreamPtr();
    } else fail(status < 0 ? "NO CONNECTION" : "SERVER ERROR");
  }

  if (currentState != State::Failed) {
    size_t filled = 0;
    size_t pcmIndex = 0;
    size_t pcmFill = 0;
    int slotRate = 0;
    bool slotStereo = false;
    uint32_t lastDataAt = millis();
    bool started = false;
    bool ended = false;
    // Hands the part-filled slot to the speaker. Slots hold several frames, so a
    // flash write or TLS handshake that stalls this task doesn't reach the listener.
    const auto submit = [&]() {
      while (!interrupted() && M5Cardputer.Speaker.isPlaying(kSpeakerChannel) >= 2) vTaskDelay(pdMS_TO_TICKS(5));
      if (!pcmFill || interrupted()) return;
      M5Cardputer.Speaker.playRaw(pcm + pcmIndex * kPcmSamples, pcmFill, slotRate, slotStereo, 1, kSpeakerChannel, false);
      samplesPlayed = samplesPlayed + pcmFill / (slotStereo ? 2 : 1);
      pcmIndex = (pcmIndex + 1) % kPcmBuffers;
      pcmFill = 0;
    };
    while (!interrupted()) {
      const uint32_t freeHeap = ESP.getFreeHeap();
      if (freeHeap < lowestFreeHeap) lowestFreeHeap = freeHeap;
      if (pauseRequested) { pauseRequested = false; currentState = currentState == State::Paused ? State::Playing : State::Paused; }

      if (local) {
        if (filled < kInputBytes && !ended) {
          const int got = file.read(input + filled, std::min<size_t>(kFileChunkBytes, kInputBytes - filled));
          if (got > 0) filled += got; else ended = true;
        }
      } else {
        const int available = stream->available();
        if (available > 0 && filled < kInputBytes) {
          const int got = stream->read(input + filled, std::min<size_t>(available, kInputBytes - filled));
          if (got > 0) { filled += got; lastDataAt = millis(); }
        } else if (available <= 0 && !stream->connected()) {
          ended = true;
        } else if (available <= 0 && millis() - lastDataAt > kStallTimeoutMs) {
          fail("STREAM STALLED");
          break;
        }
      }

      if (!started) {
        if (filled < kPrebufferBytes && !ended) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        started = true;
        currentState = State::Playing;
      }
      if (currentState == State::Paused) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }

      const int sync = filled ? MP3FindSyncWord(input, filled) : -1;
      if (sync < 0) {
        filled = 0;
        if (ended) break;
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }
      if (sync > 0) { memmove(input, input + sync, filled - sync); filled -= sync; }
      // A frame split across the buffer's end waits for more data, unless the stream is over.
      if (filled < 1500 && !ended) {
        if (M5Cardputer.Speaker.isPlaying(kSpeakerChannel) == 0 && samplesPlayed) underrunCount = underrunCount + 1;
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }

      if (pcmFill + kMaxFrameSamples > kPcmSamples) { submit(); continue; }
      unsigned char *read = input;
      int left = filled;
      const int result = MP3Decode(decoder, &read, &left, pcm + pcmIndex * kPcmSamples + pcmFill, 0);
      if (result == ERR_MP3_INDATA_UNDERFLOW || result == ERR_MP3_MAINDATA_UNDERFLOW) {
        // MAINDATA underflow is normal for the first frames after a seek.
        if (left > 0 && left != static_cast<int>(filled)) { memmove(input, read, left); filled = left; }
        else if (ended) break;
        else { memmove(input, input + 1, filled - 1); filled--; }
        continue;
      }
      if (result != ERR_MP3_NONE) {
        // Skip the bad sync byte and look for the next frame.
        memmove(input, input + 1, filled - 1);
        filled--;
        continue;
      }
      memmove(input, read, left);
      filled = left;

      MP3FrameInfo info;
      MP3GetLastFrameInfo(decoder, &info);
      if (info.outputSamps <= 0 || info.samprate <= 0) continue;
      const bool stereo = info.nChans == 2;
      if (pcmFill && (info.samprate != slotRate || stereo != slotStereo)) {
        // Rare: the format changed mid-file. This frame is dropped, not the rest.
        submit();
        continue;
      }
      slotRate = info.samprate;
      slotStereo = stereo;
      sampleRate = info.samprate;
      pcmFill += info.outputSamps;
    }
    if (ended && !interrupted()) {
      submit();
      while (M5Cardputer.Speaker.isPlaying(kSpeakerChannel) > 0 && !interrupted()) vTaskDelay(pdMS_TO_TICKS(20));
      if (currentState != State::Failed) currentState = State::Ended;
    }
  }
  if (stream) WiFi.setSleep(true);
  if (local) { if (file) file.close(); } else http.end();
  // The speaker reads pcm directly, so stop it before that memory is released.
  M5Cardputer.Speaker.stop(kSpeakerChannel);
  while (M5Cardputer.Speaker.isPlaying(kSpeakerChannel) > 0) vTaskDelay(pdMS_TO_TICKS(1));
  MP3FreeDecoder(decoder);
  free(input); free(pcm);
}

void playerTask(void *) {
  os::bindTask(os::Owner::Music);
  for (;;) {
    if (endRequested) {
      // The main loop asks for the end whenever nothing plays, so a play can arrive just after it:
      // a waiting command wins, and the handle goes under the same lock begin() reads it with.
      xSemaphoreTake(commandMutex, portMAX_DELAY);
      const bool leave = endRequested && !hasPending;
      endRequested = false;
      if (leave) taskHandle = nullptr;
      xSemaphoreGive(commandMutex);
      if (leave) vTaskDelete(nullptr);
    }
    if (!hasPending) {
      if (stopRequested) {
        stopRequested = false;
        pauseRequested = false;
        currentState = State::Idle;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    xSemaphoreTake(commandMutex, portMAX_DELAY);
    const Command command = pending;
    hasPending = false;
    stopRequested = false;
    pauseRequested = false;
    xSemaphoreGive(commandMutex);
    startPosition = command.startSeconds;
    samplesPlayed = 0;
    sampleRate = 0;
    totalSeconds = 0;
    failureText = "";
    currentState = State::Connecting;
    if (command.path.isEmpty() && WiFi.status() != WL_CONNECTED) { fail("NO WIFI"); continue; }
    try {
      playSource(command);
    } catch (const std::bad_alloc &) {
      os::recordFault(os::mediaOwner(), "player out of memory");
      fail("OUT OF MEMORY");
    }
    // Everything the player held is back; a free that slipped past the ledger must not refuse the next book.
    os::reconcile(os::mediaOwner());
  }
}
}  // namespace

bool begin() {
  if (!commandMutex) commandMutex = xSemaphoreCreateMutex();
  xSemaphoreTake(commandMutex, portMAX_DELAY);
  bool running = true;
  // Wanted again before it had ended: it just carries on.
  if (taskHandle) endRequested = false;
  // Core 0 is shared with the Wi-Fi stack; this outranks chat sync so a TLS
  // handshake there cannot hold up decoding.
  else running = xTaskCreatePinnedToCore(playerTask, "media", 8192, nullptr, 3, &taskHandle, 0) == pdPASS;
  xSemaphoreGive(commandMutex);
  return running;
}

void end() {
  if (!commandMutex) return;
  xSemaphoreTake(commandMutex, portMAX_DELAY);
  if (taskHandle) endRequested = true;
  xSemaphoreGive(commandMutex);
}

void play(const String &url, uint32_t startSeconds) {
  xSemaphoreTake(commandMutex, portMAX_DELAY);
  pending.url = url;
  pending.path = "";
  pending.startSeconds = startSeconds;
  hasPending = true;
  xSemaphoreGive(commandMutex);
  currentState = State::Connecting;
}

void playFile(const String &path, uint32_t startSeconds) {
  xSemaphoreTake(commandMutex, portMAX_DELAY);
  pending.url = "";
  pending.path = path;
  pending.startSeconds = startSeconds;
  hasPending = true;
  xSemaphoreGive(commandMutex);
  currentState = State::Connecting;
}

void togglePause() {
  if (currentState == State::Playing || currentState == State::Paused) pauseRequested = true;
}

void stop() { stopRequested = true; }

State state() { return currentState; }

bool active() {
  const State value = currentState;
  return value == State::Connecting || value == State::Playing || value == State::Paused;
}

uint32_t positionSeconds() {
  const uint32_t rate = sampleRate;
  return startPosition + (rate ? static_cast<uint32_t>(samplesPlayed / rate) : 0);
}

uint32_t durationSeconds() { return totalSeconds; }
const char *failure() { return failureText; }
uint32_t lowestHeap() { return lowestFreeHeap; }
uint32_t underruns() { return underrunCount; }

}  // namespace media
