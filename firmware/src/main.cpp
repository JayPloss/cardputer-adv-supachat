#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <WebSocketsClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <mbedtls/gcm.h>
#include <algorithm>
#include <array>
#include <deque>
#include <ctime>
#include <vector>

#include "keypress_song.h"
#include "splash_logo.h"

#ifndef SUPACHAT_DEVICE_NAME
#define SUPACHAT_DEVICE_NAME "Albie"
#endif
#ifndef SUPACHAT_DEVICE_ID
#define SUPACHAT_DEVICE_ID "albie"
#endif

namespace {
struct DuelToneStep { uint16_t frequency; uint16_t durationMs; uint16_t gapMs; };
enum class DuelSfxCue : uint8_t { None, Challenge, Countdown, Select, Lock, Impact, Victory, Defeat, Neutral };

constexpr char kApiBase[] = "https://supachat.net";
constexpr char kApiHost[] = "supachat.net";
// SHA-256 fingerprint for supachat.net's current leaf certificate.
// Renew this pin when Caddy rotates the certificate.
constexpr char kTlsFingerprint[] = "E7 09 64 D3 D6 B2 1D 03 F9 0E 81 59 6C FA 37 28 6C 69 37 5A AB C6 8A 0F DB C1 6D A0 87 9E 8F 15";
constexpr char kDeviceName[] = SUPACHAT_DEVICE_NAME;
constexpr char kDeviceId[] = SUPACHAT_DEVICE_ID;
constexpr char kFirmwareVersion[] = "v0.43";
constexpr size_t kMessageLimit = 140;
constexpr size_t kHistoryLimit = 100;
constexpr uint32_t kToneIntervalMs = 40;
constexpr uint32_t kToneDurationMs = 180;
constexpr uint8_t kDefaultVolumeLevel = 3;
constexpr uint8_t kVolumeValues[] = {0, 64, 128, 192, 255};
constexpr char const *kVolumeNames[] = {"MUTE", "LOW", "MEDIUM", "HIGH", "MAX"};
constexpr uint32_t kRenderIntervalMs = 50;
constexpr uint32_t kWifiRetryMs = 30000;
constexpr uint32_t kSyncPollMs = 3000;
constexpr uint32_t kBatterySampleMs = 1000;
constexpr uint32_t kBatteryRiseStepMs = 180000;
constexpr uint32_t kBatteryFastRiseStepMs = 10000;
constexpr uint32_t kPowerEdgeWindowMs = 6000;
constexpr int kPowerConnectRiseMv = 40;
constexpr int kPowerDisconnectFallMv = 30;
constexpr uint32_t kBatteryFallStepMs = 90000;
constexpr uint32_t kBatteryPersistMs = 300000;
constexpr int kSyncBatchLimit = 20;
constexpr uint32_t kTimeWaitMs = 12000;
constexpr uint32_t kVoiceSampleRate = 8000;
constexpr size_t kVoiceMaxSamples = 40000;
constexpr size_t kVoiceFallbackSamples = 8000;
constexpr size_t kVoiceCaptureBlock = 160;
constexpr size_t kVoicePlaybackBlock = 1024;
constexpr char kVoiceClipPath[] = "/supachat-voice-last.pcm";
constexpr uint32_t kWalkieMaxMs = 30000;
constexpr uint32_t kPttReleaseDebounceMs = 500;
constexpr uint32_t kEspNowBeaconMs = 30000;
constexpr bool kEspNowEnabled = false;
constexpr gpio_num_t kSdSckPin = GPIO_NUM_40;
constexpr gpio_num_t kSdMisoPin = GPIO_NUM_39;
constexpr gpio_num_t kSdMosiPin = GPIO_NUM_14;
constexpr gpio_num_t kSdCsPin = GPIO_NUM_12;
constexpr uint32_t kBootTuneStepMs = 250;
constexpr uint32_t kBootTuneNoteMs = 145;
constexpr uint16_t kBootTuneFrequencies[] = {
    196, 247, 294, 392, 0, 294, 330, 294,
    247, 196, 220, 247, 294, 0, 392, 370,
    330, 262, 330, 392, 494, 440, 392, 0,
    196, 294, 392, 494, 587, 523, 392, 294,
    196, 247, 294, 392, 440, 392, 330, 294,
    220, 262, 330, 440, 0, 392, 330, 294,
    247, 294, 370, 494, 440, 392, 370, 330,
    262, 330, 392, 523, 0, 494, 440, 392,
    294, 370, 440, 587, 523, 494, 440, 392,
    330, 392, 494, 659, 0, 587, 523, 494,
    392, 330, 294, 247, 220, 247, 294, 330,
    440, 392, 330, 294, 0, 247, 294, 392,
    196, 294, 330, 392, 494, 392, 330, 294,
    220, 330, 392, 440, 523, 440, 392, 330,
    247, 370, 440, 494, 587, 494, 440, 370,
    262, 392, 494, 523, 659, 587, 523, 494,
    294, 440, 523, 587, 0, 523, 494, 440,
    330, 494, 587, 659, 784, 659, 587, 523,
    392, 523, 659, 784, 0, 659, 523, 392,
    370, 494, 587, 740, 659, 587, 494, 440,
    330, 440, 523, 659, 587, 523, 440, 392,
    294, 392, 494, 587, 523, 494, 392, 330,
    262, 330, 440, 523, 494, 440, 392, 330,
    247, 294, 392, 494, 440, 392, 330, 294,
    220, 262, 330, 440, 392, 330, 294, 262,
    196, 247, 294, 392, 0, 494, 440, 392,
    330, 294, 247, 220, 196, 220, 247, 294,
    392, 370, 330, 294, 247, 294, 330, 370,
    392, 494, 587, 784, 659, 587, 494, 392,
};
constexpr size_t kBootTuneLength = sizeof(kBootTuneFrequencies) / sizeof(kBootTuneFrequencies[0]);
constexpr uint16_t kMessageNotificationFrequencies[] = {622, 831, 698};
constexpr uint16_t kMessageNotificationDurations[] = {90, 90, 180};
constexpr uint32_t kMessageNotificationGapMs = 70;
constexpr size_t kMessageNotificationLength = sizeof(kMessageNotificationFrequencies) / sizeof(kMessageNotificationFrequencies[0]);
constexpr uint32_t kDuelCountdownMs = 1200;
constexpr uint32_t kDuelRevealMs = 1200;
constexpr uint32_t kDuelAnimationFrameMs = 70;
constexpr DuelToneStep kDuelChallengeSfx[] = {{523,90,55},{659,120,0}};
constexpr DuelToneStep kDuelCountdownSfx[] = {{392,80,180},{392,80,180},{392,80,180},{784,180,0}};
constexpr DuelToneStep kDuelSelectSfx[] = {{587,70,0}};
constexpr DuelToneStep kDuelLockSfx[] = {{440,60,30},{880,100,0}};
constexpr DuelToneStep kDuelImpactSfx[] = {{988,70,20},{330,170,0}};
constexpr DuelToneStep kDuelVictorySfx[] = {{523,100,35},{659,100,35},{784,100,35},{1047,220,0}};
constexpr DuelToneStep kDuelDefeatSfx[] = {{494,130,40},{392,130,40},{294,220,0}};
constexpr DuelToneStep kDuelNeutralSfx[] = {{392,110,45},{349,170,0}};
// Retain the tested scheduling code, but do not suppress messaging until the
// family explicitly decides to re-enable quiet hours.
constexpr bool kBlackoutEnabled = false;

// Let's Encrypt ISRG Root X1. Certificate validation also requires valid NTP time.
const char kRootCa[] PROGMEM = R"EOF(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----)EOF";

struct WifiProfile { String ssid; String password; };
struct ScannedNetwork { String ssid; int32_t rssi; bool open; };
struct ChatMessage {
  int64_t id = 0;
  String roomId;
  String clientId;
  String authorId;
  String authorName;
  String body;
  String state = "queued";
  int64_t createdAt = 0;
  bool queued = false;
  bool voice = false;
  bool deleted = false;
  bool edited = false;
  int64_t replyToId = 0;
  String replyAuthor;
  String replyBody;
  int reactionCount = 0;
};

struct ChatLine {
  String text;
  uint32_t colour;
  bool mine;
  bool label;
};
struct ChatRoom { String id; String name; int64_t latestMessageId; int64_t seenMessageId; };
struct DuelOpponent { String id; String name; String status; };

uint32_t participantColour(const String &authorId, const String &authorName = "") {
  String identity = authorId + " " + authorName; identity.toLowerCase();
  if (identity.indexOf("albie") >= 0) return TFT_SKYBLUE;
  if (identity.indexOf("juju") >= 0 || identity.indexOf("julien") >= 0) return TFT_ORANGE;
  if (identity.indexOf("papa") >= 0) return TFT_GREEN;
  return TFT_WHITE;
}

enum class ScreenMode { Chat, Menu, Duel, Rooms, Volume, Walkie, Status, Networks, NetworkPassword };

Preferences preferences;
M5Canvas uiCanvas(&M5Cardputer.Display);
WebSocketsClient walkieSocket;
std::vector<WifiProfile> wifiProfiles;
std::vector<ScannedNetwork> scannedNetworks;
std::vector<ChatMessage> messages;
std::vector<ChatRoom> rooms;
std::vector<DuelOpponent> duelOpponents;
String currentRoomId = "family";
String currentRoomName = "Family";
// Retained clips stream to microSD. The small fallback keeps voice usable
// without a card while reserving heap for simultaneous HTTPS and WSS TLS.
std::array<int16_t, kVoiceFallbackSamples> voiceSamples{};
File voiceCaptureFile;
File localReplayFile;
std::deque<std::vector<int16_t>> incomingAudio;
std::array<std::vector<int16_t>, 2> playbackBuffers;
size_t playbackBufferIndex = 0;
SemaphoreHandle_t stateMutex;
String deviceToken;
String draft;
String networkStatus = "BOOT";
String walkieStatus = "OFFLINE";
String walkieSpeaker;
String currentSsid;
String selectedSsid;
String networkPassword;
String voiceClientId;
ScreenMode screenMode = ScreenMode::Chat;
bool sdReady = false;
bool renderDirty = true;
bool syncOverride = false;
bool timeKnown = false;
bool ntpAttempted = false;
bool timeSyncedByNtp = false;
bool initialSyncComplete = false;
bool keyboardReady = false;
bool uiCanvasReady = false;
bool voiceRecording = false;
bool voiceClipReady = false;
bool voiceUploadPending = false;
bool retainCurrentVoice = false;
bool voiceUsesSd = false;
bool localReplayActive = false;
bool spacePttHeld = false;
uint32_t spaceReleaseStartedAt = 0;
bool walkieConnected = false;
bool walkieInitialized = false;
bool roomsInitialized = false;
bool walkieGranted = false;
bool audioPlaying = false;
volatile bool messageNotificationPending = false;
bool messageNotificationActive = false;
volatile bool manualWifiMode = false;
volatile int64_t voiceDownloadRequestedId = 0;
volatile bool voicePlaybackCancelled = false;
volatile bool voiceDownloadActive = false;
int64_t voicePlayingMessageId = 0;
int voiceInboxSelection = 0;
int menuSelection = 0;
int menuPage = 0;
int roomSelection = 0;
int networkSelection = 0;
int duelOpponentSelection = 0;
int duelSpellSelection = -1;
uint8_t volumeLevel = kDefaultVolumeLevel;
int64_t lastServerId = 0;
int64_t lastReceiptAt = 0;
int lastHttpStatus = 0;
int batteryLevel = -1;
int batteryVoltageMv = 0;
float batteryFilteredMv = 0.0f;
uint32_t lastBatterySampleAt = 0;
uint32_t lastBatteryAdjustAt = 0;
uint32_t lastBatteryPersistAt = 0;
uint32_t lastClockCheckAt = 0;
int lastClockMinute = -1;
uint32_t batteryEdgeStartedAt = 0;
int batteryEdgeBaselineMv = 0;
int batteryPreviousMv = 0;
bool externalPowerDetected = false;
int persistedBatteryLevel = -1;
int64_t duelId = 0;
String duelStatus;
String duelChallenger;
String duelOpponent;
String duelWinnerId;
int duelChallengerScore = 0;
int duelOpponentScore = 0;
bool duelChoiceLocked = false;
bool duelChallengedByMe = false;
bool duelCanAccept = false;
bool duelCanDecline = false;
bool duelCanCancel = false;
bool duelCanChoose = false;
bool duelAttention = false;
bool duelResultSfxPending = false;
int duelRoundNumber = 0;
int64_t duelLastRoundResolvedAt = 0;
String duelLastChallengerSpell;
String duelLastOpponentSpell;
String duelLastRoundWinnerId;
String duelSpellPending;
String duelActionPending;
String duelChallengePending;
int64_t reactionPendingMessageId = 0;
bool typingState = false;
bool typingDirty = false;
uint32_t typingChangedAt = 0;
String typingNotice;
uint32_t lastDraftQueuedAt = 0;

void serviceMessageNotification();
void startDuelSfx(DuelSfxCue cue);
uint32_t lastToneAt = 0;
uint32_t lastRenderAt = 0;
uint32_t lastDuelAnimationAt = 0;
uint32_t duelCountdownStartedAt = 0;
uint32_t duelRevealStartedAt = 0;
uint32_t duelTerminalStartedAt = 0;
uint32_t bootNonce = 0;
uint32_t clientSequence = 0;
size_t songPosition = 0;
size_t messageNotificationPosition = 0;
const DuelToneStep *duelSfxSteps = nullptr;
size_t duelSfxLength = 0;
size_t duelSfxPosition = 0;
uint32_t nextDuelSfxAt = 0;
bool duelSfxActive = false;
size_t historyOffset = 0;
size_t voiceSampleCount = 0;
size_t voiceCapturedTotal = 0;
int16_t voiceCaptureBlock[kVoiceCaptureBlock]{};
int16_t voiceDmaBlocks[3][kVoiceCaptureBlock]{};
size_t voiceDmaRecordIndex = 2;
size_t voiceDmaQueued = 0;
uint32_t voiceStartedAt = 0;
uint32_t lastEspNowBeaconAt = 0;
uint32_t nextMessageNotificationAt = 0;
volatile uint32_t lastNearbyAt = 0;

constexpr uint32_t kEspNowMagic = 0x53555041;
enum class EspNowType : uint8_t { Beacon = 1, Text = 2, Audio = 3 };
struct __attribute__((packed)) EspNowPacket {
  uint32_t magic;
  uint8_t type;
  uint16_t sequence;
  char senderId[6];
  char senderName[8];
  char clientId[48];
  uint16_t payloadLength;
  uint8_t nonce[12];
  uint8_t tag[16];
  uint8_t payload[144];
};
uint16_t espNowSequence = 0;
String walkieHeaders;
uint8_t meshKey[32]{};
bool meshReady = false;
uint8_t recentMeshNonces[32][12]{};
size_t recentMeshNonceCount = 0;
size_t recentMeshNoncePosition = 0;

bool acceptMeshNonce(const uint8_t *nonce) {
  for (size_t index = 0; index < recentMeshNonceCount; index++) {
    if (memcmp(recentMeshNonces[index], nonce, 12) == 0) return false;
  }
  memcpy(recentMeshNonces[recentMeshNoncePosition], nonce, 12);
  recentMeshNoncePosition = (recentMeshNoncePosition + 1) % 32;
  recentMeshNonceCount = std::min<size_t>(recentMeshNonceCount + 1, 32);
  return true;
}

bool decodeHexKey(const String &hex, uint8_t *output, size_t length) {
  if (hex.length() != length * 2) return false;
  for (size_t index = 0; index < length; index++) {
    char pair[3] = {hex[index * 2], hex[index * 2 + 1], 0}; char *end = nullptr;
    const long value = strtol(pair, &end, 16); if (!end || *end) return false; output[index] = value;
  }
  return true;
}

bool encryptMeshPacket(EspNowPacket &packet, const uint8_t *plain, size_t length) {
  if (!meshReady || length > sizeof(packet.payload)) return false;
  packet.payloadLength = length; esp_fill_random(packet.nonce, sizeof(packet.nonce));
  mbedtls_gcm_context context; mbedtls_gcm_init(&context);
  if (mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, meshKey, 256)) { mbedtls_gcm_free(&context); return false; }
  const int result = mbedtls_gcm_crypt_and_tag(&context, MBEDTLS_GCM_ENCRYPT, length, packet.nonce, sizeof(packet.nonce),
      reinterpret_cast<uint8_t *>(&packet), offsetof(EspNowPacket, nonce), plain, packet.payload, sizeof(packet.tag), packet.tag);
  mbedtls_gcm_free(&context); return result == 0;
}

