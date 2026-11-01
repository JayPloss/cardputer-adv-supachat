// The power lab: a separate firmware, never part of a normal build. It works through a series
// of loads with rests between, logging battery voltage to the card fast enough to see each
// load's sag step and the recovery after it, then cycles until the battery is flat. Survives
// a brownout: rows reach the card in small batches, and the next boot logs why it reset.
//
// The device has no current sensor. Currents are inferred later, by tools/fit-power.py in the
// emulator repo, from discharge rate against the rated capacity and sag against resistance.
#include <Arduino.h>
#include <HTTPClient.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <libhelix-mp3/mp3dec.h>
#include <soc/usb_serial_jtag_struct.h>

#if __has_include("music_secrets.h")
#include "music_secrets.h"
#endif

namespace {
constexpr int kBatteryPin = 10;        // M5Unified's ADC1 pin for the Cardputer ADV; divider 2:1.
constexpr uint32_t kRowMs = 100;       // One row: min, mean and max of the fast samples in it.
constexpr uint32_t kFlushMs = 1000;    // At most a second of rows lost to a brownout.
constexpr uint32_t kActionMs = 20000;
constexpr uint32_t kRestMs = 30000;    // Long enough to fit the recovery's time constant.
constexpr uint32_t kLongIdleMs = 30 * 60 * 1000;
constexpr uint32_t kUsbCheckMs = 5000;
constexpr int kChargeRiseMv = 25;      // Rising at rest this much means a charger is on it.
constexpr const char *kRunPath = "/powerlab/run.csv";
constexpr const char *kBootsPath = "/powerlab/boots.csv";

File logFile;
volatile const char *action = "boot";
volatile int vMin = 100000, vMax = 0, vCount = 0;
volatile int64_t vSum = 0;
portMUX_TYPE sampleLock = portMUX_INITIALIZER_UNLOCKED;
uint32_t cycle = 0, lastRowAt = 0, lastFlushAt = 0, lastUsbAt = 0;
bool sdReady = false;
uint8_t scratch[8192];
int16_t pcm[2304];
RTC_NOINIT_ATTR uint32_t bootCounter;

// The fast sampler, on its own task: analogReadMilliVolts takes tens of microseconds, and this
// runs at about 1 kHz. Whether a peak is visible depends on the board's filtering, which is unknown.
void samplerTask(void *) {
  for (;;) {
    const int mv = analogReadMilliVolts(kBatteryPin) * 2;
    portENTER_CRITICAL(&sampleLock);
    if (mv < vMin) vMin = mv;
    if (mv > vMax) vMax = mv;
    vSum = vSum + mv; vCount = vCount + 1;
    portEXIT_CRITICAL(&sampleLock);
    vTaskDelay(1);
  }
}

// USB's start-of-frame counter only moves with a host on the line.
bool usbHostPresent() {
  const uint32_t first = USB_SERIAL_JTAG.fram_num.sof_frame_index;
  delay(5);
  return USB_SERIAL_JTAG.fram_num.sof_frame_index != first;
}

void show(const char *line, uint16_t colour = TFT_WHITE) {
  auto &d = M5Cardputer.Display;
  d.fillRect(0, 40, 240, 50, TFT_BLACK);
  d.setTextColor(colour, TFT_BLACK); d.setCursor(6, 44); d.setTextSize(2); d.print(line); d.setTextSize(1);
}

void refuseOnUsb() {
  if (!usbHostPresent()) return;
  show("ON USB POWER", TFT_RED);
  M5Cardputer.Display.setCursor(6, 70); M5Cardputer.Display.print("USB hides the battery. Unplug it to run.");
  while (usbHostPresent()) delay(500);
  ESP.restart();
}

void logRow() {
  portENTER_CRITICAL(&sampleLock);
  const int low = vMin, high = vMax, count = vCount;
  const int64_t sum = vSum;
  vMin = 100000; vMax = 0; vSum = 0; vCount = 0;
  portEXIT_CRITICAL(&sampleLock);
  if (!count || !logFile) return;
  logFile.printf("%lu,%lu,%s,%d,%d,%d,%d,%u\n", millis(), static_cast<unsigned long>(cycle), const_cast<const char *>(action), low, static_cast<int>(sum / count), high, count, ESP.getFreeHeap());
}

void service() {
  const uint32_t now = millis();
  if (now - lastRowAt >= kRowMs) { lastRowAt = now; logRow(); }
  if (now - lastFlushAt >= kFlushMs) { lastFlushAt = now; if (logFile) logFile.flush(); }
  if (now - lastUsbAt >= kUsbCheckMs) { lastUsbAt = now; if (usbHostPresent()) { if (logFile) logFile.close(); refuseOnUsb(); } }
}

void runFor(const char *label, uint32_t ms, void (*work)() = nullptr) {
  action = label; show(label);
  const uint32_t started = millis();
  while (millis() - started < ms) {
    if (work) work(); else delay(5);
    service();
  }
  if (logFile) logFile.flush();
}

void rest() { runFor("rest", kRestMs); }

// ---- the loads ----

void workCpu() {
  volatile uint32_t value = 1;
  for (int i = 0; i < 200000; i++) value = value * 31 + i;
}

void joinWifi() {
#ifdef SUPACHAT_HOME_SSID
  WiFi.mode(WIFI_STA); WiFi.begin(SUPACHAT_HOME_SSID, SUPACHAT_HOME_PSK);
  for (int i = 0; i < 200 && WiFi.status() != WL_CONNECTED; i++) { delay(50); service(); }
#endif
}

void workScan() { WiFi.scanNetworks(false, true); WiFi.scanDelete(); }

void workDownload() {
#ifdef SUPACHAT_MUSIC_HOST
  static HTTPClient http;
  http.useHTTP10(true); http.setTimeout(5000);
  http.begin(String("http://") + SUPACHAT_MUSIC_HOST + "/app/");
  if (http.GET() > 0) {
    WiFiClient *stream = http.getStreamPtr();
    const uint32_t started = millis();
    while (millis() - started < 5000 && stream->connected()) { stream->read(scratch, sizeof(scratch)); service(); }
  }
  http.end();
#else
  delay(50);
#endif
}

void workTls() {
  WiFiClientSecure client; client.setInsecure(); client.setHandshakeTimeout(15);
  client.connect("supachat.net", 443);
  client.stop();
  service();
}

void workEspNow() {
  static uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  static uint8_t packet[200];
  esp_now_send(broadcast, packet, sizeof(packet));
  delay(2);
}

void workSdWrite() {
  File file = SD.open("/powerlab/scratch.bin", FILE_WRITE);
  if (file) { for (int i = 0; i < 20; i++) { file.write(scratch, sizeof(scratch)); service(); } file.close(); }
}

void workSdRead() {
  File file = SD.open("/powerlab/scratch.bin", FILE_READ);
  if (file) { while (file.read(scratch, sizeof(scratch)) > 0) service(); file.close(); }
}

// Plays the first MP3 of a folder through the same decoder and speaker path as the player.
HMP3Decoder decoder = nullptr;
File playing;
bool audioOut = true;
int filled = 0;
String firstMp3(const char *folder) {
  File root = SD.open(folder);
  if (!root) return "";
  for (File child = root.openNextFile(); child; child = root.openNextFile()) {
    if (child.isDirectory()) { String inner = firstMp3((String(folder) + "/" + child.name()).c_str()); if (inner.length()) return inner; }
    else if (String(child.name()).endsWith(".mp3")) return String(folder) + "/" + child.name();
  }
  return "";
}
void startFile(const String &path) { if (playing) playing.close(); playing = SD.open(path, FILE_READ); filled = 0; }
void workDecode() {
  if (!playing) { delay(10); return; }
  if (filled < 2048) { const int got = playing.read(scratch + filled, sizeof(scratch) - filled); if (got <= 0) { playing.seek(0); return; } filled += got; }
  const int sync = MP3FindSyncWord(scratch, filled);
  if (sync < 0) { filled = 0; return; }
  unsigned char *read = scratch + sync;
  int left = filled - sync;
  if (MP3Decode(decoder, &read, &left, pcm, 0) == ERR_MP3_NONE) {
    if (audioOut) {
      MP3FrameInfo info; MP3GetLastFrameInfo(decoder, &info);
      while (M5Cardputer.Speaker.isPlaying(1) >= 2) delay(1);
      M5Cardputer.Speaker.playRaw(pcm, info.outputSamps, info.samprate, info.nChans == 2, 1, 1, false);
    }
    memmove(scratch, read, left); filled = left;
  } else { memmove(scratch, scratch + sync + 1, filled - sync - 1); filled -= sync + 1; }
}

void workTone() { if (!M5Cardputer.Speaker.isPlaying(0)) M5Cardputer.Speaker.tone(1000, 200, 0); delay(5); }

void oneCycle() {
  auto &display = M5Cardputer.Display;
  static const uint8_t brightness[] = {0, 64, 128, 192, 255};
  static const char *const brightnessName[] = {"idle_b0", "idle_b64", "idle_b128", "idle_b192", "idle_b255"};
  WiFi.mode(WIFI_OFF);
  // Once, at the start: a long idle's voltage slope against the whole run's gives what a rest draws.
  if (cycle == 0) { display.setBrightness(64); runFor("long_idle", kLongIdleMs); rest(); }
  for (int i = 0; i < 5; i++) { display.setBrightness(brightness[i]); runFor(brightnessName[i], kActionMs); rest(); }
  display.setBrightness(64);
  runFor("cpu_load", kActionMs, workCpu); rest();
  WiFi.mode(WIFI_STA);
  runFor("wifi_associate", 2000, joinWifi); runFor("wifi_idle", kActionMs); rest();
  runFor("wifi_scan", kActionMs, workScan); rest();
  if (WiFi.status() != WL_CONNECTED) joinWifi();
  runFor("download", kActionMs, workDownload); rest();
  runFor("tls_handshake", kActionMs, workTls); rest();
  if (esp_now_init() == ESP_OK) {
    esp_now_peer_info_t peer{}; memset(peer.peer_addr, 0xff, 6); peer.ifidx = WIFI_IF_STA;
    esp_now_add_peer(&peer);
    runFor("espnow_tx", kActionMs, workEspNow); rest();
    esp_now_deinit();
  }
  WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF);
  runFor("sd_write", kActionMs, workSdWrite); rest();
  runFor("sd_read", kActionMs, workSdRead); rest();
  const String book = firstMp3("/books"), song = firstMp3("/music");
  audioOut = false; startFile(book); runFor("mp3_decode", kActionMs, workDecode); audioOut = true; rest();
  static const uint8_t volumes[] = {64, 128, 192, 255};
  static const char *const toneName[] = {"spk_tone_64", "spk_tone_128", "spk_tone_192", "spk_tone_255"};
  static const char *const speechName[] = {"spk_speech_64", "spk_speech_128", "spk_speech_192", "spk_speech_255"};
  static const char *const musicName[] = {"spk_music_64", "spk_music_128", "spk_music_192", "spk_music_255"};
  for (int i = 0; i < 4; i++) {
    M5Cardputer.Speaker.setVolume(volumes[i]);
    runFor(toneName[i], kActionMs, workTone); M5Cardputer.Speaker.stop(); rest();
    startFile(book); runFor(speechName[i], kActionMs, workDecode); M5Cardputer.Speaker.stop(); rest();
    if (song.length()) { startFile(song); runFor(musicName[i], kActionMs, workDecode); M5Cardputer.Speaker.stop(); rest(); }
  }
  // The real cases: card playback with Wi-Fi off, then on.
  M5Cardputer.Speaker.setVolume(192);
  startFile(book); runFor("play_card_wifi_off", kActionMs * 3, workDecode); M5Cardputer.Speaker.stop(); rest();
  WiFi.mode(WIFI_STA); joinWifi();
  startFile(book); runFor("play_card_wifi_on", kActionMs * 3, workDecode); M5Cardputer.Speaker.stop(); rest();
  if (playing) playing.close();
}
}  // namespace

