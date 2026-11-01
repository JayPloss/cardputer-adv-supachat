#include "media_player.h"

#include <HTTPClient.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <libhelix-mp3/mp3dec.h>

#include <algorithm>

namespace media {
namespace {
// Chat voice uses channel 0, and so do the key and notification tones.
constexpr uint8_t kSpeakerChannel = 1;
constexpr size_t kInputBytes = 24 * 1024;
constexpr size_t kPrebufferBytes = 12 * 1024;
constexpr size_t kPcmBuffers = 3;
constexpr size_t kPcmSamples = 1152 * 2;
constexpr uint32_t kStallTimeoutMs = 15000;
constexpr uint32_t kConnectTimeoutMs = 8000;

struct Command { String url; uint32_t startSeconds = 0; };

SemaphoreHandle_t commandMutex = nullptr;
TaskHandle_t taskHandle = nullptr;
Command pending;
volatile bool hasPending = false;
volatile bool stopRequested = false;
volatile bool pauseRequested = false;
volatile State currentState = State::Idle;
volatile uint32_t startPosition = 0;
volatile uint64_t samplesPlayed = 0;
volatile uint32_t sampleRate = 0;
volatile uint32_t lowestFreeHeap = UINT32_MAX;
volatile uint32_t underrunCount = 0;
const char *volatile failureText = "";

void fail(const char *reason) { failureText = reason; currentState = State::Failed; }

bool interrupted() { return stopRequested || hasPending; }

void streamOnce(const String &url) {
  uint8_t *input = static_cast<uint8_t *>(heap_caps_malloc(kInputBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  int16_t *pcm = static_cast<int16_t *>(heap_caps_malloc(kPcmBuffers * kPcmSamples * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  HMP3Decoder decoder = MP3InitDecoder();
  if (!input || !pcm || !decoder) {
    fail("NO MEMORY");
    if (decoder) MP3FreeDecoder(decoder);
    free(input); free(pcm);
    return;
  }

  HTTPClient http;
  // HTTP/1.0: no chunk markers inside the MP3 data, and the server closes at the end.
  http.useHTTP10(true);
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kStallTimeoutMs);
  http.begin(url);
  const int status = http.GET();
  if (status != HTTP_CODE_OK && status != HTTP_CODE_PARTIAL_CONTENT) {
    fail(status < 0 ? "NO CONNECTION" : "SERVER ERROR");
  } else {
    WiFi.setSleep(false);  // Modem sleep starves a continuous stream.
    WiFiClient *stream = http.getStreamPtr();
    size_t filled = 0;
    size_t pcmIndex = 0;
    uint32_t lastDataAt = millis();
    bool started = false;
    bool ended = false;
    while (!interrupted()) {
      const uint32_t freeHeap = ESP.getFreeHeap();
      if (freeHeap < lowestFreeHeap) lowestFreeHeap = freeHeap;
      if (pauseRequested) { pauseRequested = false; currentState = currentState == State::Paused ? State::Playing : State::Paused; }

      const int available = stream->available();
      if (available > 0 && filled < kInputBytes) {
        const int got = stream->read(input + filled, min<size_t>(available, kInputBytes - filled));
        if (got > 0) { filled += got; lastDataAt = millis(); }
      } else if (available <= 0 && !stream->connected()) {
        ended = true;
      } else if (available <= 0 && millis() - lastDataAt > kStallTimeoutMs) {
        fail("STREAM STALLED");
        break;
      }

      if (!started) {
        if (filled < kPrebufferBytes && !ended) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        started = true;
        currentState = State::Playing;
      }
      if (currentState == State::Paused) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
      // Both queue slots full means roughly 50 ms or more of audio is waiting.
      if (M5Cardputer.Speaker.isPlaying(kSpeakerChannel) >= 2) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }

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

      unsigned char *read = input;
      int left = filled;
      int16_t *frame = pcm + pcmIndex * kPcmSamples;
      const int result = MP3Decode(decoder, &read, &left, frame, 0);
      if (result == ERR_MP3_INDATA_UNDERFLOW || result == ERR_MP3_MAINDATA_UNDERFLOW) {
        // MAINDATA underflow is normal for the first frames after joining mid-stream.
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
      sampleRate = info.samprate;
      if (M5Cardputer.Speaker.playRaw(frame, info.outputSamps, info.samprate, info.nChans == 2, 1, kSpeakerChannel, false)) {
        pcmIndex = (pcmIndex + 1) % kPcmBuffers;
        samplesPlayed = samplesPlayed + info.outputSamps / info.nChans;
      }
    }
    if (ended && !interrupted()) {
      while (M5Cardputer.Speaker.isPlaying(kSpeakerChannel) > 0 && !interrupted()) vTaskDelay(pdMS_TO_TICKS(20));
      if (currentState != State::Failed) currentState = State::Ended;
    }
    WiFi.setSleep(true);
  }
  http.end();
  // The speaker reads pcm directly, so stop it before that memory is released.
  M5Cardputer.Speaker.stop(kSpeakerChannel);
  while (M5Cardputer.Speaker.isPlaying(kSpeakerChannel) > 0) vTaskDelay(pdMS_TO_TICKS(1));
  MP3FreeDecoder(decoder);
  free(input); free(pcm);
}

void playerTask(void *) {
  for (;;) {
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
    failureText = "";
    currentState = State::Connecting;
    if (WiFi.status() != WL_CONNECTED) { fail("NO WIFI"); continue; }
    streamOnce(command.url);
  }
}
}  // namespace

void begin() {
  if (taskHandle) return;
  commandMutex = xSemaphoreCreateMutex();
  // Core 0 is shared with the Wi-Fi stack; this outranks chat sync so a TLS
  // handshake there cannot hold up decoding.
  xTaskCreatePinnedToCore(playerTask, "media", 8192, nullptr, 3, &taskHandle, 0);
}

void play(const String &url, uint32_t startSeconds) {
  xSemaphoreTake(commandMutex, portMAX_DELAY);
  pending.url = url;
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

const char *failure() { return failureText; }
uint32_t lowestHeap() { return lowestFreeHeap; }
uint32_t underruns() { return underrunCount; }

}  // namespace media