bool decryptMeshPacket(const EspNowPacket &packet, std::vector<uint8_t> &plain) {
  if (!meshReady || packet.payloadLength > sizeof(packet.payload)) return false;
  plain.resize(packet.payloadLength); mbedtls_gcm_context context; mbedtls_gcm_init(&context);
  if (mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, meshKey, 256)) { mbedtls_gcm_free(&context); return false; }
  const int result = mbedtls_gcm_auth_decrypt(&context, packet.payloadLength, packet.nonce, sizeof(packet.nonce),
      reinterpret_cast<const uint8_t *>(&packet), offsetof(EspNowPacket, nonce), packet.tag, sizeof(packet.tag), packet.payload, plain.data());
  mbedtls_gcm_free(&context); return result == 0;
}

void drawBootSplash() {
  auto &display = M5Cardputer.Display;
  const bool previousSwapBytes = display.getSwapBytes();
  display.setSwapBytes(true);
  display.pushImage(0, 0, kSupaChatSplashWidth, kSupaChatSplashHeight,
                    kSupaChatSplash);
  display.setSwapBytes(previousSwapBytes);
  display.setFont(&fonts::Font0); display.setTextSize(1);
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK); display.setCursor(4, 125);
  display.print(kFirmwareVersion);
}

void showBootSplash() {
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  uint32_t target = millis();
  size_t step = 0;
  for (;;) {
    const size_t noteIndex = step % kBootTuneLength;
    serviceMessageNotification();
    if (!messageNotificationPending && !messageNotificationActive && kBootTuneFrequencies[noteIndex] > 0) {
      M5Cardputer.Speaker.tone(kBootTuneFrequencies[noteIndex], kBootTuneNoteMs);
    }
    target += kBootTuneStepMs;
    while (static_cast<int32_t>(target - millis()) > 0) {
      M5Cardputer.update();
      serviceMessageNotification();
      if (M5Cardputer.Keyboard.isPressed() || M5Cardputer.BtnA.isPressed()) {
        M5Cardputer.Speaker.stop(); return;
      }
      delay(10);
    }
    step++;
  }
}

String cleanField(String value) {
  value.replace("\t", " "); value.replace("\r", " "); value.replace("\n", " ");
  return value;
}

String jsonEscape(const String &value) {
  String result; result.reserve(value.length() + 8);
  for (const char character : value) {
    if (character == '\\' || character == '"') { result += '\\'; result += character; }
    else if (character == '\n') result += "\\n";
    else if (character == '\r') result += "\\r";
    else if (static_cast<uint8_t>(character) >= 0x20) result += character;
  }
  return result;
}

void trimHistory() {
  if (messages.size() > kHistoryLimit) messages.erase(messages.begin(), messages.begin() + (messages.size() - kHistoryLimit));
}

String roomHistoryPath(const String &roomId, bool temporary = false) {
  String safe;
  for (const char character : roomId) if (isalnum(static_cast<unsigned char>(character)) || character == '-') safe += character;
  if (safe.isEmpty()) safe = "unknown";
  return "/supachat-" + safe + ".tsv" + (temporary ? ".tmp" : "");
}

void saveHistoryLocked() {
  if (!sdReady) return;
  const String path = roomHistoryPath(currentRoomId);
  const String temporaryPath = roomHistoryPath(currentRoomId, true);
  File file = SD.open(temporaryPath, FILE_WRITE);
  if (!file) return;
  for (const auto &message : messages) {
    file.printf("%lld\t%s\t%s\t%s\t%s\t%lld\t%d\n", message.id, cleanField(message.clientId).c_str(),
                cleanField(message.authorId).c_str(), cleanField(message.authorName).c_str(), cleanField(message.body).c_str(),
                message.createdAt, message.queued ? 1 : 0);
  }
  file.close();
  SD.remove(path);
  SD.rename(temporaryPath, path);
}

void loadHistory() {
  if (!sdReady) return;
  String path = roomHistoryPath(currentRoomId);
  if (!SD.exists(path) && currentRoomId == "family" && SD.exists("/supachat-messages.tsv")) path = "/supachat-messages.tsv";
  File file = SD.open(path, FILE_READ);
  if (!file) return;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    std::vector<String> fields;
    int start = 0;
    for (int index = 0; index <= line.length(); index++) {
      if (index == line.length() || line[index] == '\t') { fields.push_back(line.substring(start, index)); start = index + 1; }
    }
    if (fields.size() < 7) continue;
    ChatMessage message;
    message.roomId = currentRoomId; message.id = strtoll(fields[0].c_str(), nullptr, 10); message.clientId = fields[1]; message.authorId = fields[2];
    message.authorName = fields[3]; message.body = fields[4]; message.createdAt = strtoll(fields[5].c_str(), nullptr, 10);
    message.queued = fields[6].toInt() == 1; message.state = message.queued ? "queued" : "saved";
    messages.push_back(message); lastServerId = std::max(lastServerId, message.id);
  }
  file.close(); trimHistory();
}

void loadConfiguration() {
  preferences.begin("supachat", false);
  deviceToken = preferences.getString("device_token", "");
  meshReady = decodeHexKey(preferences.getString("mesh_key", ""), meshKey, sizeof(meshKey));
  volumeLevel = std::min<uint8_t>(preferences.getUChar("volume", kDefaultVolumeLevel), 4);
  // Older installs commonly persisted MAX as their effective default. Migrate
  // that once so updated devices start at HIGH without overriding later choices.
  if (!preferences.getBool("high_default", false)) {
    if (volumeLevel == 4) {
      volumeLevel = kDefaultVolumeLevel;
      preferences.putUChar("volume", volumeLevel);
    }
    preferences.putBool("high_default", true);
  }
  persistedBatteryLevel = preferences.getInt("battery_soc", -1);
  if (persistedBatteryLevel < 0 || persistedBatteryLevel > 100) persistedBatteryLevel = -1;
  const int count = preferences.getUChar("wifi_count", 0);
  for (int index = 0; index < count && index < 12; index++) {
    const String ssid = preferences.getString(("ssid" + String(index)).c_str(), "");
    const String psk = preferences.getString(("psk" + String(index)).c_str(), "");
    if (!ssid.isEmpty()) wifiProfiles.push_back({ssid, psk});
  }
  preferences.end();
}

void saveVolume() {
  preferences.begin("supachat", false);
  preferences.putUChar("volume", volumeLevel);
  preferences.end();
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
}

void saveWifiProfiles() {
  preferences.begin("supachat", false);
  preferences.putUChar("wifi_count", std::min<size_t>(wifiProfiles.size(), 12));
  for (size_t index = 0; index < wifiProfiles.size() && index < 12; index++) {
    preferences.putString(("ssid" + String(index)).c_str(), wifiProfiles[index].ssid);
    preferences.putString(("psk" + String(index)).c_str(), wifiProfiles[index].password);
  }
  preferences.end();
}

void rememberWifi(const String &ssid, const String &password) {
  auto existing = std::find_if(wifiProfiles.begin(), wifiProfiles.end(),
                               [&](const WifiProfile &profile) { return profile.ssid == ssid; });
  if (existing != wifiProfiles.end()) existing->password = password;
  else {
    if (wifiProfiles.size() >= 12) wifiProfiles.erase(wifiProfiles.begin());
    wifiProfiles.push_back({ssid, password});
  }
  saveWifiProfiles();
}

bool localBlackout() {
  if (!kBlackoutEnabled) return false;
  if (!timeKnown) return false;
  struct tm value{};
  if (!getLocalTime(&value, 100)) return false;
  return value.tm_hour >= 22 || value.tm_hour < 6;
}

void playNextTone() {
  if (voiceRecording || audioPlaying || messageNotificationActive || duelSfxActive) return;
  const uint32_t now = millis();
  if (now - lastToneAt < kToneIntervalMs || kKeypressSongLength == 0) return;
  lastToneAt = now;
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  M5Cardputer.Speaker.tone(kKeypressSongFrequencies[songPosition], kToneDurationMs);
  songPosition = (songPosition + 1) % kKeypressSongLength;
}

void startDuelSfx(DuelSfxCue cue) {
  if (volumeLevel == 0 || cue == DuelSfxCue::None) return;
  switch (cue) {
    case DuelSfxCue::Challenge: duelSfxSteps=kDuelChallengeSfx;duelSfxLength=sizeof(kDuelChallengeSfx)/sizeof(kDuelChallengeSfx[0]);break;
    case DuelSfxCue::Countdown: duelSfxSteps=kDuelCountdownSfx;duelSfxLength=sizeof(kDuelCountdownSfx)/sizeof(kDuelCountdownSfx[0]);break;
    case DuelSfxCue::Select: duelSfxSteps=kDuelSelectSfx;duelSfxLength=sizeof(kDuelSelectSfx)/sizeof(kDuelSelectSfx[0]);break;
    case DuelSfxCue::Lock: duelSfxSteps=kDuelLockSfx;duelSfxLength=sizeof(kDuelLockSfx)/sizeof(kDuelLockSfx[0]);break;
    case DuelSfxCue::Impact: duelSfxSteps=kDuelImpactSfx;duelSfxLength=sizeof(kDuelImpactSfx)/sizeof(kDuelImpactSfx[0]);break;
    case DuelSfxCue::Victory: duelSfxSteps=kDuelVictorySfx;duelSfxLength=sizeof(kDuelVictorySfx)/sizeof(kDuelVictorySfx[0]);break;
    case DuelSfxCue::Defeat: duelSfxSteps=kDuelDefeatSfx;duelSfxLength=sizeof(kDuelDefeatSfx)/sizeof(kDuelDefeatSfx[0]);break;
    case DuelSfxCue::Neutral: duelSfxSteps=kDuelNeutralSfx;duelSfxLength=sizeof(kDuelNeutralSfx)/sizeof(kDuelNeutralSfx[0]);break;
    default: duelSfxSteps=nullptr;duelSfxLength=0;return;
  }
  duelSfxPosition=0; nextDuelSfxAt=millis(); duelSfxActive=true;
}

void serviceDuelSfx() {
  if (volumeLevel == 0) { duelSfxActive=false;duelSfxPosition=0;return; }
  if (!duelSfxActive || voiceRecording || audioPlaying) return;
  const uint32_t now=millis(); if (static_cast<int32_t>(now-nextDuelSfxAt)<0)return;
  if (duelSfxPosition>=duelSfxLength) { duelSfxActive=false;return; }
  const DuelToneStep &step=duelSfxSteps[duelSfxPosition++];
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  M5Cardputer.Speaker.tone(step.frequency,step.durationMs);
  nextDuelSfxAt=now+step.durationMs+step.gapMs;
}