void setup() {
  auto config = M5.config(); config.output_power = true;
  M5Cardputer.begin(config, true); M5Cardputer.Display.setRotation(1);
  const esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_POWERON) bootCounter = 0; else bootCounter++;
  M5Cardputer.Display.fillScreen(TFT_BLACK); M5Cardputer.Display.setCursor(6, 6); M5Cardputer.Display.print("POWER LAB");
  refuseOnUsb();
  SPI.begin(GPIO_NUM_40, GPIO_NUM_39, GPIO_NUM_14, GPIO_NUM_12);
  sdReady = SD.begin(GPIO_NUM_12, SPI, 25000000);
  if (!sdReady) { show("NO CARD", TFT_RED); for (;;) delay(1000); }
  SD.mkdir("/powerlab");
  File boots = SD.open(kBootsPath, FILE_APPEND);
  if (boots) { boots.printf("%lu,%d,%d\n", millis(), static_cast<int>(reason), analogReadMilliVolts(kBatteryPin) * 2); boots.close(); }
  const bool fresh = !SD.exists(kRunPath);
  logFile = SD.open(kRunPath, FILE_APPEND);
  if (fresh && logFile) logFile.println("millis,cycle,action,mv_min,mv_mean,mv_max,samples,free_heap");
  decoder = MP3InitDecoder();
  analogReadResolution(12);
  xTaskCreatePinnedToCore(samplerTask, "sampler", 3072, nullptr, 5, nullptr, 1);
}

void loop() {
  oneCycle();
  cycle++;
  M5Cardputer.update();
}