void serviceMessageNotification() {
  if (volumeLevel == 0) { messageNotificationPending = false; messageNotificationActive = false; return; }
  if (voiceRecording || audioPlaying || duelSfxActive) return;
  const uint32_t now = millis();
  if (!messageNotificationActive) {
    if (!messageNotificationPending) return;
    messageNotificationPending = false; messageNotificationActive = true;
    messageNotificationPosition = 0; nextMessageNotificationAt = now;
  }
  if (static_cast<int32_t>(now - nextMessageNotificationAt) < 0) return;
  if (messageNotificationPosition >= kMessageNotificationLength) {
    messageNotificationActive = false; return;
  }
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  M5Cardputer.Speaker.tone(kMessageNotificationFrequencies[messageNotificationPosition],
                           kMessageNotificationDurations[messageNotificationPosition]);
  nextMessageNotificationAt = now + kMessageNotificationDurations[messageNotificationPosition] + kMessageNotificationGapMs;
  messageNotificationPosition++;
}

String nextClientId();
void sendEspNowAudio(const int16_t *samples, size_t count);
void processVoiceBlock(const int16_t *samples) {
  if (retainCurrentVoice && voiceSampleCount + kVoiceCaptureBlock <= kVoiceMaxSamples) {
    if (voiceUsesSd && voiceCaptureFile) {
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      const size_t written = voiceCaptureFile.write(reinterpret_cast<const uint8_t *>(samples), kVoiceCaptureBlock * sizeof(int16_t));
      xSemaphoreGive(stateMutex);
      if (written == kVoiceCaptureBlock * sizeof(int16_t)) voiceSampleCount += kVoiceCaptureBlock;
    } else if (voiceSampleCount + kVoiceCaptureBlock <= voiceSamples.size()) {
      memcpy(voiceSamples.data() + voiceSampleCount, samples, kVoiceCaptureBlock * sizeof(int16_t));
      voiceSampleCount += kVoiceCaptureBlock;
    }
  }
  if (walkieGranted && walkieConnected)
    walkieSocket.sendBIN(reinterpret_cast<const uint8_t *>(samples), kVoiceCaptureBlock * sizeof(int16_t));
  if (kEspNowEnabled && !walkieConnected) sendEspNowAudio(samples, kVoiceCaptureBlock);
  voiceCapturedTotal += kVoiceCaptureBlock;
}

void startVoiceRecording() {
  if (voiceRecording || audioPlaying) return;
  retainCurrentVoice = !voiceUploadPending;
  if (retainCurrentVoice) {
    voiceSampleCount = 0; voiceClipReady = false; voiceUsesSd = false;
    if (sdReady) {
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      if (voiceCaptureFile) voiceCaptureFile.close();
      SD.remove(kVoiceClipPath);
      voiceCaptureFile = SD.open(kVoiceClipPath, FILE_WRITE);
      voiceUsesSd = static_cast<bool>(voiceCaptureFile);
      xSemaphoreGive(stateMutex);
    }
  }
  voiceCapturedTotal = 0; voiceDmaRecordIndex = 2; voiceDmaQueued = 0;
  memset(voiceDmaBlocks, 0, sizeof(voiceDmaBlocks));
  M5Cardputer.Speaker.stop(); M5Cardputer.Speaker.end();
  auto micConfig = M5Cardputer.Mic.config(); micConfig.sample_rate = kVoiceSampleRate;
  micConfig.magnification = 48; micConfig.noise_filter_level = 8;
  M5Cardputer.Mic.config(micConfig);
  if (!M5Cardputer.Mic.begin()) {
    if (voiceCaptureFile) voiceCaptureFile.close();
    M5Cardputer.Speaker.begin(); M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
    walkieStatus = "MIC START FAILED"; retainCurrentVoice = false; renderDirty = true; return;
  }
  voiceRecording = true; voiceStartedAt = millis(); walkieStatus = "RECORDING"; renderDirty = true;
  if (walkieConnected) walkieSocket.sendTXT("{\"type\":\"ptt_start\"}");
}

void stopVoiceRecording() {
  if (!voiceRecording) return;
  const uint32_t drainStarted = millis();
  while (M5Cardputer.Mic.isRecording() && millis() - drainStarted < 250) delay(1);
  // The M5 reference recorder consumes two buffers behind the active DMA
  // target. After DMA stops, retain the final one or two completed blocks.
  const size_t pending = std::min<size_t>(voiceDmaQueued, 2);
  size_t drainIndex = (voiceDmaRecordIndex + (pending == 1 ? 2 : 1)) % 3;
  for (size_t index = 0; index < pending; index++) {
    processVoiceBlock(voiceDmaBlocks[drainIndex]); drainIndex = (drainIndex + 1) % 3;
  }
  if (voiceCaptureFile) {
    xSemaphoreTake(stateMutex, portMAX_DELAY); voiceCaptureFile.flush(); voiceCaptureFile.close(); xSemaphoreGive(stateMutex);
  }
  M5Cardputer.Mic.end(); M5Cardputer.Speaker.begin(); M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  voiceRecording = false;
  if (retainCurrentVoice) {
    voiceClipReady = voiceSampleCount > 0; voiceUploadPending = voiceClipReady;
    if (voiceUploadPending) voiceClientId = nextClientId();
  }
  walkieGranted = false;
  if (walkieConnected) walkieSocket.sendTXT("{\"type\":\"ptt_stop\"}");
  walkieStatus = retainCurrentVoice && voiceClipReady ? "RECORDED - ENTER PLAYS" :
      (voiceUploadPending ? "LIVE - LAST CLIP QUEUED" : "READY");
  renderDirty = true;
}

void sendEspNowAudio(const int16_t *samples, size_t count) {
  if (!meshReady) return;
  static const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
  size_t offset = 0;
  while (offset < count) {
    EspNowPacket packet{}; packet.magic = kEspNowMagic; packet.type = static_cast<uint8_t>(EspNowType::Audio); packet.sequence = ++espNowSequence;
    strncpy(packet.senderId, kDeviceId, sizeof(packet.senderId) - 1); strncpy(packet.senderName, kDeviceName, sizeof(packet.senderName) - 1);
    const size_t take = std::min<size_t>((sizeof(packet.payload) / 2), count - offset);
    if (encryptMeshPacket(packet, reinterpret_cast<const uint8_t *>(samples + offset), take * 2))
      esp_now_send(broadcast, reinterpret_cast<uint8_t *>(&packet), offsetof(EspNowPacket, payload) + packet.payloadLength);
    offset += take;
  }
}

void captureVoice() {
  if (!voiceRecording) return;
  if (millis() - voiceStartedAt >= kWalkieMaxMs) { stopVoiceRecording(); return; }
  const size_t queuedIndex = voiceDmaRecordIndex;
  if (!M5Cardputer.Mic.record(voiceDmaBlocks[queuedIndex], kVoiceCaptureBlock, kVoiceSampleRate)) return;
  voiceDmaQueued++;
  // Exactly mirror M5's working example: consume the buffer two queue slots
  // behind the one just handed to DMA.
  if (voiceDmaQueued > 2) processVoiceBlock(voiceDmaBlocks[(queuedIndex + 1) % 3]);
  voiceDmaRecordIndex = (queuedIndex + 1) % 3; renderDirty = true;
}

bool queuePlaybackSamples(std::vector<int16_t> samples) {
  if (samples.empty() || voiceRecording || volumeLevel == 0 || M5Cardputer.Speaker.isPlaying(0) >= 2) return false;
  M5Cardputer.Mic.end(); M5Cardputer.Speaker.begin(); M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  auto &buffer = playbackBuffers[playbackBufferIndex]; buffer = std::move(samples);
  const bool queued = M5Cardputer.Speaker.playRaw(buffer.data(), buffer.size(), kVoiceSampleRate, false, 1, 0, false);
  if (queued) playbackBufferIndex = (playbackBufferIndex + 1) % playbackBuffers.size();
  audioPlaying = M5Cardputer.Speaker.isPlaying(0) > 0; return queued;
}

void playSamples(const int16_t *samples, size_t count) {
  if (!samples || !count) return;
  queuePlaybackSamples(std::vector<int16_t>(samples, samples + count));
}

void serviceAudioPlayback() {
  audioPlaying = M5Cardputer.Speaker.isPlaying(0) > 0;
  if (voiceRecording) return;
  while (M5Cardputer.Speaker.isPlaying(0) < 2 && !incomingAudio.empty()) {
    xSemaphoreTake(stateMutex, portMAX_DELAY); auto samples = std::move(incomingAudio.front()); incomingAudio.pop_front(); xSemaphoreGive(stateMutex);
    if (!queuePlaybackSamples(std::move(samples))) break;
  }
  while (M5Cardputer.Speaker.isPlaying(0) < 2 && localReplayActive && localReplayFile) {
    std::vector<int16_t> samples(kVoicePlaybackBlock);
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    const size_t bytes = localReplayFile.read(reinterpret_cast<uint8_t *>(samples.data()), samples.size() * sizeof(int16_t));
    xSemaphoreGive(stateMutex);
    if (bytes >= 2) { samples.resize(bytes / 2); if (!queuePlaybackSamples(std::move(samples))) break; continue; }
    localReplayFile.close(); localReplayActive = false; walkieStatus = "READY"; renderDirty = true;
  }
  if (voicePlayingMessageId > 0 && !voiceDownloadActive && incomingAudio.empty() && !audioPlaying) {
    voicePlayingMessageId = 0; walkieStatus = "READY"; renderDirty = true;
  }
}

void stopVoicePlayback() {
  voicePlaybackCancelled = true; voiceDownloadRequestedId = 0;
  M5Cardputer.Speaker.stop(); audioPlaying = false;
  xSemaphoreTake(stateMutex, portMAX_DELAY); incomingAudio.clear(); xSemaphoreGive(stateMutex);
  for (auto &buffer : playbackBuffers) buffer.clear();
  voicePlayingMessageId = 0; walkieStatus = "STOPPED"; renderDirty = true;
}

std::vector<ChatMessage> voiceInbox() {
  std::vector<ChatMessage> result;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  for (auto item = messages.rbegin(); item != messages.rend(); ++item) if (item->voice) result.push_back(*item);
  xSemaphoreGive(stateMutex);
  return result;
}

void playSelectedVoiceMessage() {
  if (voicePlayingMessageId > 0 || voiceDownloadRequestedId > 0 || audioPlaying) { stopVoicePlayback(); return; }
  const auto inbox = voiceInbox();
  if (inbox.empty()) { walkieStatus = "NO VOICE MESSAGES"; renderDirty = true; return; }
  voiceInboxSelection = std::min(voiceInboxSelection, static_cast<int>(inbox.size()) - 1);
  voicePlaybackCancelled = false; voiceDownloadRequestedId = inbox[voiceInboxSelection].id;
  walkieStatus = "LOADING"; renderDirty = true;
}

void startLocalReplay() {
  if (!voiceClipReady || voiceRecording || audioPlaying) return;
  if (voiceUsesSd && sdReady) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (localReplayFile) localReplayFile.close();
    localReplayFile = SD.open(kVoiceClipPath, FILE_READ);
    localReplayActive = static_cast<bool>(localReplayFile);
    xSemaphoreGive(stateMutex);
    if (localReplayActive) { walkieStatus = "REPLAYING"; renderDirty = true; }
  } else playSamples(voiceSamples.data(), voiceSampleCount);
}

void onWalkieEvent(WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) { walkieConnected = true; walkieStatus = "READY"; renderDirty = true; return; }
  if (type == WStype_DISCONNECTED) { walkieConnected = false; walkieGranted = false; walkieStatus = "RECONNECTING"; renderDirty = true; return; }
  if (type == WStype_ERROR) {
    walkieConnected = false; walkieGranted = false; walkieStatus = "WS ERROR";
    Serial.printf("walkie error=%.*s heap=%u\n", static_cast<int>(length), payload, ESP.getFreeHeap()); renderDirty = true; return;
  }
  if (type == WStype_TEXT) {
    JsonDocument document; if (deserializeJson(document, payload, length)) return;
    const String event = document["type"] | ""; const String user = document["user"] | "";
    if (event == "ptt_start") { walkieSpeaker = user; walkieGranted = user == kDeviceId; walkieStatus = walkieGranted ? "TRANSMITTING" : (user + " TALKING"); }
    else if (event == "ptt_stop") { walkieSpeaker = ""; walkieGranted = false; walkieStatus = "READY"; }
    else if (event == "busy") walkieStatus = String(document["speaker"] | "OTHER") + " HAS CHANNEL";
    renderDirty = true; return;
  }
  if (type == WStype_BIN && length >= 2 && length % 2 == 0) {
    std::vector<int16_t> samples(length / 2); memcpy(samples.data(), payload, length);
    xSemaphoreTake(stateMutex, portMAX_DELAY); if (incomingAudio.size() < 12) incomingAudio.push_back(std::move(samples)); xSemaphoreGive(stateMutex);
  }
}

void onEspNowReceive(const uint8_t *, const uint8_t *data, int length) {
  if (length < static_cast<int>(offsetof(EspNowPacket, payload))) return;
  const auto *packet = reinterpret_cast<const EspNowPacket *>(data);
  if (packet->magic != kEspNowMagic || packet->payloadLength > sizeof(packet->payload) ||
      static_cast<int>(offsetof(EspNowPacket, payload) + packet->payloadLength) > length || String(packet->senderId) == kDeviceId) return;
  lastNearbyAt = millis();
  if (packet->type == static_cast<uint8_t>(EspNowType::Beacon)) { renderDirty = true; return; }
  std::vector<uint8_t> plain; if (!decryptMeshPacket(*packet, plain) || !acceptMeshNonce(packet->nonce)) return;
  if (packet->type == static_cast<uint8_t>(EspNowType::Audio) && plain.size() % 2 == 0) {
    std::vector<int16_t> samples(plain.size() / 2); memcpy(samples.data(), plain.data(), plain.size());
    if (stateMutex) { xSemaphoreTake(stateMutex, portMAX_DELAY); if (incomingAudio.size() < 12) incomingAudio.push_back(std::move(samples)); xSemaphoreGive(stateMutex); }
  } else if (packet->type == static_cast<uint8_t>(EspNowType::Text) && packet->payloadLength > 0) {
    ChatMessage message; message.roomId = currentRoomId; message.clientId = packet->clientId; message.authorId = packet->senderId; message.authorName = packet->senderName;
    message.body = String(reinterpret_cast<const char *>(plain.data())).substring(0, plain.size()); message.state = "nearby";
    if (stateMutex) { xSemaphoreTake(stateMutex, portMAX_DELAY); if (std::none_of(messages.begin(), messages.end(), [&](const ChatMessage &item){ return item.clientId == message.clientId; })) messages.push_back(message); xSemaphoreGive(stateMutex); renderDirty = true; }
  }
}

void initializeEspNow() {
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(onEspNowReceive);
  esp_now_peer_info_t peer{}; memset(peer.peer_addr, 0xff, 6); peer.channel = 0; peer.ifidx = WIFI_IF_STA; peer.encrypt = false;
  if (!esp_now_is_peer_exist(peer.peer_addr)) esp_now_add_peer(&peer);
}

void sendEspNowBeacon() {
  static const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
  EspNowPacket packet{}; packet.magic = kEspNowMagic; packet.type = static_cast<uint8_t>(EspNowType::Beacon); packet.sequence = ++espNowSequence;
  strncpy(packet.senderId, kDeviceId, sizeof(packet.senderId) - 1); strncpy(packet.senderName, kDeviceName, sizeof(packet.senderName) - 1);
  esp_now_send(broadcast, reinterpret_cast<uint8_t *>(&packet), offsetof(EspNowPacket, payload)); lastEspNowBeaconAt = millis();
}

bool connectKnownWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  networkStatus = "SCANNING"; renderDirty = true;
  const int count = WiFi.scanNetworks(false, true);
  int bestProfile = -1; int bestRssi = -1000;
  for (int scan = 0; scan < count; scan++) {
    for (int profile = 0; profile < static_cast<int>(wifiProfiles.size()); profile++) {
      if (WiFi.SSID(scan) == wifiProfiles[profile].ssid && WiFi.RSSI(scan) > bestRssi) { bestProfile = profile; bestRssi = WiFi.RSSI(scan); }
    }
  }
  WiFi.scanDelete();
  if (bestProfile < 0) { networkStatus = "NO KNOWN WIFI"; return false; }
  networkStatus = "JOINING"; renderDirty = true;
  WiFi.begin(wifiProfiles[bestProfile].ssid.c_str(), wifiProfiles[bestProfile].password.c_str());
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 12000) vTaskDelay(pdMS_TO_TICKS(100));
  if (WiFi.status() != WL_CONNECTED) { WiFi.disconnect(false, false); networkStatus = "JOIN FAILED"; return false; }
  currentSsid = WiFi.SSID(); networkStatus = "ONLINE"; renderDirty = true;
  return true;
}

void ensureTime() {
  if (ntpAttempted) return;
  ntpAttempted = true;
  configTzTime("EST5EDT,M3.2.0/2,M11.1.0/2", "time.cloudflare.com", "time.google.com", "pool.ntp.org");
  const uint32_t started = millis();
  struct tm value{};
  while (millis() - started < kTimeWaitMs) {
    if (getLocalTime(&value, 100) && value.tm_year >= 124) { timeKnown = true; timeSyncedByNtp = true; return; }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void seedBuildTime() {
  // TLS certificate validation needs a plausible clock. The firmware build
  // timestamp is a safe offline floor until NTP supplies the precise time.
  static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char monthName[4]{};
  int day = 0, year = 0, hour = 0, minute = 0, second = 0;
  if (sscanf(__DATE__, "%3s %d %d", monthName, &day, &year) != 3 ||
      sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) return;
  const char *month = strstr(months, monthName);
  if (!month) return;
  struct tm build{};
  build.tm_year = year - 1900; build.tm_mon = static_cast<int>((month - months) / 3);
  build.tm_mday = day; build.tm_hour = hour; build.tm_min = minute; build.tm_sec = second;
  setenv("TZ", "UTC0", 1); tzset();
  const time_t epoch = mktime(&build);
  if (epoch <= 1704067200) return;
  timeval value{epoch, 0};
  settimeofday(&value, nullptr); timeKnown = true;
  setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1); tzset();
}

String easternClockText() {
  if (!timeKnown) return "--:--";
  struct tm value{};
  if (!getLocalTime(&value, 20)) return "--:--";
  const int hour = value.tm_hour % 12 == 0 ? 12 : value.tm_hour % 12;
  char clockText[8]{};
  snprintf(clockText, sizeof(clockText), "%d:%02d%c", hour, value.tm_min, value.tm_hour < 12 ? 'A' : 'P');
  return String(clockText);
}

void serviceClockRender() {
  const uint32_t now = millis();
  if (now - lastClockCheckAt < 1000) return;
  lastClockCheckAt = now;
  struct tm value{};
  if (timeKnown && getLocalTime(&value, 20) && value.tm_min != lastClockMinute) {
    lastClockMinute = value.tm_min;
    renderDirty = true;
  }
}

void sampleBattery(bool force = false) {
  const uint32_t now = millis();
  if (!force && now - lastBatterySampleAt < kBatterySampleMs) return;
  lastBatterySampleAt = now;
  const int nextVoltage = M5Cardputer.Power.getBatteryVoltage();
  if (batteryFilteredMv <= 0.0f) batteryFilteredMv = nextVoltage;
  else batteryFilteredMv += (nextVoltage - batteryFilteredMv) * 0.12f;

  // Cable presence is an edge detector, independent of the slower SOC model.
  // Two directional samples plus a substantial raw-voltage edge reject ADC
  // noise while recognizing a newly connected cable in only a few seconds.
  if (batteryEdgeBaselineMv == 0) {
    batteryEdgeBaselineMv = batteryPreviousMv = nextVoltage;
    batteryEdgeStartedAt = now;
  } else {
    if (!externalPowerDetected && nextVoltage - batteryEdgeBaselineMv >= kPowerConnectRiseMv) {
      externalPowerDetected = true;
      lastBatteryAdjustAt = now - kBatteryFastRiseStepMs;
      renderDirty = true;
    } else if (externalPowerDetected && batteryPreviousMv - nextVoltage >= kPowerDisconnectFallMv) {
      externalPowerDetected = false;
      batteryEdgeBaselineMv = nextVoltage;
      batteryEdgeStartedAt = now;
      renderDirty = true;
    } else if (!externalPowerDetected && now - batteryEdgeStartedAt >= kPowerEdgeWindowMs) {
      batteryEdgeBaselineMv = nextVoltage;
      batteryEdgeStartedAt = now;
    }
    batteryPreviousMv = nextVoltage;
  }

  // Approximate a single-cell LiPo's nonlinear resting-voltage curve. The
  // stateful/rate-limited estimate is more important than any one ADC sample:
  // USB and load changes can move the measured voltage by tens of millivolts.
  static constexpr int voltagePoints[] = {3300, 3500, 3600, 3700, 3750, 3800, 3850, 3900, 4000, 4100, 4200};
  static constexpr int percentPoints[] = {0, 5, 12, 25, 38, 52, 65, 76, 86, 94, 100};
  const int filteredMv = static_cast<int>(batteryFilteredMv + 0.5f);
  int targetLevel = 100;
  if (filteredMv <= voltagePoints[0]) targetLevel = 0;
  else {
    for (size_t index = 1; index < sizeof(voltagePoints) / sizeof(voltagePoints[0]); ++index) {
      if (filteredMv <= voltagePoints[index]) {
        const int spanMv = voltagePoints[index] - voltagePoints[index - 1];
        targetLevel = percentPoints[index - 1] +
          (filteredMv - voltagePoints[index - 1]) * (percentPoints[index] - percentPoints[index - 1]) / spanMv;
        break;
      }
    }
  }

  const int previousLevel = batteryLevel;
  if (batteryLevel < 0) {
    batteryLevel = persistedBatteryLevel >= 0 ? persistedBatteryLevel : targetLevel;
    lastBatteryAdjustAt = now;
  } else if (targetLevel != batteryLevel) {
    const uint32_t stepMs = targetLevel > batteryLevel
      ? (externalPowerDetected ? kBatteryFastRiseStepMs : kBatteryRiseStepMs)
      : kBatteryFallStepMs;
    if (now - lastBatteryAdjustAt >= stepMs) {
      batteryLevel += targetLevel > batteryLevel ? 1 : -1;
      lastBatteryAdjustAt = now;
    }
  }
  if ((persistedBatteryLevel < 0 || now - lastBatteryPersistAt >= kBatteryPersistMs) &&
      batteryLevel != persistedBatteryLevel) {
    preferences.begin("supachat", false);
    preferences.putInt("battery_soc", batteryLevel);
    preferences.end();
    persistedBatteryLevel = batteryLevel;
    lastBatteryPersistAt = now;
  }
  if (batteryLevel != previousLevel) renderDirty = true;
  if (nextVoltage != batteryVoltageMv) renderDirty = true;
  batteryVoltageMv = nextVoltage;
}

bool requestJson(const String &path, const char *method, const String &requestBody, String &responseBody, int &status) {
  WiFiClientSecure client; client.setInsecure(); client.setTimeout(35); client.setHandshakeTimeout(20);
  if (!client.connect(kApiHost, 443)) { status = HTTPC_ERROR_CONNECTION_REFUSED; return false; }
  if (!client.verify(kTlsFingerprint, kApiHost)) { client.stop(); status = HTTPC_ERROR_CONNECTION_LOST; return false; }
  HTTPClient http; http.setTimeout(35000); http.setReuse(false);
  if (!http.begin(client, String(kApiBase) + path)) return false;
  http.addHeader("Authorization", "Bearer " + deviceToken);
  if (requestBody.length()) http.addHeader("Content-Type", "application/json");
  status = strcmp(method, "GET") == 0 ? http.GET() : http.POST(requestBody);
  if (status < 0) {
    char tlsError[96]{}; const int error = client.lastError(tlsError, sizeof(tlsError));
    Serial.printf("https status=%d tls=%d detail=%s heap=%u\n", status, error, tlsError, ESP.getFreeHeap());
  }
  lastHttpStatus = status;
  if (status > 0) responseBody = http.getString();
  http.end(); return status > 0;
}

bool uploadVoiceClip() {
  if (!voiceUploadPending || !voiceSampleCount || WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client; client.setInsecure(); client.setTimeout(35); client.setHandshakeTimeout(20);
  if (!client.connect(kApiHost, 443) || !client.verify(kTlsFingerprint, kApiHost)) { client.stop(); return false; }
  HTTPClient http; http.setTimeout(35000); http.setReuse(false);
  if (!http.begin(client, String(kApiBase) + "/api/voice")) return false;
  http.addHeader("Authorization", "Bearer " + deviceToken);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Room-Id", currentRoomId);
  if (voiceClientId.isEmpty()) voiceClientId = nextClientId();
  http.addHeader("X-Client-Id", voiceClientId); http.addHeader("X-Sample-Rate", String(kVoiceSampleRate));
  int status = -1;
  if (voiceUsesSd && sdReady) {
    File clip = SD.open(kVoiceClipPath, FILE_READ);
    if (clip) { status = http.sendRequest("POST", &clip, voiceSampleCount * 2); clip.close(); }
  } else status = http.POST(reinterpret_cast<uint8_t *>(voiceSamples.data()), voiceSampleCount * 2);
  lastHttpStatus = status;
  http.end();
  if (status != 200 && status != 201) {
    walkieStatus = "CLIP HTTP " + String(status);
    Serial.printf("clip upload status=%d samples=%u sd=%d heap=%u\n", status, voiceSampleCount, voiceUsesSd, ESP.getFreeHeap());
    renderDirty = true; return false;
  }
  voiceUploadPending = false; voiceClientId = ""; walkieStatus = "CLIP SENT"; renderDirty = true; return true;
}

bool downloadVoiceClip(int64_t messageId) {
  WiFiClientSecure client; client.setCACert(kRootCa); client.setTimeout(35);
  HTTPClient http; http.setTimeout(35000); http.setReuse(false);
  if (!http.begin(client, String(kApiBase) + "/api/voice/" + String(messageId) + "/audio")) return false;
  http.addHeader("Authorization", "Bearer " + deviceToken);
  const int status = http.GET(); const int length = http.getSize();
  if (status != 200 || length <= 0 || length > 80000 || length % 2) { http.end(); return false; }
  WiFiClient *stream = http.getStreamPtr();
  int remaining = length;
  while (remaining > 0) {
    if (voicePlaybackCancelled) { http.end(); return false; }
    while (true) {
      if (voicePlaybackCancelled) { http.end(); return false; }
      xSemaphoreTake(stateMutex, portMAX_DELAY); const bool room = incomingAudio.size() < 8; xSemaphoreGive(stateMutex);
      if (room) break;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    const size_t sampleCount = std::min<size_t>(kVoicePlaybackBlock, remaining / 2);
    std::vector<int16_t> samples(sampleCount);
    const size_t wanted = sampleCount * 2;
    const size_t received = stream->readBytes(reinterpret_cast<uint8_t *>(samples.data()), wanted);
    if (received != wanted) { http.end(); return false; }
    xSemaphoreTake(stateMutex, portMAX_DELAY); incomingAudio.push_back(std::move(samples)); xSemaphoreGive(stateMutex);
    remaining -= received;
  }
  http.end(); return true;
}

String nextClientId() {
  return String(kDeviceId) + "-" + String(bootNonce, HEX) + "-" + String(++clientSequence);
}

void mergeServerMessage(JsonObjectConst object) {
  const String messageRoomId = String(object["conversation_id"] | "");
  // A room switch can happen while an HTTPS sync is in flight. Never merge
  // that stale response into the newly selected room.
  if (messageRoomId.isEmpty() || messageRoomId != currentRoomId) return;
  const int64_t id = object["id"] | 0;
  const String clientId = object["client_id"] | "";
  auto existing = std::find_if(messages.begin(), messages.end(), [&](const ChatMessage &item) {
    return (id > 0 && item.id == id) || (!clientId.isEmpty() && item.clientId == clientId);
  });
  ChatMessage serverMessage;
  serverMessage.roomId = messageRoomId; serverMessage.id = id; serverMessage.clientId = clientId; serverMessage.authorId = String(object["author_id"] | "");
  serverMessage.authorName = String(object["author_name"] | "?"); serverMessage.body = String(object["body"] | "");
  serverMessage.createdAt = object["created_at"] | 0; serverMessage.queued = false; serverMessage.state = "saved";
  serverMessage.voice = String(object["type"] | "text") == "voice";
  serverMessage.deleted = !object["deleted_at"].isNull(); serverMessage.edited = !object["edited_at"].isNull();
  serverMessage.replyToId = object["reply_to_id"] | 0;
  if (!object["reply_to"].isNull()) { serverMessage.replyAuthor = String(object["reply_to"]["author_name"] | ""); serverMessage.replyBody = String(object["reply_to"]["body"] | ""); }
  for (JsonObjectConst reaction : object["reactions"].as<JsonArrayConst>()) serverMessage.reactionCount += reaction["count"] | 0;
  for (JsonObjectConst receipt : object["receipts"].as<JsonArrayConst>()) {
    if (String(receipt["user_id"] | "") == "papa") serverMessage.state = String(receipt["state"] | "saved");
  }
  if (existing == messages.end()) messages.push_back(serverMessage); else *existing = serverMessage;
  lastServerId = std::max(lastServerId, id); trimHistory(); renderDirty = true;
}

void sendQueuedMessages() {
  std::vector<String> queuedIds;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  for (const auto &message : messages) if (message.queued) queuedIds.push_back(message.clientId);
  xSemaphoreGive(stateMutex);
  for (const auto &clientId : queuedIds) {
    String text; int64_t replyToId=0;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    auto item = std::find_if(messages.begin(), messages.end(), [&](const ChatMessage &message) { return message.clientId == clientId; });
    if (item != messages.end()) { text = item->body; replyToId=item->replyToId; }
    xSemaphoreGive(stateMutex);
    if (text.isEmpty()) continue;
    String response; int status = 0;
    String queuedRoomId;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    item = std::find_if(messages.begin(), messages.end(), [&](const ChatMessage &message) { return message.clientId == clientId; });
    if (item != messages.end()) queuedRoomId = item->roomId;
    xSemaphoreGive(stateMutex);
    if (queuedRoomId != currentRoomId) continue;
    const String payload = "{\"client_id\":\"" + jsonEscape(clientId) + "\",\"body\":\"" + jsonEscape(text) + "\",\"room_id\":\"" + jsonEscape(queuedRoomId) + "\",\"reply_to_id\":" + (replyToId>0?String(replyToId):String("null")) + "}";
    if (!requestJson("/api/messages", "POST", payload, response, status) || (status != 200 && status != 201)) continue;
    JsonDocument document; if (deserializeJson(document, response)) continue;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    mergeServerMessage(document["message"].as<JsonObjectConst>()); saveHistoryLocked();
    xSemaphoreGive(stateMutex);
  }
}

void postReadReceipt(int64_t messageId) {
  String response; int status = 0;
  requestJson("/api/receipts", "POST", "{\"message_id\":" + String(messageId) + ",\"state\":\"read\"}", response, status);
}

bool duelTerminalStatus(const String &status) {
  return status=="complete"||status=="declined"||status=="cancelled"||status=="expired";
}

void updateDuel(JsonVariantConst value) {
  const int64_t previousId=duelId; const String previousStatus=duelStatus; const int64_t previousRoundResolvedAt=duelLastRoundResolvedAt;
  if (value.isNull()) {
    duelId=0;duelStatus="";duelChoiceLocked=false;duelWinnerId="";duelChallengedByMe=false;
    duelCanAccept=false;duelCanDecline=false;duelCanCancel=false;duelCanChoose=false;duelRoundNumber=0;
    duelLastRoundResolvedAt=0;duelLastChallengerSpell="";duelLastOpponentSpell="";duelLastRoundWinnerId="";
    duelSpellSelection=-1;duelCountdownStartedAt=0;duelRevealStartedAt=0;duelTerminalStartedAt=0;return;
  }
  duelId=value["id"]|0; duelStatus=String(value["status"]|""); duelChoiceLocked=value["my_choice_locked"]|false;
  duelChallenger=String(value["challenger"]["display_name"]|"?"); duelOpponent=String(value["opponent"]["display_name"]|"?");
  duelChallengerScore=value["challenger_score"]|0; duelOpponentScore=value["opponent_score"]|0;
  duelWinnerId=String(value["winner_id"]|"");
  duelChallengedByMe=value["challenged_by_me"]|false;duelCanAccept=value["can_accept"]|false;duelCanDecline=value["can_decline"]|false;
  duelCanCancel=value["can_cancel"]|false;duelCanChoose=value["can_choose"]|false;duelRoundNumber=value["round_number"]|0;
  JsonVariantConst lastRound=value["last_round"];
  duelLastRoundResolvedAt=lastRound.isNull()?0:(lastRound["resolved_at"]|0LL);
  duelLastChallengerSpell=lastRound.isNull()?String(""):String(lastRound["challenger_spell"]|"");
  duelLastOpponentSpell=lastRound.isNull()?String(""):String(lastRound["opponent_spell"]|"");
  duelLastRoundWinnerId=lastRound.isNull()?String(""):String(lastRound["winner_id"]|"");
  const uint32_t now=millis();
  if ((duelId!=previousId||previousStatus!=duelStatus)&&duelStatus=="pending"&&!duelChallengedByMe) {
    duelAttention=true;startDuelSfx(DuelSfxCue::Challenge);
  }
  if (duelStatus=="active"&&(duelId!=previousId||previousStatus=="pending")) {
    duelCountdownStartedAt=now;duelSpellSelection=-1;startDuelSfx(DuelSfxCue::Countdown);
  }
  if (duelLastRoundResolvedAt>0&&duelLastRoundResolvedAt!=previousRoundResolvedAt) {
    duelRevealStartedAt=now;duelSpellSelection=-1;duelResultSfxPending=duelTerminalStatus(duelStatus);startDuelSfx(DuelSfxCue::Impact);
  }
  if (duelTerminalStatus(duelStatus)&&(duelId!=previousId||!duelTerminalStatus(previousStatus))) {
    duelAttention=true;duelTerminalStartedAt=now;
    if (duelRevealStartedAt==0) startDuelSfx(duelStatus=="complete"?(duelWinnerId==kDeviceId?DuelSfxCue::Victory:DuelSfxCue::Defeat):DuelSfxCue::Neutral);
  }
}

void serviceMessagingActions() {
  if (typingDirty) {
    String response; int status=0; const String payload="{\"room_id\":\""+jsonEscape(currentRoomId)+"\",\"typing\":"+(typingState?"true":"false")+"}";
    if (requestJson("/api/typing","POST",payload,response,status)&&status==200) typingDirty=false;
  }
  if (reactionPendingMessageId>0) {
    String response; int status=0; const int64_t id=reactionPendingMessageId;
    if (requestJson("/api/messages/"+String(id)+"/reactions","POST","{\"emoji\":\"👍\"}",response,status)&&status==200) reactionPendingMessageId=0;
  }
  if (!duelChallengePending.isEmpty()) {
    String response;int status=0;const String payload="{\"room_id\":\""+jsonEscape(currentRoomId)+"\",\"opponent\":\""+jsonEscape(duelChallengePending)+"\"}";
    if(requestJson("/api/duels/challenge","POST",payload,response,status)){
      if(status==200){JsonDocument result;if(!deserializeJson(result,response))updateDuel(result["duel"]);duelChallengePending="";}
      else{networkStatus="DUEL HTTP "+String(status);duelChallengePending="";renderDirty=true;}
    }
  }
  if (!duelSpellPending.isEmpty()&&duelId>0) {
    String response; int status=0; const String payload="{\"room_id\":\""+jsonEscape(currentRoomId)+"\",\"spell\":\""+jsonEscape(duelSpellPending)+"\"}";
    if (requestJson("/api/duels/"+String(duelId)+"/choice","POST",payload,response,status)) { if(status==200){JsonDocument result;if(!deserializeJson(result,response))updateDuel(result["duel"]);}else{networkStatus="DUEL HTTP "+String(status);duelChoiceLocked=false;}duelSpellPending="";renderDirty=true; }
  }
  if (!duelActionPending.isEmpty()&&duelId>0) {
    String response; int status=0; const String action=duelActionPending; const String payload="{\"room_id\":\""+jsonEscape(currentRoomId)+"\"}";
    if (requestJson("/api/duels/"+String(duelId)+"/"+action,"POST",payload,response,status)) { if(status==200){JsonDocument result;if(!deserializeJson(result,response))updateDuel(result["duel"]);}else networkStatus="DUEL HTTP "+String(status);duelActionPending="";renderDirty=true; }
  }
}

void synchronize() {
  String roomsResponse; int roomsStatus = 0;
  if (requestJson("/api/rooms", "GET", "", roomsResponse, roomsStatus) && roomsStatus == 200) {
    JsonDocument roomDocument;
    if (!deserializeJson(roomDocument, roomsResponse)) {
      std::vector<ChatRoom> nextRooms;
      for (JsonObjectConst object : roomDocument["rooms"].as<JsonArrayConst>()) {
        const String id = String(object["id"] | ""); const int64_t latest = object["latest_message_id"] | 0;
        auto previous = std::find_if(rooms.begin(), rooms.end(), [&](const ChatRoom &room) { return room.id == id; });
        const int64_t seen = previous != rooms.end() ? previous->seenMessageId : latest;
        nextRooms.push_back({id, String(object["name"] | "Room"), latest, seen});
      }
      rooms = std::move(nextRooms); roomsInitialized = true;
      auto active = std::find_if(rooms.begin(), rooms.end(), [&](const ChatRoom &room) { return room.id == currentRoomId; });
      if (active == rooms.end() && !rooms.empty()) { currentRoomId = rooms[0].id; currentRoomName = rooms[0].name; }
      else if (active != rooms.end()) currentRoomName = active->name;
    }
  }
  sendQueuedMessages(); serviceMessagingActions();
  String response; int status = 0;
  const String path = "/api/device/sync?after=" + String(lastServerId) + "&receipts_after=" + String(lastReceiptAt)
      + "&limit=" + String(kSyncBatchLimit) + "&wait=0&room=" + currentRoomId;
  if (!requestJson(path, "GET", "", response, status)) {
    networkStatus = "SYNC IO " + String(status);
    Serial.printf("sync transport error=%d heap=%u\n", status, ESP.getFreeHeap()); renderDirty = true; return;
  }
  if (status != 200) {
    networkStatus = "SYNC HTTP " + String(status);
    Serial.printf("sync http status=%d body=%s\n", status, response.substring(0, 80).c_str()); renderDirty = true; return;
  }
  JsonDocument document; if (deserializeJson(document, response)) { networkStatus = "BAD DATA"; renderDirty = true; return; }
  updateDuel(document["duel"]);
  duelOpponents.clear();for(JsonObjectConst person:document["presence"].as<JsonArrayConst>()){
    const String id=String(person["id"]|"");if(id.isEmpty()||id==kDeviceId)continue;
    duelOpponents.push_back({id,String(person["display_name"]|id),String(person["status"]|"offline")});
  }
  if(!duelOpponents.empty())duelOpponentSelection=std::min(duelOpponentSelection,static_cast<int>(duelOpponents.size())-1);else duelOpponentSelection=0;
  typingNotice=""; for(JsonObjectConst person:document["typing"].as<JsonArrayConst>()){if(!typingNotice.isEmpty())typingNotice+=", ";typingNotice+=String(person["display_name"]|"Someone");} if(!typingNotice.isEmpty())typingNotice+=" typing";
  std::vector<int64_t> newlyRead;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  for (JsonObjectConst object : document["messages"].as<JsonArrayConst>()) {
    const bool incoming = String(object["author_id"] | "") != kDeviceId;
    mergeServerMessage(object);
    if (incoming) newlyRead.push_back(object["id"] | 0);
  }
  for (JsonObjectConst receipt : document["receipts"].as<JsonArrayConst>()) {
    const int64_t messageId = receipt["message_id"] | 0;
    const int64_t updatedAt = receipt["updated_at"] | 0;
    lastReceiptAt = std::max(lastReceiptAt, updatedAt);
    if (String(receipt["user_id"] | "") == "papa") {
      auto item = std::find_if(messages.begin(), messages.end(), [&](const ChatMessage &message) { return message.id == messageId; });
      if (item != messages.end()) item->state = String(receipt["state"] | item->state);
    }
  }
  saveHistoryLocked(); xSemaphoreGive(stateMutex);
  if (!newlyRead.empty()) messageNotificationPending = true;
  for (const int64_t id : newlyRead) if (id > 0) postReadReceipt(id);
  networkStatus = "SYNCED"; renderDirty = true;
  initialSyncComplete = true;
  auto activeRoom = std::find_if(rooms.begin(), rooms.end(), [&](const ChatRoom &room) { return room.id == currentRoomId; });
  if (activeRoom != rooms.end()) activeRoom->seenMessageId = std::max(activeRoom->latestMessageId, lastServerId);
}

void networkTask(void *) {
  uint32_t nextWifiAttempt = 0;
  for (;;) {
    if (manualWifiMode) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED && now < nextWifiAttempt) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
    if (!connectKnownWifi()) { nextWifiAttempt = millis() + kWifiRetryMs; vTaskDelay(pdMS_TO_TICKS(500)); continue; }
    ensureTime();
    if (localBlackout() && !syncOverride) {
      networkStatus = "BLACKOUT 22-06"; currentSsid = ""; renderDirty = true;
      WiFi.disconnect(true, false); nextWifiAttempt = millis() + 60000;
      vTaskDelay(pdMS_TO_TICKS(1000)); continue;
    }
    if (voiceDownloadRequestedId > 0) {
      const int64_t messageId = voiceDownloadRequestedId; voiceDownloadRequestedId = 0;
      voicePlaybackCancelled = false; voiceDownloadActive = true; voicePlayingMessageId = messageId;
      walkieStatus = "LOADING"; renderDirty = true;
      const bool loaded = downloadVoiceClip(messageId); voiceDownloadActive = false;
      if (!loaded && !voicePlaybackCancelled) { voicePlayingMessageId = 0; walkieStatus = "PLAY FAILED"; renderDirty = true; }
    }
    synchronize();
    if (voiceUploadPending && !localReplayActive) uploadVoiceClip();
    if (syncOverride) { syncOverride = false; WiFi.disconnect(true, false); nextWifiAttempt = millis() + 60000; }
    vTaskDelay(pdMS_TO_TICKS(kSyncPollMs));
  }
}

void walkieTask(void *) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && timeKnown && initialSyncComplete) {
      if (!walkieInitialized) {
        walkieHeaders = "Authorization: Bearer " + deviceToken + "\r\n";
        walkieSocket.setExtraHeaders(walkieHeaders.c_str()); walkieSocket.onEvent(onWalkieEvent); walkieSocket.setReconnectInterval(2000);
        const String walkiePath = "/walkie?room=" + currentRoomId;
        walkieSocket.beginSSL(kApiHost, 443, walkiePath.c_str(), kTlsFingerprint); walkieInitialized = true;
      }
      walkieSocket.loop();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void drawHeader(const char *title) {
  auto &display = uiCanvas;
  display.fillRect(0, 0, 240, 20, TFT_DARKGREEN);
  display.setTextColor(TFT_WHITE, TFT_DARKGREEN); display.setTextSize(1); display.setCursor(5, 6); display.print(title);
  if (strcmp(title, "FAMLY") == 0) { display.setCursor(40, 6); display.print(easternClockText()); }
  if (!keyboardReady) { display.setTextColor(TFT_RED, TFT_DARKGREEN); display.setCursor(88, 6); display.print("K!"); }
  display.setCursor(106, 6); display.print(networkStatus.substring(0, 10));
  const String batteryText = batteryLevel < 0 ? String("?%") : String(batteryLevel) + "%";
  display.setTextSize(1.5f); display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  display.setCursor(237 - static_cast<int>(batteryText.length()) * 9, 4);
  display.print(batteryText); display.setTextSize(1);
  if (externalPowerDetected) {
    display.fillTriangle(199, 2, 194, 10, 199, 10, TFT_YELLOW);
    display.fillTriangle(197, 8, 203, 8, 196, 18, TFT_YELLOW);
  }
}

void drawChat() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("");
  int activeIndex = -1; for (int index = 0; index < static_cast<int>(rooms.size()); index++) if (rooms[index].id == currentRoomId) activeIndex = index;
  const int leftIndex = rooms.size() > 1 && activeIndex >= 0 ? (activeIndex - 1 + rooms.size()) % rooms.size() : -1;
  const int rightIndex = rooms.size() > 1 && activeIndex >= 0 ? (activeIndex + 1) % rooms.size() : -1;
  const bool leftNew = leftIndex >= 0 && rooms[leftIndex].latestMessageId > rooms[leftIndex].seenMessageId;
  const bool rightNew = rightIndex >= 0 && rooms[rightIndex].latestMessageId > rooms[rightIndex].seenMessageId;
  display.setTextSize(1); display.setTextColor(leftNew ? TFT_YELLOW : TFT_DARKGREY, TFT_DARKGREEN); display.setCursor(4, 6); display.print(leftNew ? "<*" : "< ");
  const String roomTitle = currentRoomName.substring(0, 10); display.setTextColor(TFT_WHITE, TFT_DARKGREEN); display.setCursor(50 - roomTitle.length() * 3, 6); display.print(roomTitle);
  display.setTextColor(rightNew ? TFT_YELLOW : TFT_DARKGREY, TFT_DARKGREEN); display.setCursor(91, 6); display.print(rightNew ? "*>" : " >");
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  const int end = std::max(0, static_cast<int>(messages.size()) - static_cast<int>(historyOffset));
  const int first = std::max(0, end - 10);
  std::vector<ChatLine> lines;
  lines.reserve(10);
  for (int index = first; index < end; index++) {
    const auto &message = messages[index];
    const bool mine = message.authorId == kDeviceId;
    const uint32_t colour = participantColour(message.authorId, message.authorName);
    if (!mine) lines.push_back({message.authorName.substring(0, 5), colour, false, true});
    if (message.replyToId > 0) lines.push_back({String("reply ") + message.replyAuthor.substring(0, 6) + ": " + message.replyBody.substring(0, 12), TFT_DARKGREY, mine, true});
    String text = message.deleted ? String("[deleted]") : message.voice ? String("[voice message]") : message.body;
    if (message.edited && !message.deleted) text += " [edit]";
    if (message.reactionCount > 0) text += " [+" + String(message.reactionCount) + "]";
    while (!text.isEmpty()) {
      int split = text.length() <= 25 ? text.length() : text.lastIndexOf(' ', 25);
      if (split < 6) split = std::min(25, static_cast<int>(text.length()));
      String line = text.substring(0, split);
      text = text.substring(split);
      while (text.startsWith(" ")) text.remove(0, 1);
      lines.push_back({line, colour, mine, false});
    }
  }
  int firstLine = static_cast<int>(lines.size());
  int usedHeight = 0;
  while (firstLine > 0) {
    const int lineHeight = lines[firstLine - 1].label ? 10 : 17;
    if (usedHeight + lineHeight > 82) break;
    usedHeight += lineHeight; firstLine--;
  }
  int y = 24;
  for (int index = firstLine; index < static_cast<int>(lines.size()); index++) {
    if (lines[index].label) {
      display.setFont(&fonts::Font0); display.setTextSize(1);
    } else {
      display.setFont(&fonts::Font2); display.setTextSize(1);
    }
    display.setTextColor(lines[index].colour, TFT_BLACK);
    const int x = lines[index].mine ? std::max(3, 237 - display.textWidth(lines[index].text)) : 3;
    display.setCursor(x, y); display.print(lines[index].text); y += lines[index].label ? 10 : 17;
  }
  xSemaphoreGive(stateMutex);
  display.setFont(&fonts::Font0);
  display.drawFastHLine(0, 106, 240, TFT_DARKGREY);
  display.setTextSize(1);
  display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(3, 112); display.print("> ");
  display.setTextColor(TFT_WHITE, TFT_BLACK); display.print(draft.substring(draft.length() > 34 ? draft.length() - 34 : 0));
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(3, 126);
  if (duelId > 0 && duelStatus == "active") display.print(duelChoiceLocked ? "DUEL LOCKED - OPEN MENU" : "DUEL READY - OPEN MENU");
  else if (duelId > 0 && duelStatus == "pending") display.print(duelChallengedByMe ? "DUEL WAITING - OPEN MENU" : "DUEL CHALLENGE - MENU");
  else if (duelId > 0 && duelTerminalStatus(duelStatus)) display.print("DUEL RESULT - OPEN MENU");
  else if (!typingNotice.isEmpty()) display.print(typingNotice.substring(0, 28));
  display.setCursor(198, 126); display.printf("%d/140", draft.length());
}

const char *kMenuItems[2][4] = {
  {"CHAT", "ROOMS", "DUELS", "VOICE MESSAGES"},
  {"SYNC NOW", "VOLUME", "NETWORKS", "STATUS"},
};
void drawMenu() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(menuPage==0?"MENU COMMS":"MENU DEVICE");
  display.setTextSize(1);
  for (int index = 0; index < 4; index++) {
    const int y = 25 + index * 20; const bool selected = index == menuSelection;
    display.fillRoundRect(6, y, 228, 17, 4, selected ? TFT_GREEN : TFT_DARKGREY);
    display.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_GREEN : TFT_DARKGREY);
    display.setCursor(13, y + 5); display.print(kMenuItems[menuPage][index]);
    if(menuPage==0&&index==2&&duelAttention){display.setCursor(220,y+5);display.print("*");}
  }
  display.setTextSize(1); display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 117);
  display.printf("< PAGE  %d/2  PAGE >   ENTER",menuPage+1);
}

void drawDuelArrow(int centerX,int centerY,int direction,uint32_t colour){
  auto &display=uiCanvas;
  if(direction==0)display.fillTriangle(centerX,centerY-4,centerX-4,centerY+3,centerX+4,centerY+3,colour);
  else if(direction==1)display.fillTriangle(centerX+4,centerY,centerX-3,centerY-4,centerX-3,centerY+4,colour);
  else if(direction==2)display.fillTriangle(centerX,centerY+4,centerX-4,centerY-3,centerX+4,centerY-3,colour);
  else display.fillTriangle(centerX-4,centerY,centerX+3,centerY-4,centerX+3,centerY+4,colour);
}

void drawDuelOption(int x,int y,int width,const char *before,const char *after,int direction,bool selected){
  auto &display=uiCanvas;const uint32_t background=selected?TFT_GREEN:TFT_DARKGREY;const uint32_t foreground=selected?TFT_BLACK:TFT_WHITE;
  display.fillRoundRect(x,y,width,18,5,background);display.setTextColor(foreground,background);display.setTextSize(1);
  const int contentWidth=(strlen(before)+strlen(after))*6+12;const int start=x+(width-contentWidth)/2;
  display.setCursor(start,y+5);display.print(before);const int arrowX=start+strlen(before)*6+5;drawDuelArrow(arrowX,y+9,direction,foreground);
  display.setCursor(arrowX+7,y+5);display.print(after);
}

void drawDuelReveal(){
  auto &display=uiCanvas;const uint32_t elapsed=millis()-duelRevealStartedAt;const int travel=std::min<int>(70,elapsed/10);const int shake=elapsed>720?((elapsed/kDuelAnimationFrameMs)%2?2:-2):0;
  display.setTextColor(participantColour("",duelChallenger),TFT_BLACK);display.setCursor(5+shake,31);display.print(duelChallenger.substring(0,10));
  display.setTextColor(participantColour("",duelOpponent),TFT_BLACK);const String opponent=duelOpponent.substring(0,10);display.setCursor(235-opponent.length()*6+shake,31);display.print(opponent);
  display.drawLine(28+travel,61,38+travel,61,TFT_YELLOW);display.fillCircle(40+travel,61,3,TFT_YELLOW);
  display.drawLine(212-travel,74,202-travel,74,TFT_MAGENTA);display.fillCircle(200-travel,74,3,TFT_MAGENTA);
  if(elapsed>650){display.drawCircle(120+shake,68,8+(elapsed/70)%9,TFT_WHITE);display.drawCircle(120-shake,68,18+(elapsed/70)%7,TFT_YELLOW);}
  display.setTextColor(TFT_DARKGREY,TFT_BLACK);display.setCursor(5,94);display.print(duelLastChallengerSpell.substring(0,15));
  const String spell=duelLastOpponentSpell.substring(0,15);display.setCursor(235-spell.length()*6,94);display.print(spell);
  if(elapsed>850){display.setTextColor(TFT_GREEN,TFT_BLACK);display.setCursor(84,109);display.print(duelLastRoundWinnerId.isEmpty()?"BLOCKED":duelLastRoundWinnerId==kDeviceId?"POINT YOU":"POINT THEM");}
}

void drawDuel(){
  auto &display=uiCanvas;display.fillScreen(TFT_BLACK);drawHeader("DUEL");display.setTextSize(1);
  if(duelId<=0){
    display.setTextColor(TFT_YELLOW,TFT_BLACK);display.setCursor(6,25);display.print(("CHALLENGE · "+currentRoomName).substring(0,36));
    if(duelOpponents.empty()){display.setTextColor(TFT_DARKGREY,TFT_BLACK);display.setCursor(38,64);display.print("NO OTHER MEMBERS");}
    else{const int first=std::max(0,duelOpponentSelection-2);for(int row=0;row<4&&first+row<static_cast<int>(duelOpponents.size());row++){const int index=first+row,y=39+row*17;const bool selected=index==duelOpponentSelection;display.fillRoundRect(6,y,228,15,4,selected?TFT_GREEN:TFT_DARKGREY);display.setTextColor(selected?TFT_BLACK:participantColour(duelOpponents[index].id,duelOpponents[index].name),selected?TFT_GREEN:TFT_DARKGREY);display.setCursor(12,y+4);display.printf("%-20s %s",duelOpponents[index].name.substring(0,20).c_str(),duelOpponents[index].status.substring(0,7).c_str());}}
    display.setTextColor(TFT_DARKGREY,TFT_BLACK);display.setCursor(6,123);display.print(duelChallengePending.isEmpty()?"UP/DOWN CHOOSE  ENTER DUEL":"SENDING CHALLENGE...");return;
  }
  display.setTextColor(TFT_WHITE,TFT_BLACK);display.setCursor(82,22);display.printf("%d  -  %d",duelChallengerScore,duelOpponentScore);
  if(duelStatus=="pending"){
    const bool pulse=(millis()/280)%2;display.drawRoundRect(8,37,224,56,10,pulse?TFT_MAGENTA:TFT_DARKGREY);
    display.setTextColor(participantColour("",duelChallenger),TFT_BLACK);display.setCursor(18,48);display.print(duelChallenger.substring(0,13));display.setTextColor(TFT_WHITE,TFT_BLACK);display.print("  VS  ");display.setTextColor(participantColour("",duelOpponent),TFT_BLACK);display.print(duelOpponent.substring(0,13));
    display.setTextColor(TFT_YELLOW,TFT_BLACK);display.setCursor(duelChallengedByMe?65:54,72);display.print(duelChallengedByMe?"WAITING FOR ANSWER":"YOU ARE CHALLENGED");
    display.setTextColor(TFT_DARKGREY,TFT_BLACK);display.setCursor(6,117);display.print(duelChallengedByMe?"DEL CANCEL        MENU BACK":"ENTER ACCEPT  DEL DECLINE");return;
  }
  const uint32_t countdownElapsed=duelCountdownStartedAt?millis()-duelCountdownStartedAt:kDuelCountdownMs;
  if(duelStatus=="active"&&countdownElapsed<kDuelCountdownMs){const int phase=countdownElapsed/300;const String word=phase<3?String(3-phase):String("DUEL!");display.setTextSize(4);display.setTextColor(phase<3?TFT_YELLOW:TFT_GREEN,TFT_BLACK);display.setCursor(120-word.length()*12,49);display.print(word);display.setTextSize(1);return;}
  if(duelRevealStartedAt&&millis()-duelRevealStartedAt<kDuelRevealMs){drawDuelReveal();return;}
  if(duelStatus=="active"){
    if(duelChoiceLocked){const int frame=(millis()/kDuelAnimationFrameMs)%12;display.setTextColor(TFT_GREEN,TFT_BLACK);display.setTextSize(2);display.setCursor(48,52);display.print("SPELL LOCKED");display.setTextSize(1);display.setTextColor(TFT_DARKGREY,TFT_BLACK);display.setCursor(59,80);display.print("WAITING FOR RIVAL");display.fillCircle(120+(frame<6?frame:12-frame)*3-9,101,3,TFT_MAGENTA);display.setCursor(6,123);display.print("MENU BACK · RESULT AUTOPLAYS");return;}
    drawDuelOption(84,27,72,"PRO","TEGO",0,duelSpellSelection==0);
    drawDuelOption(4,57,83,"LEVI","CORP",3,duelSpellSelection==3);
    drawDuelOption(153,57,82,"SEC","TUM",1,duelSpellSelection==1);
    drawDuelOption(82,87,78,"LANG","LOCK",2,duelSpellSelection==2);
    display.setTextColor(duelSpellSelection<0?TFT_DARKGREY:TFT_YELLOW,TFT_BLACK);display.setCursor(6,117);display.print(duelSpellSelection<0?"ARROWS CHOOSE · ENTER CAST":"ENTER CAST · ARROWS CHANGE");return;
  }
  const bool complete=duelStatus=="complete";const bool won=complete&&duelWinnerId==kDeviceId;const uint32_t resultColour=complete?(won?TFT_GREEN:TFT_RED):TFT_YELLOW;
  if(millis()-duelTerminalStartedAt<1600){for(int i=0;i<14;i++){const int px=(i*37+(millis()/35)*(i%3+1))%238;const int py=27+(i*19)%70;display.fillCircle(px,py,1,i%2?resultColour:TFT_WHITE);}}
  display.setTextSize(3);display.setTextColor(resultColour,TFT_BLACK);const String result=complete?(won?"VICTORY":"DEFEAT"):duelStatus;display.setCursor(120-result.length()*9,48);display.print(result);display.setTextSize(1);display.setTextColor(TFT_WHITE,TFT_BLACK);display.setCursor(82,84);display.printf("FINAL %d - %d",duelChallengerScore,duelOpponentScore);display.setTextColor(TFT_DARKGREY,TFT_BLACK);display.setCursor(6,120);display.print("ENTER DONE          MENU BACK");
}

void drawRooms() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("ROOMS"); display.setTextSize(1);
  const int start = std::max(0, roomSelection - 5);
  for (int index = start; index < static_cast<int>(rooms.size()) && index < start + 6; index++) {
    const int y = 22 + (index - start) * 15; const bool selected = index == roomSelection;
    display.fillRoundRect(6, y, 228, 13, 3, selected ? TFT_GREEN : TFT_DARKGREY);
    display.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_GREEN : TFT_DARKGREY); display.setCursor(12, y + 3);
    display.print(rooms[index].name.substring(0, 34));
  }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print("UP/DOWN CHOOSE    ENTER OPEN");
}

void drawWalkie() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("VOICE");
  if (voiceRecording) {
    display.setTextSize(2); display.setTextColor(TFT_RED, TFT_BLACK); display.setCursor(12, 32); display.print("RECORDING");
    display.setTextSize(1); display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(12, 62);
    display.printf("%u.%us / 30s", voiceCapturedTotal / kVoiceSampleRate,
                   (voiceCapturedTotal % kVoiceSampleRate) * 10 / kVoiceSampleRate);
  } else {
    const auto inbox = voiceInbox();
    if (inbox.empty()) { display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(12, 42); display.print("No voice messages yet"); }
    else {
      voiceInboxSelection = std::min(voiceInboxSelection, static_cast<int>(inbox.size()) - 1);
      const int first = std::max(0, voiceInboxSelection - 3);
      for (int row = 0; row < 4 && first + row < static_cast<int>(inbox.size()); row++) {
        const int index = first + row; const bool selected = index == voiceInboxSelection; const int y = 23 + row * 17;
        display.fillRoundRect(5, y, 230, 15, 3, selected ? TFT_GREEN : TFT_DARKGREY);
        display.setTextColor(selected ? TFT_BLACK : participantColour(inbox[index].authorId, inbox[index].authorName), selected ? TFT_GREEN : TFT_DARKGREY);
        display.setCursor(9, y + 4); display.printf("%-5s  voice #%lld", inbox[index].authorName.substring(0, 5).c_str(), inbox[index].id);
      }
    }
    display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(7, 94); display.print(walkieStatus.substring(0, 35));
  }
  display.setTextSize(1); display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.setCursor(6, 111); display.print("UP/DOWN CHOOSE  ENTER PLAY/STOP");
  display.setCursor(6, 123); display.print("HOLD SPACE RECORD   LEFT MENU");
}

void drawVolume() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("VOLUME");
  display.setTextSize(2); display.setTextColor(TFT_YELLOW, TFT_BLACK);
  display.setCursor(72, 39); display.print(kVolumeNames[volumeLevel]);
  for (int index = 0; index < 5; index++) {
    const int x = 20 + index * 42;
    display.fillRoundRect(x, 72, 30, 20, 4, index <= volumeLevel && volumeLevel > 0 ? TFT_GREEN : TFT_DARKGREY);
  }
  display.setTextSize(1); display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.setCursor(6, 116); display.print("LEFT/RIGHT CHANGE   OK BACK");
}

void drawStatus() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("STATUS"); display.setTextSize(1);
  display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 29); display.printf("NODE      %s\n", kDeviceName);
  display.printf("WIFI      %s\n", currentSsid.isEmpty() ? "offline" : currentSsid.c_str());
  display.printf("STATE     %s\n", networkStatus.c_str()); display.printf("TIME      %s\n", timeKnown ? "known" : "unknown");
  display.printf("BATTERY   %d%%  %d mV\n", batteryLevel, batteryVoltageMv);
  display.printf("POWER     %s\n", externalPowerDetected ? "cable detected" : "not detected");
  display.printf("SD/KEY    %s / %s\n", sdReady ? "ready" : "missing", keyboardReady ? "ready" : "fault");
  display.printf("HTTP/HEAP %d / %u\n", lastHttpStatus, ESP.getFreeHeap());
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print("LEFT / OK  BACK");
}

void drawNetworks() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("NETWORKS"); display.setTextSize(1);
  if (scannedNetworks.empty()) {
    display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 38); display.print("No networks found");
  } else {
    const int first = std::max(0, networkSelection - 3);
    for (int row = 0; row < 6 && first + row < static_cast<int>(scannedNetworks.size()); row++) {
      const int index = first + row; const auto &network = scannedNetworks[index]; const int y = 23 + row * 16;
      const bool selected = index == networkSelection;
      display.fillRect(3, y, 234, 14, selected ? TFT_GREEN : TFT_BLACK);
      display.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_GREEN : TFT_BLACK);
      display.setCursor(6, y + 3); display.printf("%c %-22s %3d", network.open ? ' ' : '*', network.ssid.substring(0, 22).c_str(), network.rssi);
    }
  }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 121); display.print("ARROWS  OK JOIN  LEFT BACK");
}

void drawNetworkPassword() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("JOIN WIFI"); display.setTextSize(1);
  display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(6, 28); display.print(selectedSsid.substring(0, 28));
  display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 52); display.print("PASSWORD");
  display.fillRoundRect(5, 65, 230, 24, 4, TFT_DARKGREY);
  display.setTextColor(TFT_WHITE, TFT_DARKGREY); display.setCursor(10, 73);
  display.print(networkPassword.substring(networkPassword.length() > 28 ? networkPassword.length() - 28 : 0));
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 104); display.print(networkStatus.substring(0, 34));
  display.setCursor(6, 121); display.print("TYPE  ENTER JOIN  LEFT CANCEL");
}

void render() {
  if (screenMode == ScreenMode::Chat) drawChat(); else if (screenMode == ScreenMode::Menu) drawMenu(); else if (screenMode == ScreenMode::Rooms) drawRooms();
  else if (screenMode == ScreenMode::Duel) drawDuel();
  else if (screenMode == ScreenMode::Volume) drawVolume();
  else if (screenMode == ScreenMode::Walkie) drawWalkie();
  else if (screenMode == ScreenMode::Status) drawStatus();
  else if (screenMode == ScreenMode::NetworkPassword) drawNetworkPassword(); else drawNetworks();
  if (uiCanvasReady) uiCanvas.pushSprite(0, 0);
  renderDirty = false; lastRenderAt = millis();
}

void serviceDuelAnimations(){
  const uint32_t now=millis();
  if(duelCountdownStartedAt&&now-duelCountdownStartedAt>=kDuelCountdownMs)duelCountdownStartedAt=0;
  if(duelRevealStartedAt&&now-duelRevealStartedAt>=kDuelRevealMs){duelRevealStartedAt=0;if(duelResultSfxPending){duelResultSfxPending=false;startDuelSfx(duelStatus=="complete"?(duelWinnerId==kDeviceId?DuelSfxCue::Victory:DuelSfxCue::Defeat):DuelSfxCue::Neutral);}}
  if(screenMode!=ScreenMode::Duel||now-lastDuelAnimationAt<kDuelAnimationFrameMs)return;
  const bool animated=duelStatus=="pending"||(duelStatus=="active"&&(duelChoiceLocked||duelCountdownStartedAt||duelRevealStartedAt))||(duelTerminalStatus(duelStatus)&&now-duelTerminalStartedAt<1600);
  if(animated){lastDuelAnimationAt=now;renderDirty=true;}
}

bool containsKey(const Keyboard_Class::KeysState &keys, char wanted) {
  return std::find(keys.word.begin(), keys.word.end(), wanted) != keys.word.end();
}

bool physicalKeyAt(uint8_t column, uint8_t row) {
  for (const auto &key : M5Cardputer.Keyboard.keyList()) {
    if (key.x == column && key.y == row) return true;
  }
  return false;
}

bool navUp() { return physicalKeyAt(11, 2); }
bool navLeft() { return physicalKeyAt(10, 3); }
bool navDown() { return physicalKeyAt(11, 3); }
bool navRight() { return physicalKeyAt(12, 3); }

void sendDraft() {
  draft.trim(); if (draft.isEmpty()) return;
  if (draft == "/like") {
    xSemaphoreTake(stateMutex, portMAX_DELAY); for(auto item=messages.rbegin();item!=messages.rend();++item)if(item->authorId!=kDeviceId&&!item->deleted){reactionPendingMessageId=item->id;break;} xSemaphoreGive(stateMutex);
    draft=""; typingState=false;typingDirty=true;renderDirty=true;return;
  }
  if (millis()-lastDraftQueuedAt<1000) { networkStatus="SLOW DOWN"; renderDirty=true; return; }
  ChatMessage message; message.roomId = currentRoomId; message.clientId = nextClientId(); message.authorId = kDeviceId; message.authorName = kDeviceName;
  if(draft.startsWith("/r ")){xSemaphoreTake(stateMutex,portMAX_DELAY);for(auto item=messages.rbegin();item!=messages.rend();++item)if(item->authorId!=kDeviceId&&!item->deleted){message.replyToId=item->id;message.replyAuthor=item->authorName;message.replyBody=item->body;break;}xSemaphoreGive(stateMutex);draft.remove(0,3);draft.trim();if(draft.isEmpty())return;}
  message.body = draft; message.createdAt = time(nullptr) * 1000LL; message.queued = true; message.state = "queued";
  xSemaphoreTake(stateMutex, portMAX_DELAY); messages.push_back(message); trimHistory(); saveHistoryLocked(); xSemaphoreGive(stateMutex);
  if (kEspNowEnabled && meshReady) {
    static const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
    EspNowPacket packet{}; packet.magic = kEspNowMagic; packet.type = static_cast<uint8_t>(EspNowType::Text); packet.sequence = ++espNowSequence;
    strncpy(packet.senderId, kDeviceId, sizeof(packet.senderId) - 1); strncpy(packet.senderName, kDeviceName, sizeof(packet.senderName) - 1);
    strncpy(packet.clientId, message.clientId.c_str(), sizeof(packet.clientId) - 1);
    const size_t length = std::min<size_t>(message.body.length(), sizeof(packet.payload));
    if (encryptMeshPacket(packet, reinterpret_cast<const uint8_t *>(message.body.c_str()), length))
      esp_now_send(broadcast, reinterpret_cast<uint8_t *>(&packet), offsetof(EspNowPacket, payload) + packet.payloadLength);
  }
  draft = ""; lastDraftQueuedAt=millis(); typingState=false;typingDirty=true;historyOffset = 0; renderDirty = true;
}

void scanForNetworks() {
  manualWifiMode = true; networkStatus = "SCANNING"; scannedNetworks.clear(); networkSelection = 0;
  WiFi.disconnect(false, false); delay(100);
  const int count = WiFi.scanNetworks(false, true);
  for (int index = 0; index < count; index++) {
    const String ssid = WiFi.SSID(index); if (ssid.isEmpty()) continue;
    auto existing = std::find_if(scannedNetworks.begin(), scannedNetworks.end(),
                                 [&](const ScannedNetwork &item) { return item.ssid == ssid; });
    const ScannedNetwork candidate{ssid, WiFi.RSSI(index), WiFi.encryptionType(index) == WIFI_AUTH_OPEN};
    if (existing == scannedNetworks.end()) scannedNetworks.push_back(candidate);
    else if (candidate.rssi > existing->rssi) *existing = candidate;
  }
  WiFi.scanDelete();
  std::sort(scannedNetworks.begin(), scannedNetworks.end(),
            [](const ScannedNetwork &left, const ScannedNetwork &right) { return left.rssi > right.rssi; });
  networkStatus = scannedNetworks.empty() ? "NO NETWORKS" : "SELECT NETWORK"; renderDirty = true;
}

void selectNetwork() {
  if (scannedNetworks.empty()) return;
  selectedSsid = scannedNetworks[networkSelection].ssid; networkPassword = "";
  auto saved = std::find_if(wifiProfiles.begin(), wifiProfiles.end(),
                            [&](const WifiProfile &profile) { return profile.ssid == selectedSsid; });
  if (saved != wifiProfiles.end()) networkPassword = saved->password;
  screenMode = ScreenMode::NetworkPassword; networkStatus = "TYPE PASSWORD"; renderDirty = true;
}

void joinSelectedNetwork() {
  networkStatus = "CONNECTING"; renderDirty = true; render();
  WiFi.disconnect(false, false);
  WiFi.begin(selectedSsid.c_str(), networkPassword.c_str());
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) delay(100);
  if (WiFi.status() != WL_CONNECTED) { networkStatus = "FAILED - CHECK PASSWORD"; renderDirty = true; return; }
  currentSsid = WiFi.SSID(); rememberWifi(selectedSsid, networkPassword);
  networkStatus = "SAVED + CONNECTED"; screenMode = ScreenMode::Networks; manualWifiMode = false; renderDirty = true;
}

void openSelectedMenuItem() {
  if(menuPage==0){
    if(menuSelection==0)screenMode=ScreenMode::Chat;
    else if(menuSelection==1)screenMode=ScreenMode::Rooms;
    else if(menuSelection==2){screenMode=ScreenMode::Duel;duelAttention=false;if(duelId<=0)duelSpellSelection=-1;}
    else{screenMode=ScreenMode::Walkie;walkieStatus="READY";}
  }else{
    if(menuSelection==0){syncOverride=true;networkStatus="SYNC REQUESTED";screenMode=ScreenMode::Chat;}
    else if(menuSelection==1)screenMode=ScreenMode::Volume;
    else if(menuSelection==2){screenMode=ScreenMode::Networks;scanForNetworks();}
    else screenMode=ScreenMode::Status;
  }
  renderDirty = true;
}

void selectRoom(int next) {
  if (next < 0 || next >= static_cast<int>(rooms.size()) || rooms[next].id == currentRoomId) return;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  saveHistoryLocked();
  currentRoomId = rooms[next].id; currentRoomName = rooms[next].name; roomSelection = next;
  messages.clear(); lastServerId = 0; lastReceiptAt = 0; historyOffset = 0; syncOverride = true;
  updateDuel(JsonVariantConst());duelOpponents.clear();duelChallengePending="";duelActionPending="";duelSpellPending="";
  loadHistory();
  xSemaphoreGive(stateMutex);
  walkieSocket.disconnect(); walkieInitialized = false; networkStatus = "SWITCHING"; renderDirty = true;
}

void switchRoom(int direction) {
  if (rooms.size() < 2) return;
  int active = 0; for (int index = 0; index < static_cast<int>(rooms.size()); index++) if (rooms[index].id == currentRoomId) active = index;
  selectRoom((active + direction + static_cast<int>(rooms.size())) % static_cast<int>(rooms.size()));
}

void handleKeyboard() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
  const auto keys = M5Cardputer.Keyboard.keysState();
  // Shift+/ is '?', Shift+. is '>', etc. Modifier chords are text input,
  // never navigation, even when the printed arrow shares that physical key.
  const bool navigationChord = !(keys.shift || keys.ctrl || keys.alt || keys.fn || keys.opt);
  const bool goUp = navigationChord && navUp();
  const bool goDown = navigationChord && navDown();
  const bool goLeft = navigationChord && navLeft();
  const bool goRight = navigationChord && navRight();
  if (screenMode == ScreenMode::Menu) {
    if (goUp) { menuSelection = (menuSelection + 3) % 4; playNextTone(); }
    else if (goDown) { menuSelection = (menuSelection + 1) % 4; playNextTone(); }
    else if (goLeft) { menuPage = (menuPage + 1) % 2; playNextTone(); }
    else if (goRight) { menuPage = (menuPage + 1) % 2; playNextTone(); }
    else if (keys.enter) { openSelectedMenuItem(); playNextTone(); }
    renderDirty = true; return;
  }
  if(screenMode==ScreenMode::Duel){
    if(duelId<=0){
      if(goUp&&duelOpponentSelection>0){duelOpponentSelection--;playNextTone();}
      else if(goDown&&duelOpponentSelection+1<static_cast<int>(duelOpponents.size())){duelOpponentSelection++;playNextTone();}
      else if(keys.enter&&!duelOpponents.empty()&&duelChallengePending.isEmpty()){duelChallengePending=duelOpponents[duelOpponentSelection].id;networkStatus="CHALLENGING";startDuelSfx(DuelSfxCue::Challenge);}
    }else if(duelStatus=="pending"){
      if(keys.enter&&duelCanAccept&&duelActionPending.isEmpty()){duelActionPending="accept";networkStatus="ACCEPTING";startDuelSfx(DuelSfxCue::Countdown);}
      else if(keys.del&&duelActionPending.isEmpty()&&(duelCanCancel||duelCanDecline)){duelActionPending=duelCanCancel?"cancel":"decline";networkStatus=duelCanCancel?"CANCELLING":"DECLINING";startDuelSfx(DuelSfxCue::Neutral);}
    }else if(duelStatus=="active"&&!duelChoiceLocked&&!duelCountdownStartedAt&&!duelRevealStartedAt){
      int next=-1;if(goUp)next=0;else if(goRight)next=1;else if(goDown)next=2;else if(goLeft)next=3;
      if(next>=0){duelSpellSelection=next;startDuelSfx(DuelSfxCue::Select);}
      else if(keys.enter&&duelSpellSelection>=0&&duelSpellPending.isEmpty()){
        static const char *spells[]={"protego","sectumsempra","langlock","levicorpus"};duelSpellPending=spells[duelSpellSelection];duelChoiceLocked=true;networkStatus="SPELL LOCKED";startDuelSfx(DuelSfxCue::Lock);
      }
    }else if(duelTerminalStatus(duelStatus)&&keys.enter&&duelActionPending.isEmpty()){duelActionPending="acknowledge";networkStatus="DUEL CLOSED";startDuelSfx(DuelSfxCue::Neutral);}
    renderDirty=true;return;
  }
  if (screenMode == ScreenMode::Rooms) {
    if (goUp && roomSelection > 0) roomSelection--;
    else if (goDown && roomSelection + 1 < static_cast<int>(rooms.size())) roomSelection++;
    else if (goLeft) screenMode = ScreenMode::Menu;
    else if ((goRight || keys.enter) && !rooms.empty()) { selectRoom(roomSelection); screenMode=ScreenMode::Chat; }
    playNextTone(); renderDirty=true; return;
  }
  if (screenMode == ScreenMode::Volume) {
    if (goLeft && volumeLevel > 0) { volumeLevel--; saveVolume(); playNextTone(); }
    else if (goRight && volumeLevel < 4) { volumeLevel++; saveVolume(); playNextTone(); }
    else if (keys.enter) screenMode = ScreenMode::Menu;
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::Walkie) {
    if (goLeft) { screenMode = ScreenMode::Menu; playNextTone(); }
    else if (goUp && voiceInboxSelection > 0) { voiceInboxSelection--; playNextTone(); }
    else if (goDown) { const auto inbox = voiceInbox(); if (voiceInboxSelection + 1 < static_cast<int>(inbox.size())) voiceInboxSelection++; playNextTone(); }
    else if (keys.enter && !voiceRecording) playSelectedVoiceMessage();
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::Networks) {
    if (goUp && networkSelection > 0) { networkSelection--; playNextTone(); }
    else if (goDown && networkSelection + 1 < static_cast<int>(scannedNetworks.size())) { networkSelection++; playNextTone(); }
    else if (goLeft) { manualWifiMode = false; screenMode = ScreenMode::Menu; playNextTone(); }
    else if (goRight || keys.enter) { selectNetwork(); playNextTone(); }
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::NetworkPassword) {
    if (goLeft) { screenMode = ScreenMode::Networks; networkPassword = ""; playNextTone(); renderDirty = true; return; }
    playNextTone();
    if (keys.enter) { joinSelectedNetwork(); return; }
    if (keys.del) { if (!networkPassword.isEmpty()) networkPassword.remove(networkPassword.length() - 1); }
    if (keys.space && networkPassword.length() < 63) networkPassword += ' ';
    for (const auto character : keys.word) if (networkPassword.length() < 63 && character >= 0x20 && character <= 0x7e) networkPassword += character;
    renderDirty = true; return;
  }
  if (screenMode != ScreenMode::Chat) {
    if (goLeft || keys.enter) { screenMode = ScreenMode::Menu; playNextTone(); renderDirty = true; }
    return;
  }
  if (goUp) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    const size_t maximum = messages.size() > 5 ? messages.size() - 5 : 0;
    historyOffset = std::min(maximum, historyOffset + 1);
    xSemaphoreGive(stateMutex); playNextTone(); renderDirty = true; return;
  }
  if (goDown) { if (historyOffset > 0) historyOffset--; playNextTone(); renderDirty = true; return; }
  if (goLeft) { switchRoom(-1); playNextTone(); return; }
  if (goRight) { switchRoom(1); playNextTone(); return; }
  playNextTone();
  if (keys.enter) { sendDraft(); return; }
  if (keys.del) { if (!draft.isEmpty()) draft.remove(draft.length() - 1); typingState=!draft.isEmpty();typingDirty=true;typingChangedAt=millis();renderDirty = true; return; }
  if (keys.space && draft.length() < kMessageLimit) draft += ' ';
  for (const auto character : keys.word) if (draft.length() < kMessageLimit && character >= 0x20 && character <= 0x7e) draft += character;
  if(!draft.isEmpty()){typingState=true;typingDirty=true;typingChangedAt=millis();}
  renderDirty = true;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  auto config = M5.config(); config.output_power = true;
  M5Cardputer.begin(config, true); M5Cardputer.Display.setRotation(1); M5Cardputer.Display.setTextWrap(false);
  loadConfiguration();
  seedBuildTime();
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  sampleBattery(true);
  drawBootSplash();
  uiCanvas.setPsram(false); uiCanvas.setColorDepth(16);
  uiCanvasReady = uiCanvas.createSprite(240, 135) != nullptr; uiCanvas.setTextWrap(false);
  stateMutex = xSemaphoreCreateMutex(); bootNonce = esp_random();
  // Cardputer ADV's explicit SD bus keeps GPIO11 free for the TCA8418
  // keyboard interrupt. Default ESP32-S3 SPI pins would disable all keys.
  SPI.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
  sdReady = SD.begin(kSdCsPin, SPI, 25000000); loadHistory();
  keyboardReady = M5Cardputer.In_I2C.scanID(0x34, 400000);
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false); WiFi.persistent(false); WiFi.disconnect(false, false);
  if (kEspNowEnabled) initializeEspNow();
  xTaskCreatePinnedToCore(networkTask, "supachat-net", 16384, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(walkieTask, "supachat-ws", 12288, nullptr, 1, nullptr, 1);
  showBootSplash();
  render();
  Serial.printf("SUPACHAT ready node=%s wifi_profiles=%u sd=%d keyboard=%d board=%d\n", kDeviceId,
                wifiProfiles.size(), sdReady, keyboardReady, static_cast<int>(M5.getBoard()));
}

void loop() {
  M5Cardputer.update(); sampleBattery(); serviceClockRender(); handleKeyboard(); captureVoice(); serviceAudioPlayback(); serviceDuelSfx(); serviceMessageNotification(); serviceDuelAnimations();
  if(typingState&&millis()-typingChangedAt>2000){typingState=false;typingDirty=true;}
  if (screenMode == ScreenMode::Walkie) {
    const bool spacePressed = M5Cardputer.Keyboard.isPressed() && M5Cardputer.Keyboard.keysState().space;
    if (spacePressed) {
      spaceReleaseStartedAt = 0;
      if (!spacePttHeld) { spacePttHeld = true; startVoiceRecording(); }
    } else if (spacePttHeld) {
      const uint32_t now = millis();
      if (spaceReleaseStartedAt == 0) spaceReleaseStartedAt = now;
      else if (now - spaceReleaseStartedAt >= kPttReleaseDebounceMs) {
        spacePttHeld = false; spaceReleaseStartedAt = 0; stopVoiceRecording();
      }
    }
    if (M5Cardputer.BtnA.wasClicked()) { screenMode = ScreenMode::Menu; renderDirty = true; }
  } else if (M5Cardputer.BtnA.wasClicked()) {
    playNextTone(); screenMode = screenMode == ScreenMode::Menu ? ScreenMode::Chat : ScreenMode::Menu; renderDirty = true;
  }
  if (screenMode != ScreenMode::Walkie && spacePttHeld) { spacePttHeld = false; spaceReleaseStartedAt = 0; stopVoiceRecording(); }
  if (kEspNowEnabled && millis() - lastEspNowBeaconAt >= kEspNowBeaconMs) sendEspNowBeacon();
  if (renderDirty && millis() - lastRenderAt >= kRenderIntervalMs) render();
  delay(2);
}
