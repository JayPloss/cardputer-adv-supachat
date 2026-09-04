#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include "changelog.h"
#include "fox_finding.h"
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
constexpr char kApiBase[] = "https://supachat.net";
constexpr char kApiHost[] = "supachat.net";
// SHA-256 fingerprint for supachat.net's current leaf certificate.
// Renew this pin when Caddy rotates the certificate.
constexpr char kTlsFingerprint[] = "E7 09 64 D3 D6 B2 1D 03 F9 0E 81 59 6C FA 37 28 6C 69 37 5A AB C6 8A 0F DB C1 6D A0 87 9E 8F 15";
constexpr char kDeviceName[] = SUPACHAT_DEVICE_NAME;
constexpr char kDeviceId[] = SUPACHAT_DEVICE_ID;
#if defined(SUPACHAT_LANGUAGE_FR)
constexpr bool kBuildFrenchDefault = true;
#else
constexpr bool kBuildFrenchDefault = false;
#endif
bool frenchUi = kBuildFrenchDefault;
String languageOverride = "auto";
const char *uiText(const char *english, const char *french) { return frenchUi ? french : english; }
constexpr char kFirmwareVersion[] = "v0.53";
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
constexpr uint32_t kPowerIdleAfterInputMs = 5000;
constexpr size_t kPowerTrendSamples = 6;
constexpr int kPowerTrendMinimumRiseMv = 15;
constexpr int kPowerTrendNoiseToleranceMv = 3;
constexpr uint32_t kBatteryFallStepMs = 90000;
constexpr uint32_t kBatteryPersistMs = 300000;
constexpr uint32_t kChargePlotSampleMs = 300000;
constexpr size_t kChargePlotCapacity = 144;
constexpr uint32_t kChargeScreenTimeoutMs = 20000;
constexpr uint8_t kChargeBrightness = 24;
constexpr uint8_t kChargeDimBrightness = 0;
constexpr int kChargePlotMinMv = 3200;
constexpr int kChargePlotMaxMv = 4300;
constexpr int kChargeTrendThresholdMv = 12;
constexpr char kChargeLogPath[] = "/supachat-charge.csv";
constexpr int kSyncBatchLimit = 20;
constexpr int kHistoryPageLimit = 20;
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
constexpr bool kEspNowEnabled = true;
constexpr uint8_t kEspNowFallbackChannel = 1;
constexpr uint32_t kFoxPlaceRescanMs = 30000;
#if defined(SUPACHAT_DEVICE_EMMANUELLE) || defined(SUPACHAT_DEVICE_NAOMIE) || defined(SUPACHAT_DEVICE_ANDREW)
constexpr char kDefaultRoomId[] = "wolfpack";
constexpr char kDefaultRoomName[] = "Wolfpack";
#else
constexpr char kDefaultRoomId[] = "family";
constexpr char kDefaultRoomName[] = "Family";
#endif
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
  int64_t replyToId = 0;
  String replyAuthor;
  String replyBody;
};

struct ChatLine {
  String text;
  String sender;
  uint32_t colour;
  bool mine;
  bool showSender;
  bool selected;
};
struct ChatRoom { String id; String name; String groupId; String groupName; String defaultLanguage; int64_t latestMessageId; int64_t seenMessageId; };
enum class ChargeTrend : uint8_t { Waiting, Rising, Flat, Falling, Unstable };
struct ChargePlotPoint {
  uint32_t elapsedSeconds = 0;
  uint16_t filteredMv = 0;
  uint8_t estimatedPercent = 0;
  ChargeTrend trend = ChargeTrend::Waiting;
};
void applyEffectiveLanguage();
void setLocalOnlyMode(bool enabled);

uint32_t participantColour(const String &authorId, const String &authorName = "") {
  String identity = authorId + " " + authorName; identity.toLowerCase();
  if (identity.indexOf("albie") >= 0) return 0x7DD3FC;
  if (identity.indexOf("juju") >= 0 || identity.indexOf("julien") >= 0) return 0xFFAD5C;
  if (identity.indexOf("papa") >= 0 || identity.indexOf("jay") >= 0) return 0xA7F070;
  if (identity.indexOf("theo") >= 0 || identity.indexOf("théo") >= 0) return 0xC4A7FF;
  if (identity.indexOf("josee") >= 0 || identity.indexOf("josée") >= 0) return 0xFF8FB8;
  if (identity.indexOf("emman") >= 0) return 0x60E1E0;
  if (identity.indexOf("andrew") >= 0) return 0xF4D35E;
  if (identity.indexOf("naomie") >= 0) return 0xFF6B6B;
  return TFT_WHITE;
}

enum class ScreenMode { Chat, Menu, Rooms, Volume, Language, EmojiRecipes, Changelog, VoiceMessages, Walkie, Status, Networks, NetworkPassword, ChargingConfirm, Charging, FoxFinding };

Preferences preferences;
M5Canvas uiCanvas(&M5Cardputer.Display);
WebSocketsClient walkieSocket;
std::vector<WifiProfile> wifiProfiles;
std::vector<ScannedNetwork> scannedNetworks;
std::vector<ChatMessage> messages;
std::vector<ChatRoom> rooms;
String currentRoomId = kDefaultRoomId;
String currentRoomName = kDefaultRoomName;
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
bool frenchGravePending = false;
String voiceClientId;
ScreenMode screenMode = ScreenMode::Chat;
bool sdReady = false;
bool renderDirty = true;
bool syncOverride = false;
bool timeKnown = false;
bool ntpAttempted = false;
bool timeSyncedByNtp = false;
bool initialSyncComplete = false;
int64_t historyBeforeId = 0;
size_t historyHydratedCount = 0;
bool keyboardReady = false;
bool uiCanvasReady = false;
bool voiceRecording = false;
bool voiceClipReady = false;
bool voiceUploadPending = false;
bool retainCurrentVoice = false;
bool voiceLiveMode = false;
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
constexpr int kMenuPageCount = 3;
constexpr int kMenuItemsPerPage = 6;
int menuPage = 0;
int menuSelections[kMenuPageCount] = {0, 0, 0};
int changelogSelection = 0;
int changelogLineOffset = 0;
int statusPage = 0;
volatile bool localOnlyMode = false;
volatile bool wifiResumeRequested = false;
int roomSelection = 0;
int networkSelection = 0;
uint8_t volumeLevel = kDefaultVolumeLevel;
int64_t lastServerId = 0;
int64_t lastReceiptAt = 0;
int lastHttpStatus = 0;
String lastSyncError = "NONE";
String lastSyncDetail;
String lastSyncErrorRoom;
int lastSyncErrorHttpStatus = 0;
size_t lastSyncResponseBytes = 0;
uint32_t lastSyncErrorHeap = 0;
uint32_t lastSyncErrorAt = 0;
bool lastSyncErrorInitial = false;
int batteryLevel = -1;
int batteryVoltageMv = 0;
float batteryFilteredMv = 0.0f;
uint32_t lastBatterySampleAt = 0;
uint32_t lastBatteryAdjustAt = 0;
uint32_t lastBatteryPersistAt = 0;
uint32_t lastClockCheckAt = 0;
int lastClockMinute = -1;
int persistedBatteryLevel = -1;
uint32_t lastUserInputAt = 0;
std::array<int, kPowerTrendSamples> powerTrendVoltages{};
size_t powerTrendCount = 0;
bool externalPowerDetected = false;
volatile bool chargingModeActive = false;
bool chargingDimmed = false;
bool inputQuarantined = false;
bool espNowActive = false;
uint32_t chargeSessionStartedAt = 0;
uint32_t nextChargePlotAt = 0;
uint32_t chargeSessionId = 0;
int chargeLastLoggedPercent = -1;
ChargeTrend chargeTrend = ChargeTrend::Waiting;
std::array<ChargePlotPoint, kChargePlotCapacity> chargePlot{};
size_t chargePlotCount = 0;

enum class FoxState : uint8_t { Idle, Selecting, Requesting, Acquiring, Guiding, TargetActive, SignalLost };
struct FoxPeer {
  uint8_t mac[6]{};
  char id[6]{};
  char name[8]{};
  volatile int8_t rssi = -127;
  volatile uint32_t rssiAt = 0;
  uint32_t seenAt = 0;
};
struct __attribute__((packed)) FoxControl {
  char targetId[6]{};
  uint32_t sessionId = 0;
  uint8_t role = 0;
};
std::array<FoxPeer, supachat::fox::kMaxPeers> foxPeers{};
size_t foxPeerCount = 0;
int foxPeerSelection = 0;
FoxState foxState = FoxState::Idle;
uint32_t foxSessionId = 0;
uint32_t foxStartedAt = 0;
uint32_t foxLastPacketAt = 0;
uint32_t foxLastBeaconAt = 0;
uint32_t foxLastPlaceScanAt = 0;
char foxTargetId[6]{};
char foxTargetName[8]{};
uint8_t foxTargetMac[6]{};
volatile bool foxScanRequested = false;
volatile bool foxSendPlaceResult = false;
volatile bool foxAckRequested = false;
volatile bool foxEnterLocalOnlyRequested = false;
volatile bool foxRequestPlaceAfterScan = false;
volatile bool foxRestoreWifiRequested = false;
bool foxForcedLocalOnly = false;
uint32_t foxLastConsumedRssiAt = 0;
uint32_t foxPackInviteSession = 0;
uint32_t foxPackInviteAt = 0;
char foxPackInviteQuarryId[6]{};
char foxPackInviteQuarryName[8]{};
char foxPackInviteHunterName[8]{};
std::array<supachat::fox::PackEvidence, supachat::fox::kMaxPackHunters> foxPackEvidence{};
size_t foxPackEvidenceCount = 0;
uint32_t foxLastPackObservationAt = 0;
uint16_t foxPackSequence = 0;
bool foxJoinedPack = false;
supachat::fox::PlacePrint foxLocalPrint{};
supachat::fox::Match foxPlaceMatch{};
supachat::fox::RssiTrend foxRssiTrend;

void serviceMessageNotification();
uint32_t lastToneAt = 0;
uint32_t lastRenderAt = 0;
uint32_t bootNonce = 0;
uint32_t clientSequence = 0;
size_t songPosition = 0;
size_t messageNotificationPosition = 0;
size_t historyOffset = 0;
int emojiRecipeSelection = 0;
int64_t replyToMessageId = 0;
String replyToAuthor;
String replyToBody;
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
enum class EspNowType : uint8_t { Beacon = 1, Text = 2, Audio = 3, FoxBeacon = 4, FindStart = 5, FindAck = 6, PlacePrintRequest = 7, PlacePrintResult = 8, FindEnd = 9, PackObservation = 10 };
struct __attribute__((packed)) EspNowPacket {
  uint32_t magic;
  uint8_t type;
  uint16_t sequence;
  char senderId[6];
  char senderName[8];
  char roomId[9];
  char clientId[48];
  uint16_t payloadLength;
  uint8_t nonce[12];
  uint8_t tag[16];
  uint8_t payload[142];
};
static_assert(sizeof(EspNowPacket) <= ESP_NOW_MAX_DATA_LEN, "ESP-NOW packet exceeds transport limit");
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

FoxPeer *findFoxPeerByMac(const uint8_t *mac) {
  for (size_t index = 0; index < foxPeerCount; ++index)
    if (memcmp(foxPeers[index].mac, mac, 6) == 0) return &foxPeers[index];
  return nullptr;
}

FoxPeer *rememberFoxPeer(const uint8_t *mac, const char *id, const char *name) {
  FoxPeer *peer = findFoxPeerByMac(mac);
  if (!peer && foxPeerCount < foxPeers.size()) {
    peer = &foxPeers[foxPeerCount++];
    memcpy(peer->mac, mac, 6);
  }
  if (!peer) return nullptr;
  memset(peer->id, 0, sizeof(peer->id)); memset(peer->name, 0, sizeof(peer->name));
  strncpy(peer->id, id, sizeof(peer->id) - 1);
  strncpy(peer->name, name, sizeof(peer->name) - 1);
  peer->seenAt = millis();
  return peer;
}

void onFoxPromiscuousPacket(void *buffer, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT || !buffer) return;
  const auto *packet = static_cast<const wifi_promiscuous_pkt_t *>(buffer);
  if (packet->rx_ctrl.sig_len < 24) return;
  const uint8_t *frame = packet->payload;
  if ((frame[0] & 0xfc) != 0xd0) return;  // ESP-NOW uses vendor-specific action frames.
  const uint8_t *source = frame + 10;
  FoxPeer *peer = findFoxPeerByMac(source);
  if (!peer) return;
  peer->rssi = packet->rx_ctrl.rssi;
  peer->rssiAt = millis();
}

bool sendFoxPacket(EspNowType type, const void *payload, size_t length) {
  if (!espNowActive || !meshReady || length > 142) return false;
  static const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
  EspNowPacket packet{};
  packet.magic = kEspNowMagic; packet.type = static_cast<uint8_t>(type); packet.sequence = ++espNowSequence;
  strncpy(packet.senderId, kDeviceId, sizeof(packet.senderId) - 1);
  strncpy(packet.senderName, kDeviceName, sizeof(packet.senderName) - 1);
  strncpy(packet.roomId, currentRoomId.c_str(), sizeof(packet.roomId) - 1);
  if (!encryptMeshPacket(packet, static_cast<const uint8_t *>(payload), length)) return false;
  return esp_now_send(broadcast, reinterpret_cast<uint8_t *>(&packet), offsetof(EspNowPacket, payload) + packet.payloadLength) == ESP_OK;
}

void sendFoxControl(EspNowType type) {
  FoxControl control{};
  strncpy(control.targetId, foxTargetId, sizeof(control.targetId) - 1);
  control.sessionId = foxSessionId;
  control.role = foxState == FoxState::TargetActive ? 1 : 0;
  sendFoxPacket(type, &control, sizeof(control));
}

FoxPeer *findFoxPeerById(const char *id) {
  for (size_t index = 0; index < foxPeerCount; ++index) if (strncmp(foxPeers[index].id, id, sizeof(foxPeers[index].id)) == 0) return &foxPeers[index];
  return nullptr;
}

void recordPackEvidence(const char *hunterId, const supachat::fox::PackObservation &observation) {
  supachat::fox::PackEvidence *evidence = nullptr;
  for (size_t index = 0; index < foxPackEvidenceCount; ++index)
    if (strncmp(foxPackEvidence[index].hunterId, hunterId, sizeof(foxPackEvidence[index].hunterId)) == 0) evidence = &foxPackEvidence[index];
  if (!evidence && foxPackEvidenceCount < foxPackEvidence.size()) evidence = &foxPackEvidence[foxPackEvidenceCount++];
  if (!evidence) return;
  memset(evidence->hunterId, 0, sizeof(evidence->hunterId)); strncpy(evidence->hunterId, hunterId, sizeof(evidence->hunterId) - 1);
  evidence->directRssi = observation.directRssi; evidence->trendDelta = observation.trendDelta;
  evidence->placeSimilarity = observation.placeSimilarity; evidence->placeConfidence = observation.placeConfidence; evidence->receivedAt = millis();
}

void sendPackObservation() {
  if (foxState != FoxState::Guiding && foxState != FoxState::Acquiring && foxState != FoxState::SignalLost) return;
  supachat::fox::PackObservation observation{}; observation.sessionId = foxSessionId;
  strncpy(observation.quarryId, foxTargetId, sizeof(observation.quarryId) - 1);
  observation.directRssi = static_cast<int8_t>(foxRssiTrend.average()); observation.trendDelta = static_cast<int8_t>(foxRssiTrend.delta());
  observation.placeSimilarity = foxPlaceMatch.similarity; observation.placeConfidence = foxPlaceMatch.confidence;
  observation.placeOverlap = foxPlaceMatch.overlap; observation.sequence = ++foxPackSequence;
  sendFoxPacket(EspNowType::PackObservation, &observation, sizeof(observation));
  recordPackEvidence(kDeviceId, observation);
}

void joinPackHunt() {
  if (!foxPackInviteSession || millis() - foxPackInviteAt > supachat::fox::kPeerFreshMs) return;
  FoxPeer *quarry = findFoxPeerById(foxPackInviteQuarryId); if (!quarry) return;
  strncpy(foxTargetId, quarry->id, sizeof(foxTargetId) - 1); strncpy(foxTargetName, quarry->name, sizeof(foxTargetName) - 1);
  memcpy(foxTargetMac, quarry->mac, 6); foxSessionId = foxPackInviteSession; foxStartedAt = millis(); foxLastPacketAt = millis();
  foxJoinedPack = true; foxState = FoxState::Acquiring; foxRssiTrend.reset(); foxPackEvidenceCount = 0;
  if (!localOnlyMode) { foxForcedLocalOnly = true; setLocalOnlyMode(true); }
  foxScanRequested = true; foxRequestPlaceAfterScan = true; renderDirty = true;
}

void stopFoxFinding(bool notifyPeer = true) {
  if (notifyPeer && foxState != FoxState::Idle && foxState != FoxState::Selecting) sendFoxControl(EspNowType::FindEnd);
  foxState = FoxState::Idle; foxSessionId = 0; foxStartedAt = 0; foxLastPacketAt = 0;
  foxScanRequested = false; foxSendPlaceResult = false; foxPlaceMatch = {};
  foxRssiTrend.reset(); foxLastConsumedRssiAt = 0;
  foxPackEvidenceCount = 0; foxJoinedPack = false;
  memset(foxTargetId, 0, sizeof(foxTargetId)); memset(foxTargetName, 0, sizeof(foxTargetName));
  if (foxForcedLocalOnly) { foxForcedLocalOnly = false; foxRestoreWifiRequested = true; }
}

void beginFoxFinding() {
  if (!foxPeerCount) { foxState = FoxState::Selecting; renderDirty = true; return; }
  foxPeerSelection = std::max(0, std::min(foxPeerSelection, static_cast<int>(foxPeerCount) - 1));
  const FoxPeer &peer = foxPeers[foxPeerSelection];
  strncpy(foxTargetId, peer.id, sizeof(foxTargetId) - 1);
  strncpy(foxTargetName, peer.name, sizeof(foxTargetName) - 1);
  memcpy(foxTargetMac, peer.mac, 6);
  foxSessionId = esp_random(); foxStartedAt = millis(); foxLastPacketAt = millis(); foxState = FoxState::Requesting;
  foxRssiTrend.reset(); foxLastConsumedRssiAt = 0;
  foxPackEvidenceCount = 0; foxJoinedPack = false;
  if (!localOnlyMode) { foxForcedLocalOnly = true; setLocalOnlyMode(true); }
  sendFoxControl(EspNowType::FindStart); renderDirty = true;
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
  size_t step = 0;
  for (;;) {
    const size_t noteIndex = step % kBootTuneLength;
    if (kBootTuneFrequencies[noteIndex] > 0) {
      M5Cardputer.Speaker.tone(kBootTuneFrequencies[noteIndex], kBootTuneNoteMs, 0, true);
    }
    // Schedule from the time this note was actually submitted. A cumulative
    // deadline causes overdue notes to fire back-to-back after a device stall.
    const uint32_t target = millis() + kBootTuneStepMs;
    while (static_cast<int32_t>(target - millis()) > 0) {
      M5Cardputer.update();
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
  languageOverride = preferences.getString("language", "auto");
  if (languageOverride != "auto" && languageOverride != "en" && languageOverride != "fr") languageOverride = "auto";
  frenchUi = languageOverride == "fr" || (languageOverride == "auto" && kBuildFrenchDefault);
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
  if (chargingModeActive || voiceRecording || audioPlaying || messageNotificationActive) return;
  const uint32_t now = millis();
  if (now - lastToneAt < kToneIntervalMs || kKeypressSongLength == 0) return;
  lastToneAt = now;
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  M5Cardputer.Speaker.tone(kKeypressSongFrequencies[songPosition], kToneDurationMs, 0, true);
  songPosition = (songPosition + 1) % kKeypressSongLength;
}

void serviceMessageNotification() {
  if (chargingModeActive) { messageNotificationPending = false; messageNotificationActive = false; return; }
  if (volumeLevel == 0) { messageNotificationPending = false; messageNotificationActive = false; return; }
  if (voiceRecording || audioPlaying) return;
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
  if (chargingModeActive) return;
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
  if (voiceLiveMode && walkieGranted && walkieConnected)
    walkieSocket.sendBIN(reinterpret_cast<const uint8_t *>(samples), kVoiceCaptureBlock * sizeof(int16_t));
  if (voiceLiveMode && kEspNowEnabled && !walkieConnected) sendEspNowAudio(samples, kVoiceCaptureBlock);
  voiceCapturedTotal += kVoiceCaptureBlock;
}

void startVoiceRecording() {
  if (chargingModeActive || voiceRecording || audioPlaying) return;
  voiceLiveMode = screenMode == ScreenMode::Walkie;
  retainCurrentVoice = !voiceLiveMode && !voiceUploadPending;
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
  if (voiceLiveMode && walkieConnected) walkieSocket.sendTXT("{\"type\":\"ptt_start\"}");
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
  if (voiceLiveMode && walkieConnected) walkieSocket.sendTXT("{\"type\":\"ptt_stop\"}");
  walkieStatus = voiceLiveMode ? "READY TO TALK" :
      (retainCurrentVoice && voiceClipReady ? "RECORDED - ENTER PLAYS" :
      (voiceUploadPending ? "LAST MESSAGE QUEUED" : "READY"));
  renderDirty = true;
}

void sendEspNowAudio(const int16_t *samples, size_t count) {
  if (!meshReady) return;
  static const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
  size_t offset = 0;
  while (offset < count) {
    EspNowPacket packet{}; packet.magic = kEspNowMagic; packet.type = static_cast<uint8_t>(EspNowType::Audio); packet.sequence = ++espNowSequence;
    strncpy(packet.senderId, kDeviceId, sizeof(packet.senderId) - 1); strncpy(packet.senderName, kDeviceName, sizeof(packet.senderName) - 1);
    strncpy(packet.roomId, currentRoomId.c_str(), sizeof(packet.roomId) - 1);
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
  if (chargingModeActive || samples.empty() || voiceRecording || volumeLevel == 0 || M5Cardputer.Speaker.isPlaying(0) >= 2) return false;
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
  if (chargingModeActive) return;
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
  if (chargingModeActive) return;
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

void onEspNowReceive(const uint8_t *mac, const uint8_t *data, int length) {
  if (chargingModeActive) return;
  if (length < static_cast<int>(offsetof(EspNowPacket, payload))) return;
  const auto *packet = reinterpret_cast<const EspNowPacket *>(data);
  if (packet->magic != kEspNowMagic || packet->payloadLength > sizeof(packet->payload) ||
      static_cast<int>(offsetof(EspNowPacket, payload) + packet->payloadLength) > length ||
      String(packet->senderId) == kDeviceId || String(packet->roomId) != currentRoomId) return;
  FoxPeer *peer = rememberFoxPeer(mac, packet->senderId, packet->senderName);
  lastNearbyAt = millis();
  if (packet->type == static_cast<uint8_t>(EspNowType::Beacon)) { renderDirty = true; return; }
  std::vector<uint8_t> plain; if (!decryptMeshPacket(*packet, plain) || !acceptMeshNonce(packet->nonce)) return;
  const EspNowType type = static_cast<EspNowType>(packet->type);
  if (type >= EspNowType::FoxBeacon && type <= EspNowType::PackObservation) {
    if (type == EspNowType::PlacePrintResult && plain.size() == sizeof(supachat::fox::PlacePrint) &&
        foxState != FoxState::Idle && String(packet->senderId) == foxTargetId) {
      supachat::fox::PlacePrint remote{}; memcpy(&remote, plain.data(), sizeof(remote));
      remote.count = std::min<uint8_t>(remote.count, supachat::fox::kMaxPlacePrintEntries);
      foxPlaceMatch = supachat::fox::match(foxLocalPrint, remote); foxState = FoxState::Guiding;
      foxLastPacketAt = millis(); renderDirty = true; return;
    }
    if (type == EspNowType::PackObservation) {
      if (plain.size() != sizeof(supachat::fox::PackObservation)) return;
      supachat::fox::PackObservation observation{}; memcpy(&observation, plain.data(), sizeof(observation));
      if (observation.sessionId == foxSessionId && String(observation.quarryId) == foxTargetId && foxState != FoxState::TargetActive) {
        recordPackEvidence(packet->senderId, observation); renderDirty = true;
      }
      return;
    }
    if (plain.size() != sizeof(FoxControl)) return;
    FoxControl control{}; memcpy(&control, plain.data(), sizeof(control));
    if (control.sessionId == 0) return;
    if (type == EspNowType::FoxBeacon && control.role == 0 && String(control.targetId) != kDeviceId &&
        (foxState == FoxState::Idle || foxState == FoxState::Selecting)) {
      FoxPeer *quarry = findFoxPeerById(control.targetId);
      if (quarry) {
        foxPackInviteSession = control.sessionId; foxPackInviteAt = millis();
        memset(foxPackInviteQuarryId, 0, sizeof(foxPackInviteQuarryId)); memset(foxPackInviteQuarryName, 0, sizeof(foxPackInviteQuarryName)); memset(foxPackInviteHunterName, 0, sizeof(foxPackInviteHunterName));
        strncpy(foxPackInviteQuarryId, quarry->id, sizeof(foxPackInviteQuarryId) - 1); strncpy(foxPackInviteQuarryName, quarry->name, sizeof(foxPackInviteQuarryName) - 1);
        strncpy(foxPackInviteHunterName, packet->senderName, sizeof(foxPackInviteHunterName) - 1); renderDirty = true;
      }
      return;
    }
    if (type == EspNowType::FindStart && String(control.targetId) == kDeviceId) {
      if (voiceRecording || audioPlaying) return;
      strncpy(foxTargetId, packet->senderId, sizeof(foxTargetId) - 1);
      strncpy(foxTargetName, packet->senderName, sizeof(foxTargetName) - 1);
      memcpy(foxTargetMac, mac, 6); foxSessionId = control.sessionId; foxStartedAt = millis();
      foxLastPacketAt = millis(); foxState = FoxState::TargetActive;
      screenMode = ScreenMode::FoxFinding;
      foxEnterLocalOnlyRequested = !localOnlyMode; foxAckRequested = true; renderDirty = true; return;
    }
    if (type == EspNowType::PlacePrintRequest && foxState == FoxState::TargetActive && control.sessionId == foxSessionId && String(control.targetId) == kDeviceId) {
      foxScanRequested = true; foxSendPlaceResult = true; foxLastPacketAt = millis(); return;
    }
    if (control.sessionId != foxSessionId || String(packet->senderId) != foxTargetId) return;
    foxLastPacketAt = millis();
    if (peer && peer->rssiAt && millis() - peer->rssiAt < 2000 && peer->rssiAt != foxLastConsumedRssiAt) { foxLastConsumedRssiAt = peer->rssiAt; foxRssiTrend.add(peer->rssi); }
    if (type == EspNowType::FoxBeacon && foxState == FoxState::SignalLost) foxState = FoxState::Guiding;
    if (type == EspNowType::FindAck && foxState == FoxState::Requesting) {
      foxState = FoxState::Acquiring; foxScanRequested = true; foxRequestPlaceAfterScan = true;
    } else if (type == EspNowType::PlacePrintRequest && foxState == FoxState::TargetActive) {
      foxScanRequested = true; foxSendPlaceResult = true;
    } else if (type == EspNowType::FindEnd) stopFoxFinding(false);
    renderDirty = true; return;
  }
  if (packet->type == static_cast<uint8_t>(EspNowType::Audio) && plain.size() % 2 == 0) {
    std::vector<int16_t> samples(plain.size() / 2); memcpy(samples.data(), plain.data(), plain.size());
    if (stateMutex) { xSemaphoreTake(stateMutex, portMAX_DELAY); if (incomingAudio.size() < 12) incomingAudio.push_back(std::move(samples)); xSemaphoreGive(stateMutex); }
  } else if (packet->type == static_cast<uint8_t>(EspNowType::Text) && packet->payloadLength > 0) {
    ChatMessage message; message.roomId = currentRoomId; message.clientId = packet->clientId; message.authorId = packet->senderId; message.authorName = packet->senderName;
    message.body = String(reinterpret_cast<const char *>(plain.data())).substring(0, plain.size()); message.state = "nearby";
    if (stateMutex) {
      bool inserted = false;
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      if (std::none_of(messages.begin(), messages.end(), [&](const ChatMessage &item){ return item.clientId == message.clientId; })) {
        messages.push_back(message); trimHistory(); saveHistoryLocked(); inserted = true;
      }
      xSemaphoreGive(stateMutex);
      if (inserted) messageNotificationPending = true;
      renderDirty = true;
    }
  }
}

void initializeEspNow() {
  if (chargingModeActive || espNowActive) return;
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(onEspNowReceive);
  esp_wifi_set_promiscuous_rx_cb(onFoxPromiscuousPacket);
  esp_wifi_set_promiscuous(true);
  esp_now_peer_info_t peer{}; memset(peer.peer_addr, 0xff, 6); peer.channel = 0; peer.ifidx = WIFI_IF_STA; peer.encrypt = false;
  if (!esp_now_is_peer_exist(peer.peer_addr)) esp_now_add_peer(&peer);
  espNowActive = true;
}

void collectFoxPlacePrint() {
  uint8_t primary = kEspNowFallbackChannel; wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&primary, &secondary);
  const uint32_t started = millis();
  const int count = WiFi.scanNetworks(false, true, false, 120);
  std::vector<supachat::fox::PlacePrintEntry> entries;
  entries.reserve(std::max(0, count));
  for (int index = 0; index < count; ++index) {
    const uint8_t *bssid = WiFi.BSSID(index); if (!bssid) continue;
    supachat::fox::PlacePrintEntry entry{}; memcpy(entry.bssid, bssid, 6);
    entry.rssi = static_cast<int8_t>(std::max(-127, std::min(0, static_cast<int>(WiFi.RSSI(index)))));
    entry.channel = static_cast<uint8_t>(WiFi.channel(index)); entries.push_back(entry);
  }
  WiFi.scanDelete();
  std::sort(entries.begin(), entries.end(), [](const supachat::fox::PlacePrintEntry &left, const supachat::fox::PlacePrintEntry &right) { return left.rssi > right.rssi; });
  foxLocalPrint = {}; foxLocalPrint.scanMs = static_cast<uint16_t>(std::min<uint32_t>(65535, millis() - started));
  foxLocalPrint.truncated = entries.size() > supachat::fox::kMaxPlacePrintEntries;
  foxLocalPrint.count = static_cast<uint8_t>(std::min(entries.size(), supachat::fox::kMaxPlacePrintEntries));
  for (uint8_t index = 0; index < foxLocalPrint.count; ++index) foxLocalPrint.entries[index] = entries[index];
  if (WiFi.status() != WL_CONNECTED) esp_wifi_set_channel(primary, secondary);
  foxLastPlaceScanAt = millis();
  if (foxRequestPlaceAfterScan) { foxRequestPlaceAfterScan = false; sendFoxControl(EspNowType::PlacePrintRequest); }
  if (foxSendPlaceResult) { sendFoxPacket(EspNowType::PlacePrintResult, &foxLocalPrint, sizeof(foxLocalPrint)); foxSendPlaceResult = false; }
  renderDirty = true;
}

void serviceFoxFinding() {
  if (foxEnterLocalOnlyRequested) { foxEnterLocalOnlyRequested = false; foxForcedLocalOnly = true; setLocalOnlyMode(true); }
  if (foxRestoreWifiRequested) { foxRestoreWifiRequested = false; setLocalOnlyMode(false); }
  if (foxAckRequested) { foxAckRequested = false; sendFoxControl(EspNowType::FindAck); }
  if (foxState == FoxState::Idle || foxState == FoxState::Selecting) {
    for (size_t index = 0; index < foxPeerCount;) {
      if (millis() - foxPeers[index].seenAt <= supachat::fox::kPeerFreshMs) { ++index; continue; }
      foxPeers[index] = foxPeers[foxPeerCount - 1]; --foxPeerCount;
    }
    foxPeerSelection = std::max(0, std::min(foxPeerSelection, std::max(0, static_cast<int>(foxPeerCount) - 1)));
    if (foxPackInviteAt && millis() - foxPackInviteAt > supachat::fox::kPeerFreshMs) { foxPackInviteSession = 0; foxPackInviteAt = 0; }
    return;
  }
  const uint32_t now = millis();
  if (now - foxStartedAt >= supachat::fox::kSessionExpiryMs) { stopFoxFinding(); screenMode = ScreenMode::FoxFinding; renderDirty = true; return; }
  if (now - foxLastBeaconAt >= supachat::fox::kFoxBeaconMs) { sendFoxControl(EspNowType::FoxBeacon); foxLastBeaconAt = now; }
  if (foxState != FoxState::TargetActive && now - foxLastPackObservationAt >= supachat::fox::kPackObservationMs) { sendPackObservation(); foxLastPackObservationAt = now; }
  FoxPeer *peer = findFoxPeerByMac(foxTargetMac);
  if (peer && peer->rssiAt != foxLastConsumedRssiAt && now - peer->rssiAt < 1500) { foxLastConsumedRssiAt = peer->rssiAt; foxRssiTrend.add(peer->rssi); renderDirty = true; }
  if (foxLastPacketAt && now - foxLastPacketAt > supachat::fox::kSignalLostMs && foxState != FoxState::TargetActive) foxState = FoxState::SignalLost;
  if (foxState == FoxState::Guiding && now - foxLastPlaceScanAt >= kFoxPlaceRescanMs) {
    foxState = FoxState::Acquiring; foxScanRequested = true; foxRequestPlaceAfterScan = true;
  }
}

void sendEspNowBeacon() {
  if (chargingModeActive || !espNowActive) return;
  static const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
  EspNowPacket packet{}; packet.magic = kEspNowMagic; packet.type = static_cast<uint8_t>(EspNowType::Beacon); packet.sequence = ++espNowSequence;
  strncpy(packet.senderId, kDeviceId, sizeof(packet.senderId) - 1); strncpy(packet.senderName, kDeviceName, sizeof(packet.senderName) - 1);
  strncpy(packet.roomId, currentRoomId.c_str(), sizeof(packet.roomId) - 1);
  esp_now_send(broadcast, reinterpret_cast<uint8_t *>(&packet), offsetof(EspNowPacket, payload)); lastEspNowBeaconAt = millis();
}

bool connectKnownWifi() {
  if (chargingModeActive) return false;
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
  if (chargingModeActive) return false;
  if (bestProfile < 0) {
    if (kEspNowEnabled) esp_wifi_set_channel(kEspNowFallbackChannel, WIFI_SECOND_CHAN_NONE);
    networkStatus = "ESPNOW ONLY"; return false;
  }
  networkStatus = "JOINING"; renderDirty = true;
  if (chargingModeActive) return false;
  WiFi.begin(wifiProfiles[bestProfile].ssid.c_str(), wifiProfiles[bestProfile].password.c_str());
  const uint32_t started = millis();
  while (!chargingModeActive && WiFi.status() != WL_CONNECTED && millis() - started < 12000) vTaskDelay(pdMS_TO_TICKS(100));
  if (chargingModeActive) { WiFi.disconnect(false, false); return false; }
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

  // Cardputer ADV exposes no direct VBUS signal. Only claim charging when an
  // idle device shows a sustained upward voltage trend. User activity changes
  // load, so discard the trend window instead of classifying those samples.
  if (now - lastUserInputAt < kPowerIdleAfterInputMs) {
    powerTrendCount = 0;
  } else {
    if (powerTrendCount < kPowerTrendSamples) powerTrendVoltages[powerTrendCount++] = nextVoltage;
    else {
      std::move(powerTrendVoltages.begin() + 1, powerTrendVoltages.end(), powerTrendVoltages.begin());
      powerTrendVoltages.back() = nextVoltage;
    }
    if (powerTrendCount == kPowerTrendSamples) {
      bool steadilyRising = powerTrendVoltages.back() - powerTrendVoltages.front() >= kPowerTrendMinimumRiseMv;
      for (size_t index = 1; index < powerTrendVoltages.size() && steadilyRising; index++)
        steadilyRising = powerTrendVoltages[index] + kPowerTrendNoiseToleranceMv >= powerTrendVoltages[index - 1];
      if (steadilyRising != externalPowerDetected) {
        externalPowerDetected = steadilyRising;
        if (steadilyRising) lastBatteryAdjustAt = now - kBatteryFastRiseStepMs;
        renderDirty = true;
      }
    }
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
  // A raw reading can move every second. Keep sampling it for the battery
  // model, but do not repaint the entire charging screen for ADC noise.
  if (!chargingModeActive && nextVoltage != batteryVoltageMv) renderDirty = true;
  batteryVoltageMv = nextVoltage;
}

const char *chargeTrendName(ChargeTrend trend) {
  switch (trend) {
    case ChargeTrend::Rising: return "RISING";
    case ChargeTrend::Flat: return "FLAT";
    case ChargeTrend::Falling: return "FALLING";
    case ChargeTrend::Unstable: return "UNSTABLE";
    default: return "WAITING";
  }
}

void appendChargeLog(const char *event, const char *detail = "") {
  if (!sdReady) return;
  const bool writeHeader = !SD.exists(kChargeLogPath);
  File file = SD.open(kChargeLogPath, FILE_APPEND);
  if (!file) return;
  if (writeHeader) file.println("schema_version,device_id,firmware_version,session_id,elapsed_s,event,raw_mv,filtered_mv,estimated_pct,trend,detail");
  const uint32_t elapsed = chargingModeActive ? (millis() - chargeSessionStartedAt) / 1000 : 0;
  file.printf("1,%s,%s,%08lx,%lu,%s,%d,%d,%d,%s,%s\n", kDeviceId, kFirmwareVersion,
              static_cast<unsigned long>(chargeSessionId), static_cast<unsigned long>(elapsed), event,
              batteryVoltageMv, static_cast<int>(batteryFilteredMv + 0.5f), batteryLevel,
              chargeTrendName(chargeTrend), detail);
  file.close();
}

ChargeTrend classifyChargeTrend(int nextMv) {
  constexpr size_t kWindow = 6;
  int values[kWindow]{};
  size_t count = 0;
  const size_t previous = std::min(chargePlotCount, kWindow - 1);
  const size_t start = chargePlotCount - previous;
  for (size_t index = start; index < chargePlotCount; index++) values[count++] = chargePlot[index].filteredMv;
  values[count++] = nextMv;
  if (count < 3) return ChargeTrend::Waiting;
  int minimum = values[0], maximum = values[0];
  for (size_t index = 1; index < count; index++) { minimum = std::min(minimum, values[index]); maximum = std::max(maximum, values[index]); }
  const int delta = values[count - 1] - values[0];
  if (delta >= kChargeTrendThresholdMv) return ChargeTrend::Rising;
  if (delta <= -kChargeTrendThresholdMv) return ChargeTrend::Falling;
  if (maximum - minimum <= kChargeTrendThresholdMv) return ChargeTrend::Flat;
  return ChargeTrend::Unstable;
}

void addChargePlotPoint(bool logSample = true) {
  if (!chargingModeActive || batteryFilteredMv <= 0) return;
  const int filteredMv = static_cast<int>(batteryFilteredMv + 0.5f);
  const ChargeTrend previousTrend = chargeTrend;
  chargeTrend = classifyChargeTrend(filteredMv);
  if (chargePlotCount == chargePlot.size()) {
    std::move(chargePlot.begin() + 1, chargePlot.end(), chargePlot.begin());
    chargePlotCount--;
  }
  ChargePlotPoint point;
  point.elapsedSeconds = (millis() - chargeSessionStartedAt) / 1000;
  point.filteredMv = static_cast<uint16_t>(std::max(0, filteredMv));
  point.estimatedPercent = static_cast<uint8_t>(std::max(0, batteryLevel));
  point.trend = chargeTrend;
  chargePlot[chargePlotCount++] = point;
  if (logSample) appendChargeLog("SAMPLE");
  if (chargeTrend != previousTrend && chargeTrend != ChargeTrend::Waiting)
    appendChargeLog((String("TREND_") + chargeTrendName(chargeTrend)).c_str());
  renderDirty = true;
}

void enterChargingMode() {
  if (chargingModeActive) return;
  chargingModeActive = true;
  inputQuarantined = true;
  chargingDimmed = false;
  lastUserInputAt = millis();
  messageNotificationPending = false; messageNotificationActive = false;
  if (voiceRecording) stopVoiceRecording();
  stopVoicePlayback();
  M5Cardputer.Mic.end(); M5Cardputer.Speaker.stop(); M5Cardputer.Speaker.end();
  walkieSocket.disconnect(); walkieConnected = false; walkieInitialized = false; walkieGranted = false;
  if (espNowActive) { esp_now_deinit(); espNowActive = false; }
  WiFi.disconnect(false, false); WiFi.mode(WIFI_OFF);
  currentSsid = ""; networkStatus = "CHARGE MODE";
  setCpuFrequencyMhz(80);
  M5Cardputer.Display.setBrightness(kChargeBrightness);
  chargeSessionStartedAt = millis(); chargeSessionId = esp_random();
  chargeLastLoggedPercent = batteryLevel;
  chargeTrend = ChargeTrend::Waiting; chargePlotCount = 0;
  nextChargePlotAt = millis() + kChargePlotSampleMs;
  screenMode = ScreenMode::Charging;
  appendChargeLog("MODE_ENTER"); addChargePlotPoint();
  renderDirty = true;
}

void exitChargingMode() {
  if (!chargingModeActive) return;
  appendChargeLog("MODE_EXIT");
  chargingModeActive = false;
  inputQuarantined = true;
  setCpuFrequencyMhz(240);
  M5Cardputer.Display.setBrightness(255);
  M5Cardputer.Speaker.begin(); M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false); WiFi.persistent(false); WiFi.disconnect(false, false);
  if (kEspNowEnabled) initializeEspNow();
  wifiResumeRequested = true; syncOverride = true; networkStatus = "RESUMING";
  screenMode = ScreenMode::Menu; renderDirty = true;
}

void serviceChargingMode() {
  if (!chargingModeActive) return;
  const uint32_t now = millis();
  if (!chargingDimmed && now - lastUserInputAt >= kChargeScreenTimeoutMs) {
    chargingDimmed = true;
    M5Cardputer.Display.setBrightness(kChargeDimBrightness);
  }
  if (static_cast<int32_t>(now - nextChargePlotAt) >= 0) {
    do { nextChargePlotAt += kChargePlotSampleMs; } while (static_cast<int32_t>(now - nextChargePlotAt) >= 0);
    addChargePlotPoint();
  }
  if (batteryLevel != chargeLastLoggedPercent) {
    chargeLastLoggedPercent = batteryLevel; appendChargeLog("PERCENT_CHANGE"); renderDirty = true;
  }
}

bool requestJson(const String &path, const char *method, const String &requestBody, String &responseBody, int &status) {
  if (chargingModeActive) { status = HTTPC_ERROR_CONNECTION_REFUSED; return false; }
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
  if (!object["receipt_state"].isNull()) serverMessage.state = String(object["receipt_state"] | "saved");
  if (!object["reply_to"].isNull()) {
    serverMessage.replyToId = object["reply_to"]["id"] | 0;
    serverMessage.replyAuthor = String(object["reply_to"]["author_name"] | "?");
    serverMessage.replyBody = String(object["reply_to"]["body"] | "");
  }
  for (JsonObjectConst receipt : object["receipts"].as<JsonArrayConst>()) {
    if (String(receipt["user_id"] | "") == kDeviceId) serverMessage.state = String(receipt["state"] | "saved");
  }
  if (existing == messages.end()) {
    const auto position = std::find_if(messages.begin(), messages.end(), [&](const ChatMessage &message) {
      return message.id > 0 && message.id > id;
    });
    messages.insert(position, serverMessage);
  } else *existing = serverMessage;
  lastServerId = std::max(lastServerId, id); trimHistory(); renderDirty = true;
}

void sendQueuedMessages() {
  std::vector<String> queuedIds;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  for (const auto &message : messages) if (message.queued) queuedIds.push_back(message.clientId);
  xSemaphoreGive(stateMutex);
  for (const auto &clientId : queuedIds) {
    String text; int64_t replyToId = 0;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    auto item = std::find_if(messages.begin(), messages.end(), [&](const ChatMessage &message) { return message.clientId == clientId; });
    if (item != messages.end()) { text = item->body; replyToId = item->replyToId; }
    xSemaphoreGive(stateMutex);
    if (text.isEmpty()) continue;
    String response; int status = 0;
    String queuedRoomId;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    item = std::find_if(messages.begin(), messages.end(), [&](const ChatMessage &message) { return message.clientId == clientId; });
    if (item != messages.end()) queuedRoomId = item->roomId;
    xSemaphoreGive(stateMutex);
    if (queuedRoomId != currentRoomId) continue;
    const String payload = "{\"client_id\":\"" + jsonEscape(clientId) + "\",\"body\":\"" + jsonEscape(text) + "\",\"room_id\":\"" + jsonEscape(queuedRoomId) + "\"" + (replyToId > 0 ? ",\"reply_to_id\":" + String(replyToId) : "") + "}";
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

void recordSyncError(const char *kind, const String &detail, int status, size_t responseBytes, bool initial) {
  lastSyncError = kind;
  lastSyncDetail = detail;
  lastSyncErrorRoom = currentRoomId;
  lastSyncErrorHttpStatus = status;
  lastSyncResponseBytes = responseBytes;
  lastSyncErrorHeap = ESP.getFreeHeap();
  lastSyncErrorAt = millis();
  lastSyncErrorInitial = initial;
  renderDirty = true;
}

void synchronize() {
  String roomsResponse; int roomsStatus = 0;
  if (requestJson("/api/rooms", "GET", "", roomsResponse, roomsStatus) && roomsStatus == 200) {
    JsonDocument roomDocument;
    const DeserializationError roomJsonError = deserializeJson(roomDocument, roomsResponse);
    if (!roomJsonError) {
      std::vector<ChatRoom> nextRooms;
      for (JsonObjectConst object : roomDocument["rooms"].as<JsonArrayConst>()) {
        const String id = String(object["id"] | ""); const int64_t latest = object["latest_message_id"] | 0;
        auto previous = std::find_if(rooms.begin(), rooms.end(), [&](const ChatRoom &room) { return room.id == id; });
        const int64_t seen = previous != rooms.end() ? previous->seenMessageId : latest;
        nextRooms.push_back({id, String(object["name"] | "Room"), String(object["group_id"] | ""),
          String(object["group_name"] | ""), String(object["default_language"] | "en"), latest, seen});
      }
      rooms = std::move(nextRooms); roomsInitialized = true;
      auto active = std::find_if(rooms.begin(), rooms.end(), [&](const ChatRoom &room) { return room.id == currentRoomId; });
      if (active == rooms.end() && !rooms.empty()) { currentRoomId = rooms[0].id; currentRoomName = rooms[0].name; }
      else if (active != rooms.end()) currentRoomName = active->name;
      applyEffectiveLanguage();
    } else {
      recordSyncError("ROOM JSON", roomJsonError.c_str(), roomsStatus, roomsResponse.length(), !initialSyncComplete);
    }
  }
  sendQueuedMessages();
  String response; int status = 0;
  // Initial hydration walks backward from the newest page. Each response is
  // discarded before the next page so TLS, String, and JSON storage never need
  // to coexist with an entire 100-message history payload.
  const bool hydratingHistory = !initialSyncComplete;
  const String requestedRoomId = currentRoomId;
  const int64_t syncAfter = hydratingHistory ? 0 : lastServerId;
  const size_t syncLimit = hydratingHistory ? kHistoryPageLimit : kSyncBatchLimit;
  const String path = "/api/device/sync?after=" + String(syncAfter) + "&receipts_after=" + String(lastReceiptAt)
      + "&limit=" + String(syncLimit) + "&wait=0&room=" + requestedRoomId
      + (hydratingHistory ? "&before=" + String(historyBeforeId) : "");
  if (!requestJson(path, "GET", "", response, status)) {
    networkStatus = "SYNC IO " + String(status);
    recordSyncError("SYNC IO", String(status), status, response.length(), !initialSyncComplete);
    Serial.printf("sync transport error=%d bytes=%u heap=%u\n", status, response.length(), lastSyncErrorHeap); return;
  }
  if (status != 200) {
    networkStatus = "SYNC HTTP " + String(status);
    recordSyncError("SYNC HTTP", String(status), status, response.length(), !initialSyncComplete);
    Serial.printf("sync http status=%d bytes=%u heap=%u\n", status, response.length(), lastSyncErrorHeap); return;
  }
  if (currentRoomId != requestedRoomId) return;
  // ArduinoJson treats String input as read-only and duplicates every parsed
  // string. The mutable buffer selects zero-copy mode within each bounded page.
  JsonDocument document;
  const DeserializationError jsonError = response.isEmpty()
      ? DeserializationError(DeserializationError::EmptyInput)
      : deserializeJson(document, &response[0], response.length());
  if (jsonError) {
    networkStatus = jsonError == DeserializationError::NoMemory ? "JSON MEMORY" : "BAD JSON";
    recordSyncError(networkStatus.c_str(), jsonError.c_str(), status, response.length(), !initialSyncComplete);
    Serial.printf("sync json error=%s bytes=%u heap=%u\n", jsonError.c_str(), response.length(), lastSyncErrorHeap);
    return;
  }
  std::vector<int64_t> newlyRead;
  const JsonArrayConst pageMessages = document["messages"].as<JsonArrayConst>();
  const size_t pageMessageCount = pageMessages.size();
  const bool historyHasMore = hydratingHistory && (document["history"]["has_more"] | false);
  const int64_t nextHistoryBefore = hydratingHistory ? (document["history"]["next_before"] | 0) : 0;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  for (JsonObjectConst object : pageMessages) {
    const bool incoming = String(object["author_id"] | "") != kDeviceId;
    mergeServerMessage(object);
    if (incoming) newlyRead.push_back(object["id"] | 0);
  }
  for (JsonObjectConst receipt : document["receipts"].as<JsonArrayConst>()) {
    const int64_t messageId = receipt["message_id"] | 0;
    const int64_t updatedAt = receipt["updated_at"] | 0;
    lastReceiptAt = std::max(lastReceiptAt, updatedAt);
    if (String(receipt["user_id"] | "") == kDeviceId) {
      auto item = std::find_if(messages.begin(), messages.end(), [&](const ChatMessage &message) { return message.id == messageId; });
      if (item != messages.end()) item->state = String(receipt["state"] | item->state);
    }
  }
  saveHistoryLocked(); xSemaphoreGive(stateMutex);
  // Initial history hydration is silent. Only messages arriving after the
  // current room is fully loaded are notifications.
  if (!hydratingHistory && !newlyRead.empty()) messageNotificationPending = true;
  document.clear(); response = "";
  // Backfill is historical state, not a stream of newly observed messages.
  // The server already records delivery; avoid opening up to 100 extra TLS
  // sessions just to mark old pages read.
  if (!hydratingHistory) for (const int64_t id : newlyRead) if (id > 0) postReadReceipt(id);
  if (hydratingHistory) {
    historyHydratedCount += pageMessageCount;
    if (historyHasMore && nextHistoryBefore > 0 && pageMessageCount > 0 && historyHydratedCount < kHistoryLimit) {
      historyBeforeId = nextHistoryBefore;
      networkStatus = "HISTORY " + String(historyHydratedCount);
      renderDirty = true;
      return;
    }
    historyBeforeId = 0;
    historyHydratedCount = 0;
  }
  networkStatus = "SYNCED"; renderDirty = true;
  initialSyncComplete = true;
  auto activeRoom = std::find_if(rooms.begin(), rooms.end(), [&](const ChatRoom &room) { return room.id == currentRoomId; });
  if (activeRoom != rooms.end()) activeRoom->seenMessageId = std::max(activeRoom->latestMessageId, lastServerId);
}

void networkTask(void *) {
  uint32_t nextWifiAttempt = 0;
  for (;;) {
    if (foxScanRequested) { foxScanRequested = false; collectFoxPlacePrint(); continue; }
    if (chargingModeActive) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if (manualWifiMode) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    if (localOnlyMode) {
      if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false, false);
      if (currentSsid.length() || networkStatus != "ESPNOW LOCAL") {
        currentSsid = ""; networkStatus = "ESPNOW LOCAL"; renderDirty = true;
      }
      vTaskDelay(pdMS_TO_TICKS(250)); continue;
    }
    if (wifiResumeRequested) { wifiResumeRequested = false; nextWifiAttempt = 0; }
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
    if (syncOverride) syncOverride = false;
    vTaskDelay(pdMS_TO_TICKS(kSyncPollMs));
  }
}

void walkieTask(void *) {
  for (;;) {
    if (chargingModeActive) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
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
  if (screenMode != ScreenMode::Charging && screenMode != ScreenMode::ChargingConfirm) {
    display.setCursor(106, 6); display.print(networkStatus.substring(0, 10));
  }
  if (screenMode != ScreenMode::Charging) {
    const String batteryText = batteryLevel < 0 ? String("?%") : String(batteryLevel) + "%";
    display.setTextSize(1); display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    display.setCursor(237 - static_cast<int>(batteryText.length()) * 6, 6);
    display.print(batteryText); display.setTextSize(1);
  }
  if (externalPowerDetected && screenMode != ScreenMode::Charging && screenMode != ScreenMode::ChargingConfirm) {
    display.fillTriangle(199, 2, 194, 10, 199, 10, TFT_YELLOW);
    display.fillTriangle(197, 8, 203, 8, 196, 18, TFT_YELLOW);
  }
}

size_t utf8CharacterCount(const String &text);
String utf8Tail(const String &text, size_t maximumCharacters);
void printFont0Text(const String &text);

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
    const String sender = (message.authorName.isEmpty() ? message.authorId : message.authorName).substring(0, 12);
    String text = message.voice ? String("[voice message]") : message.body;
    bool firstChunk = true;
    while (!text.isEmpty()) {
      const int width = firstChunk ? std::max(8, 23 - static_cast<int>(sender.length())) : 25;
      int split = text.length() <= width ? text.length() : text.lastIndexOf(' ', width);
      if (split < 6) split = std::min(width, static_cast<int>(text.length()));
      String line = text.substring(0, split);
      text = text.substring(split);
      while (text.startsWith(" ")) text.remove(0, 1);
      lines.push_back({line, sender, colour, mine, firstChunk, historyOffset > 0 && index == end - 1});
      firstChunk = false;
    }
  }
  int firstLine = static_cast<int>(lines.size());
  int usedHeight = 0;
  while (firstLine > 0) {
    if (usedHeight + 17 > 82) break;
    usedHeight += 17; firstLine--;
  }
  // Never show an orphaned body whose sender prefix was clipped by the viewport.
  if (firstLine < static_cast<int>(lines.size())) lines[firstLine].showSender = true;
  int y = 24;
  for (int index = firstLine; index < static_cast<int>(lines.size()); index++) {
    // Font0 stores CP437 bitmaps; map UTF-8 accents to their actual glyph slots.
    display.setFont(&fonts::Font0); display.setTextSize(1.5f);
    const String prefix = lines[index].showSender ? lines[index].sender + ": " : "";
    const int x = lines[index].mine ? std::max(3, 237 - display.textWidth(prefix + lines[index].text)) : 3;
    const uint32_t lineBackground = lines[index].selected ? 0x18C3 : TFT_BLACK;
    if (lines[index].selected) display.fillRoundRect(std::max(1, x - 2), y - 2, std::min(238 - x, display.textWidth(prefix + lines[index].text) + 5), 15, 2, lineBackground);
    display.setCursor(x, y);
    if (lines[index].showSender) {
      display.setTextColor(lines[index].colour, lineBackground); printFont0Text(lines[index].sender);
      display.setTextColor(TFT_WHITE, lineBackground); display.print(": ");
    } else display.setTextColor(TFT_WHITE, lineBackground);
    printFont0Text(lines[index].text); y += 17;
  }
  xSemaphoreGive(stateMutex);
  display.setFont(&fonts::Font0);
  display.drawFastHLine(0, 106, 240, TFT_DARKGREY);
  display.setTextSize(1);
  if (replyToMessageId > 0) { display.setTextColor(TFT_CYAN, TFT_BLACK); display.setCursor(3, 109); display.print(uiText("REPLY ", "REPONDRE ")); printFont0Text(replyToAuthor.substring(0, 15)); }
  display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(3, replyToMessageId > 0 ? 120 : 112); display.print("> ");
  display.setTextColor(TFT_WHITE, TFT_BLACK); printFont0Text(utf8Tail(draft, replyToMessageId > 0 ? 27 : 34));
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(198, 126); display.printf("%d/140", utf8CharacterCount(draft));
}

const char *kMenuItemsEn[] = {"BACK TO CHAT", "ROOMS", "SYNC NOW", "VOICE MESSAGES", "WALKIE-TALKIE", "VOLUME", "LANGUAGE", "NETWORKS", "ESP-NOW LOCAL", "CHARGING MODE", "STATUS", "CHANGELOG", "EMOJI RECIPES", "FOX FINDING"};
const char *kMenuItemsFr[] = {"RETOUR CHAT", "SALONS", "SYNCHRO", "MESSAGES VOCAUX", "WALKIE-TALKIE", "VOLUME", "LANGUE", "RESEAUX", "ESP-NOW LOCAL", "MODE CHARGE", "ETAT", "CHANGEMENTS", "RECETTES EMOJI", "CHASSE AU RENARD"};
constexpr int kMenuItemCounts[kMenuPageCount] = {6, 6, 2};
void drawMenu() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("MENU");
  display.setTextSize(1); display.setTextColor(TFT_YELLOW, TFT_DARKGREEN); display.setCursor(78, 6); display.printf("%d/%d", menuPage + 1, kMenuPageCount);
  for (int row = 0; row < kMenuItemCounts[menuPage]; row++) {
    const int index = menuPage * kMenuItemsPerPage + row;
    const int y = 21 + row * 14; const bool selected = row == menuSelections[menuPage];
    display.fillRoundRect(6, y, 228, 12, 3, selected ? TFT_GREEN : TFT_DARKGREY);
    display.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_GREEN : TFT_DARKGREY);
    display.setCursor(13, y + 3); display.print(frenchUi ? kMenuItemsFr[index] : kMenuItemsEn[index]);
    if (index == 8) { display.setCursor(184, y + 3); display.print(localOnlyMode ? "ON" : "OFF"); }
  }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 110); display.print(uiText("LEFT/RIGHT PAGE", "GAUCHE/DROITE PAGE"));
  display.setCursor(6, 122); display.print(uiText("UP/DOWN MOVE      ENTER OPEN", "HAUT/BAS BOUGER   ENTER OUVRIR"));
}

const char *kEmojiRecipes[] = {":)  HAPPY", ":(  SAD", ";)  WINK", ":D  LAUGH", "<3  LOVE", ":P  PLAYFUL", ":O  SURPRISED", ":/  UNSURE"};
constexpr int kEmojiRecipeCount = sizeof(kEmojiRecipes) / sizeof(kEmojiRecipes[0]);
void drawEmojiRecipes() {
  auto &display=uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("EMOJI", "EMOJIS")); display.setTextSize(1);
  const int start=std::max(0,emojiRecipeSelection-5);
  for(int i=start;i<kEmojiRecipeCount && i<start+6;i++){const int y=22+(i-start)*15; const bool selected=i==emojiRecipeSelection; display.fillRoundRect(8,y,224,13,3,selected?TFT_DARKGREEN:TFT_BLACK); display.setTextColor(selected?TFT_YELLOW:TFT_WHITE,selected?TFT_DARKGREEN:TFT_BLACK); display.setCursor(14,y+3); display.print(kEmojiRecipes[i]);}
  display.setTextColor(TFT_DARKGREY,TFT_BLACK); display.setCursor(7,121); display.print(uiText("UP/DOWN BROWSE  ENTER BACK","HAUT/BAS VOIR  ENTER RETOUR"));
}

void drawRooms() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("ROOMS", "SALONS")); display.setTextSize(1);
  const int start = std::max(0, roomSelection - 5);
  for (int index = start; index < static_cast<int>(rooms.size()) && index < start + 6; index++) {
    const int y = 22 + (index - start) * 15; const bool selected = index == roomSelection;
    display.fillRoundRect(6, y, 228, 13, 3, selected ? TFT_GREEN : TFT_DARKGREY);
    display.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_GREEN : TFT_DARKGREY); display.setCursor(12, y + 3);
    display.print(rooms[index].name.substring(0, 34));
  }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print(uiText("UP/DOWN CHOOSE    ENTER OPEN", "HAUT/BAS CHOISIR  ENTER OUVRIR"));
}

void drawWalkie() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK);
  const bool liveMode = screenMode == ScreenMode::Walkie;
  drawHeader(liveMode ? "WALKIE" : uiText("VOICE MESSAGES", "MESSAGES VOCAUX"));
  if (voiceRecording) {
    display.setTextSize(2); display.setTextColor(TFT_RED, TFT_BLACK); display.setCursor(12, 32);
    display.print(liveMode ? uiText("TRANSMITTING", "TRANSMISSION") : uiText("RECORDING", "ENREGISTRE"));
    display.setTextSize(1); display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(12, 62);
    display.printf("%u.%us / 30s", voiceCapturedTotal / kVoiceSampleRate,
                   (voiceCapturedTotal % kVoiceSampleRate) * 10 / kVoiceSampleRate);
  } else if (liveMode) {
    display.setTextSize(2); display.setTextColor(TFT_GREEN, TFT_BLACK); display.setCursor(31, 39);
    display.print(uiText("READY TO TALK", "PRET A PARLER"));
    display.setTextSize(1); display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(7, 78);
    display.print(walkieStatus.substring(0, 35));
  } else {
    const auto inbox = voiceInbox();
    if (inbox.empty()) { display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(12, 42); display.print(uiText("No voice messages yet", "Aucun message vocal")); }
    else {
      voiceInboxSelection = std::min(voiceInboxSelection, static_cast<int>(inbox.size()) - 1);
      const int first = std::max(0, voiceInboxSelection - 3);
      for (int row = 0; row < 4 && first + row < static_cast<int>(inbox.size()); row++) {
        const int index = first + row; const bool selected = index == voiceInboxSelection; const int y = 23 + row * 17;
        display.fillRoundRect(5, y, 230, 15, 3, selected ? TFT_GREEN : TFT_DARKGREY);
        display.setTextColor(selected ? TFT_BLACK : participantColour(inbox[index].authorId, inbox[index].authorName), selected ? TFT_GREEN : TFT_DARKGREY);
        display.setCursor(9, y + 4); display.printf(frenchUi ? "%-5s  vocal #%lld" : "%-5s  voice #%lld", inbox[index].authorName.substring(0, 5).c_str(), inbox[index].id);
      }
    }
    display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(7, 94); display.print(walkieStatus.substring(0, 35));
  }
  display.setTextSize(1); display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (!liveMode) {
    display.setCursor(6, 111); display.print(uiText("UP/DOWN CHOOSE  ENTER PLAY/STOP", "HAUT/BAS CHOISIR ENTER ECOUTER"));
    display.setCursor(6, 123); display.print(uiText("HOLD SPACE RECORD   MENU BACK", "TENIR ESPACE ENREG. MENU RETOUR"));
  } else {
    display.setCursor(6, 111); display.print(uiText("HOLD SPACE: PUSH TO TALK", "TENIR ESPACE: PARLER"));
    display.setCursor(6, 123); display.print(uiText("RELEASE: STOP       MENU BACK", "RELACHER: FIN       MENU RETOUR"));
  }
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
  display.setCursor(6, 116); display.print(uiText("LEFT/RIGHT CHANGE   OK BACK", "GAUCHE/DROITE CHANGER OK RETOUR"));
}

void drawStatus() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("STATUS", "ETAT")); display.setTextSize(1);
  display.setTextColor(TFT_YELLOW, TFT_DARKGREEN); display.setCursor(78, 6); display.printf("%d/2", statusPage + 1);
  display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 29);
  if (statusPage == 0) {
    display.printf(frenchUi ? "APPAREIL  %s\n" : "NODE      %s\n", kDeviceName);
    display.printf("WIFI      %s\n", currentSsid.isEmpty() ? "offline" : currentSsid.c_str());
    display.printf(frenchUi ? "ETAT      %s\n" : "STATE     %s\n", networkStatus.c_str()); display.printf(frenchUi ? "HEURE     %s\n" : "TIME      %s\n", timeKnown ? uiText("known", "connue") : uiText("unknown", "inconnue"));
    display.printf(frenchUi ? "BATTERIE  %d%%  %d mV\n" : "BATTERY   %d%%  %d mV\n", batteryLevel, batteryVoltageMv);
    display.printf(frenchUi ? "ALIM.     %s\n" : "POWER     %s\n", externalPowerDetected ? uiText("cable detected", "cable detecte") : uiText("not detected", "non detecte"));
    display.printf("SD/KEY    %s / %s\n", sdReady ? "ready" : "missing", keyboardReady ? "ready" : "fault");
    display.printf("HTTP/HEAP %d / %u\n", lastHttpStatus, ESP.getFreeHeap());
  } else {
    const bool hasError = lastSyncError != "NONE";
    const uint32_t errorAgeSeconds = hasError ? (millis() - lastSyncErrorAt) / 1000 : 0;
    display.printf("ERROR     %s\n", lastSyncError.substring(0, 25).c_str());
    display.printf("DETAIL    %s\n", hasError ? lastSyncDetail.substring(0, 25).c_str() : "no sync failures");
    display.printf("HTTP/BYTES %d / %u\n", lastSyncErrorHttpStatus, lastSyncResponseBytes);
    display.printf("HEAP      %u\n", lastSyncErrorHeap);
    display.printf("ROOM      %s\n", hasError ? lastSyncErrorRoom.substring(0, 25).c_str() : "-");
    display.printf("PHASE     %s\n", hasError ? (lastSyncErrorInitial ? "INITIAL" : "LIVE") : "-");
    display.printf("AGE       %us\n", errorAgeSeconds);
  }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print(uiText("LEFT/RIGHT PAGE  OK BACK", "GAUCHE/DROITE PAGE OK RETOUR"));
}

void drawNetworks() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("NETWORKS", "RESEAUX")); display.setTextSize(1);
  if (scannedNetworks.empty()) {
    display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 38); display.print(uiText("No networks found", "Aucun reseau trouve"));
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
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 121); display.print(uiText("ARROWS  OK JOIN  LEFT BACK", "FLECHES OK JOINDRE GAUCHE RETOUR"));
}

void drawNetworkPassword() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("JOIN WIFI", "JOINDRE WIFI")); display.setTextSize(1);
  display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(6, 28); display.print(selectedSsid.substring(0, 28));
  display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 52); display.print(uiText("PASSWORD", "MOT DE PASSE"));
  display.fillRoundRect(5, 65, 230, 24, 4, TFT_DARKGREY);
  display.setTextColor(TFT_WHITE, TFT_DARKGREY); display.setCursor(10, 73);
  display.print(networkPassword.substring(networkPassword.length() > 28 ? networkPassword.length() - 28 : 0));
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 104); display.print(networkStatus.substring(0, 34));
  display.setCursor(6, 121); display.print(uiText("TYPE  ENTER JOIN  LEFT CANCEL", "TAPER ENTER JOINDRE GAUCHE ANNULER"));
}

void drawChargingConfirm() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("CHARGE MODE", "MODE CHARGE"));
  display.setTextSize(2); display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(45, 30);
  display.print(uiText("CHARGING", "CHARGE"));
  display.setTextSize(1); display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(24, 64);
  display.print(uiText("PAUSES CHAT + RADIO", "PAUSE CHAT + RADIO"));
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 110);
  display.print(uiText("LOW-POWER BATTERY HISTORY", "HISTORIQUE BATTERIE"));
  display.setCursor(6, 123); display.print(uiText("LEFT CANCEL       ENTER START", "GAUCHE ANNULER    ENTER DEMARRER"));
}

int chargePlotY(int millivolts) {
  const int clamped = std::max(kChargePlotMinMv, std::min(kChargePlotMaxMv, millivolts));
  return 109 - (clamped - kChargePlotMinMv) * 34 / (kChargePlotMaxMv - kChargePlotMinMv);
}

void drawCharging() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("CHARGING", "CHARGE"));
  const int filteredMv = static_cast<int>(batteryFilteredMv + 0.5f);
  const uint32_t elapsedMinutes = (millis() - chargeSessionStartedAt) / 60000;
  display.setTextSize(3); display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(7, 27);
  display.printf("%d%%", batteryLevel);
  display.setTextSize(2); display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(132, 29);
  display.printf("%d mV", filteredMv);
  display.setTextSize(1); display.setTextColor(TFT_LIGHTGREY, TFT_BLACK); display.setCursor(150, 53);
  display.printf("%02lu:%02lu elapsed", static_cast<unsigned long>(elapsedMinutes / 60), static_cast<unsigned long>(elapsedMinutes % 60));

  constexpr int plotLeft = 8, plotRight = 232, plotBottom = 110;
  display.drawFastHLine(plotLeft, plotBottom, plotRight - plotLeft + 1, TFT_DARKGREY);
  if (chargePlotCount == 1) {
    display.fillCircle(plotLeft, chargePlotY(chargePlot[0].filteredMv), 2, TFT_GREEN);
  } else if (chargePlotCount > 1) {
    for (size_t index = 1; index < chargePlotCount; index++) {
      const int x0 = plotLeft + static_cast<int>((index - 1) * (plotRight - plotLeft) / (chargePlotCount - 1));
      const int x1 = plotLeft + static_cast<int>(index * (plotRight - plotLeft) / (chargePlotCount - 1));
      display.drawLine(x0, chargePlotY(chargePlot[index - 1].filteredMv), x1, chargePlotY(chargePlot[index].filteredMv), TFT_GREEN);
    }
  }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(5, 123);
  display.print(uiText("LEFT EXIT   ENTER SCREEN OFF", "GAUCHE SORTIR  ENTER ECRAN OFF"));
}

void drawFoxFinding() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("FOX FINDING", "CHASSE RENARD"));
  display.setTextSize(1);
  if (foxState == FoxState::Idle || foxState == FoxState::Selecting) {
    display.setTextColor(TFT_LIGHTGREY, TFT_BLACK); display.setCursor(6, 24);
    display.print(uiText("NEARBY PARTICIPANTS", "PARTICIPANTS PROCHES"));
    const bool packInvite = foxPackInviteSession && millis() - foxPackInviteAt <= supachat::fox::kPeerFreshMs;
    if (packInvite) { display.setTextColor(TFT_CYAN, TFT_BLACK); display.setCursor(6, 34); display.printf("JOIN PACK: %s + %s", foxPackInviteHunterName, foxPackInviteQuarryName); }
    if (!foxPeerCount) { display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(30, 58); display.print(uiText("Waiting for beacons...", "Attente des balises...")); }
    for (size_t index = 0; index < foxPeerCount && index < 5; ++index) {
      const int y = (packInvite ? 52 : 39) + static_cast<int>(index) * 14; const bool selected = !packInvite && static_cast<int>(index) == foxPeerSelection;
      display.fillRoundRect(6, y, 228, 12, 3, selected ? TFT_GREEN : TFT_DARKGREY);
      display.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_GREEN : TFT_DARKGREY); display.setCursor(12, y + 3);
      display.printf("%-12s %4d dBm", foxPeers[index].name, foxPeers[index].rssi);
    }
    display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print(packInvite ? "LEFT BACK          ENTER JOIN" : uiText("LEFT BACK        ENTER REQUEST", "GAUCHE RETOUR   ENTER DEMANDE")); return;
  }
  display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 25); display.printf("%s  %d dBm", foxTargetName, foxRssiTrend.average());
  const char *phase = foxState == FoxState::Requesting ? "REQUESTING" : foxState == FoxState::Acquiring ? "SCANNING PLACE" : foxState == FoxState::TargetActive ? "HELPING SEEKER" : foxState == FoxState::SignalLost ? "SIGNAL LOST" : foxRssiTrend.guidance();
  const uint32_t phaseColour = foxState == FoxState::SignalLost ? TFT_RED : strcmp(phase, "WARMER") == 0 ? TFT_GREEN : strcmp(phase, "COLDER") == 0 ? TFT_ORANGE : TFT_YELLOW;
  display.setTextSize(2); display.setTextColor(phaseColour, TFT_BLACK); display.setCursor(18, 48); display.print(phase);
  display.setTextSize(1); display.setTextColor(TFT_LIGHTGREY, TFT_BLACK); display.setCursor(6, 80);
  if (foxPlaceMatch.confidence) display.printf("PLACE %u%%  %u MATCHES  CONF %u", foxPlaceMatch.similarity, foxPlaceMatch.overlap, foxPlaceMatch.confidence);
  else display.print(uiText("PLACEPRINT NOT READY", "EMPREINTE EN ATTENTE"));
  size_t freshPack = 0; for (size_t index = 0; index < foxPackEvidenceCount; ++index) if (millis() - foxPackEvidence[index].receivedAt <= supachat::fox::kPackFreshMs) ++freshPack;
  const int best = supachat::fox::bestEvidence(foxPackEvidence.data(), foxPackEvidenceCount, millis());
  display.setCursor(6, 96);
  if (freshPack > 1 && best >= 0) { FoxPeer *leader = findFoxPeerById(foxPackEvidence[best].hunterId); display.printf("PACK %u  LEAD %s", static_cast<unsigned>(freshPack), leader ? leader->name : foxPackEvidence[best].hunterId); }
  else display.print(uiText("SOLO - NOTES SHARED", "SOLO - NOTES PARTAGEES"));
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print(uiText("LEFT END        NO METRES/GPS", "GAUCHE FIN     SANS METRES/GPS"));
}

void drawLanguage();
void drawChangelog();
void render() {
  if (screenMode == ScreenMode::Chat) drawChat(); else if (screenMode == ScreenMode::Menu) drawMenu(); else if (screenMode == ScreenMode::Rooms) drawRooms();
  else if (screenMode == ScreenMode::Volume) drawVolume(); else if (screenMode == ScreenMode::Language) drawLanguage(); else if (screenMode == ScreenMode::Changelog) drawChangelog();
  else if (screenMode == ScreenMode::EmojiRecipes) drawEmojiRecipes();
  else if (screenMode == ScreenMode::VoiceMessages || screenMode == ScreenMode::Walkie) drawWalkie();
  else if (screenMode == ScreenMode::Status) drawStatus();
  else if (screenMode == ScreenMode::NetworkPassword) drawNetworkPassword();
  else if (screenMode == ScreenMode::ChargingConfirm) drawChargingConfirm();
  else if (screenMode == ScreenMode::Charging) drawCharging();
  else if (screenMode == ScreenMode::FoxFinding) drawFoxFinding(); else drawNetworks();
  if (uiCanvasReady) uiCanvas.pushSprite(0, 0);
  renderDirty = false; lastRenderAt = millis();
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

size_t utf8CharacterCount(const String &text) {
  size_t count = 0;
  for (size_t index = 0; index < text.length(); index++)
    if ((static_cast<uint8_t>(text[index]) & 0xC0) != 0x80) count++;
  return count;
}

void setLocalOnlyMode(bool enabled) {
  localOnlyMode = enabled;
  manualWifiMode = false;
  walkieSocket.disconnect(); walkieInitialized = false; walkieConnected = false;
  if (enabled) {
    WiFi.disconnect(false, false); currentSsid = "";
    esp_wifi_set_channel(kEspNowFallbackChannel, WIFI_SECOND_CHAN_NONE);
    networkStatus = "ESPNOW LOCAL"; walkieStatus = "ESPNOW LOCAL";
  } else {
    wifiResumeRequested = true;
    networkStatus = "WIFI RESUME"; walkieStatus = "RECONNECTING";
  }
  renderDirty = true;
}

void drawLanguage() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("LANGUAGE", "LANGUE"));
  display.setTextSize(2); display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(42, 42);
  display.print(languageOverride == "auto" ? uiText("AUTOMATIC", "AUTOMATIQUE") : languageOverride == "fr" ? "FRANCAIS" : "ENGLISH");
  display.setTextSize(1); display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(22, 75);
  if (languageOverride == "auto") display.print(uiText("Uses this room's group", "Selon le groupe du salon"));
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 116);
  display.print(uiText("LEFT/RIGHT CHANGE   OK BACK", "GAUCHE/DROITE CHANGER OK RETOUR"));
}

void drawChangelog() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("CHANGELOG", "CHANGEMENTS"));
  changelogSelection = std::max(0, std::min(changelogSelection, kSupaChatChangelogCount - 1));
  const auto &entry = kSupaChatChangelog[changelogSelection];
  display.setTextSize(2); display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(8, 25); display.print(entry.version);
  display.setTextSize(1); display.setTextColor(TFT_WHITE, TFT_BLACK);
  changelogLineOffset = std::max(0, std::min(changelogLineOffset, std::max(0, entry.lineCount - 4)));
  for (int row = 0; row < 4 && changelogLineOffset + row < entry.lineCount; row++) {
    display.setCursor(12, 49 + row * 13); display.print("- "); display.print(entry.lines[changelogLineOffset + row]);
  }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 110);
  display.printf("%d/%d  %d-%d/%d", changelogSelection + 1, kSupaChatChangelogCount,
                 changelogLineOffset + 1, std::min(changelogLineOffset + 4, entry.lineCount), entry.lineCount);
  display.setCursor(6, 122); display.print(uiText("UP/DOWN SCROLL LEFT/RIGHT BUILD", "HAUT/BAS DEFIL. GAUCHE/DROITE VER."));
}

void applyEffectiveLanguage() {
  if (languageOverride == "fr") frenchUi = true;
  else if (languageOverride == "en") frenchUi = false;
  else {
    auto active = std::find_if(rooms.begin(), rooms.end(), [&](const ChatRoom &room) { return room.id == currentRoomId; });
    frenchUi = active != rooms.end() ? active->defaultLanguage == "fr" : kBuildFrenchDefault;
  }
}

void saveLanguageOverride() {
  preferences.begin("supachat", false); preferences.putString("language", languageOverride); preferences.end();
  applyEffectiveLanguage(); renderDirty = true;
}

void removeLastUtf8Character(String &text) {
  if (text.isEmpty()) return;
  size_t index = text.length() - 1;
  while (index > 0 && (static_cast<uint8_t>(text[index]) & 0xC0) == 0x80) index--;
  text.remove(index);
}

String utf8Tail(const String &text, size_t maximumCharacters) {
  size_t start = text.length();
  size_t count = 0;
  while (start > 0 && count < maximumCharacters) {
    start--;
    if ((static_cast<uint8_t>(text[start]) & 0xC0) != 0x80) count++;
  }
  return text.substring(start);
}

String font0Cp437Text(const String &text) {
  String encoded;
  encoded.reserve(text.length());
  for (size_t index = 0; index < text.length(); index++) {
    const uint8_t first = static_cast<uint8_t>(text[index]);
    if (first == 0xC3 && index + 1 < text.length()) {
      const uint8_t second = static_cast<uint8_t>(text[index + 1]);
      if (second == 0xA9) { encoded += static_cast<char>(0x82); index++; continue; } // é
      if (second == 0xA8) { encoded += static_cast<char>(0x8A); index++; continue; } // è
      if (second == 0xA0) { encoded += static_cast<char>(0x85); index++; continue; } // à
    }
    encoded += static_cast<char>(first);
  }
  return encoded;
}

void printFont0Text(const String &text) {
  const String encoded = font0Cp437Text(text);
  uiCanvas.setAttribute(lgfx::attribute::UTF8_SWITCH, false);
  uiCanvas.cp437(true);
  uiCanvas.print(encoded);
  uiCanvas.setAttribute(lgfx::attribute::UTF8_SWITCH, true);
}

bool appendKeyboardText(String &target, const char *text, size_t limit, bool byteLimit) {
  const size_t added = strlen(text);
  if (byteLimit ? target.length() + added > limit : utf8CharacterCount(target) + 1 > limit) return false;
  target += text;
  return true;
}

void typeFrenchCharacter(String &target, char character, size_t limit, bool byteLimit) {
  if (!frenchUi) {
    const char text[] = {character, '\0'};
    appendKeyboardText(target, text, limit, byteLimit);
    return;
  }
  if (frenchGravePending) {
    frenchGravePending = false;
    if (character == 'a') { appendKeyboardText(target, u8"à", limit, byteLimit); return; }
    if (character == 'e') { appendKeyboardText(target, u8"è", limit, byteLimit); return; }
    appendKeyboardText(target, "'", limit, byteLimit);
    typeFrenchCharacter(target, character, limit, byteLimit);
    return;
  }
  if (character == '\'') { frenchGravePending = true; return; }
  if (character == '?') { appendKeyboardText(target, u8"é", limit, byteLimit); return; }
  const char text[] = {character, '\0'};
  appendKeyboardText(target, text, limit, byteLimit);
}

void flushFrenchDeadKey(String &target, size_t limit, bool byteLimit) {
  if (!frenchGravePending) return;
  frenchGravePending = false;
  appendKeyboardText(target, "'", limit, byteLimit);
}

void sendDraft() {
  flushFrenchDeadKey(draft, kMessageLimit, false);
  draft.trim(); if (draft.isEmpty()) return;
  ChatMessage message; message.roomId = currentRoomId; message.clientId = nextClientId(); message.authorId = kDeviceId; message.authorName = kDeviceName;
  message.body = draft; message.createdAt = time(nullptr) * 1000LL; message.queued = true; message.state = "queued";
  message.replyToId = replyToMessageId; message.replyAuthor = replyToAuthor; message.replyBody = replyToBody;
  xSemaphoreTake(stateMutex, portMAX_DELAY); messages.push_back(message); trimHistory(); saveHistoryLocked(); xSemaphoreGive(stateMutex);
  if (kEspNowEnabled && meshReady) {
    static const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
    EspNowPacket packet{}; packet.magic = kEspNowMagic; packet.type = static_cast<uint8_t>(EspNowType::Text); packet.sequence = ++espNowSequence;
    strncpy(packet.senderId, kDeviceId, sizeof(packet.senderId) - 1); strncpy(packet.senderName, kDeviceName, sizeof(packet.senderName) - 1);
    strncpy(packet.roomId, currentRoomId.c_str(), sizeof(packet.roomId) - 1);
    strncpy(packet.clientId, message.clientId.c_str(), sizeof(packet.clientId) - 1);
    const size_t length = std::min<size_t>(message.body.length(), sizeof(packet.payload));
    if (encryptMeshPacket(packet, reinterpret_cast<const uint8_t *>(message.body.c_str()), length))
      esp_now_send(broadcast, reinterpret_cast<uint8_t *>(&packet), offsetof(EspNowPacket, payload) + packet.payloadLength);
  }
  draft = ""; historyOffset = 0; replyToMessageId = 0; replyToAuthor = ""; replyToBody = ""; renderDirty = true;
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
  const int selection = menuPage * kMenuItemsPerPage + menuSelections[menuPage];
  if (selection == 0) screenMode = ScreenMode::Chat;
  else if (selection == 1) screenMode = ScreenMode::Rooms;
  else if (selection == 2) { if (!localOnlyMode) syncOverride = true; networkStatus = localOnlyMode ? "ESPNOW LOCAL" : "SYNC REQUESTED"; screenMode = ScreenMode::Chat; }
  else if (selection == 3) { screenMode = ScreenMode::VoiceMessages; walkieStatus = "READY"; }
  else if (selection == 4) { screenMode = ScreenMode::Walkie; walkieStatus = localOnlyMode ? "ESPNOW LOCAL" : "READY TO TALK"; }
  else if (selection == 5) screenMode = ScreenMode::Volume;
  else if (selection == 6) screenMode = ScreenMode::Language;
  else if (selection == 7) { if (!localOnlyMode) { screenMode = ScreenMode::Networks; scanForNetworks(); } }
  else if (selection == 8) setLocalOnlyMode(!localOnlyMode);
  else if (selection == 9) screenMode = ScreenMode::ChargingConfirm;
  else if (selection == 10) screenMode = ScreenMode::Status;
  else if (selection == 11) screenMode = ScreenMode::Changelog;
  else if (selection == 12) screenMode = ScreenMode::EmojiRecipes;
  else {
    screenMode = ScreenMode::FoxFinding; foxState = FoxState::Selecting; foxPeerSelection = 0;
    // Discovery cannot begin while participants remain on unrelated Wi-Fi
    // channels. Enter the shared ESP-NOW channel before waiting for peers.
    if (!localOnlyMode) {
      foxForcedLocalOnly = true; foxPeerCount = 0; setLocalOnlyMode(true);
    }
    sendEspNowBeacon();
  }
  renderDirty = true;
}

void selectRoom(int next) {
  if (next < 0 || next >= static_cast<int>(rooms.size()) || rooms[next].id == currentRoomId) return;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  saveHistoryLocked();
  currentRoomId = rooms[next].id; currentRoomName = rooms[next].name; roomSelection = next;
  applyEffectiveLanguage();
  messages.clear(); lastServerId = 0; lastReceiptAt = 0; historyOffset = 0; initialSyncComplete = false;
  historyBeforeId = 0; historyHydratedCount = 0; syncOverride = true;
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
  if (inputQuarantined) {
    if (!M5Cardputer.Keyboard.isPressed() && !M5Cardputer.Keyboard.isChange() &&
        !M5Cardputer.BtnA.isPressed() && !M5Cardputer.BtnA.wasClicked()) inputQuarantined = false;
    return;
  }
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
  lastUserInputAt = millis();
  const auto keys = M5Cardputer.Keyboard.keysState();
  if (screenMode == ScreenMode::Charging && chargingDimmed) {
    chargingDimmed = false;
    M5Cardputer.Display.setBrightness(kChargeBrightness);
    renderDirty = true;
    return;
  }
  // Shift+/ is '?', Shift+. is '>', etc. Shifted punctuation remains text;
  // only Fn selects the arrow layer while a text field is active.
  // Text-entry screens prioritize punctuation and require Fn for arrows. On
  // navigation screens, the coloured arrows are primary and need no Fn.
  const bool textEntryScreen = screenMode == ScreenMode::Chat || screenMode == ScreenMode::NetworkPassword;
  const bool otherModifier = keys.shift || keys.ctrl || keys.alt || keys.opt;
  // Text entry requires Fn to select the physical arrow layer. Navigation
  // screens treat those physical positions as arrows with or without Fn.
  const bool navigationChord = !otherModifier && (textEntryScreen ? keys.fn : !keys.fn);
  const bool goUp = navigationChord && navUp();
  const bool goDown = navigationChord && navDown();
  const bool goLeft = navigationChord && navLeft();
  const bool goRight = navigationChord && navRight();
  if (screenMode == ScreenMode::Menu) {
    const int pageItems = kMenuItemCounts[menuPage];
    if (goUp) { menuSelections[menuPage] = (menuSelections[menuPage] + pageItems - 1) % pageItems; playNextTone(); }
    else if (goDown) { menuSelections[menuPage] = (menuSelections[menuPage] + 1) % pageItems; playNextTone(); }
    else if (goLeft) { menuPage = (menuPage + kMenuPageCount - 1) % kMenuPageCount; playNextTone(); }
    else if (goRight) { menuPage = (menuPage + 1) % kMenuPageCount; playNextTone(); }
    else if (keys.enter) { openSelectedMenuItem(); playNextTone(); }
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::ChargingConfirm) {
    if (goLeft) { screenMode = ScreenMode::Menu; playNextTone(); }
    else if (keys.enter) enterChargingMode();
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::Charging) {
    if (goLeft) exitChargingMode();
    else if (keys.enter) {
      chargingDimmed = true;
      M5Cardputer.Display.setBrightness(kChargeDimBrightness);
    }
    return;
  }
  if (screenMode == ScreenMode::FoxFinding) {
    if (goLeft) { stopFoxFinding(); screenMode = ScreenMode::Menu; playNextTone(); }
    else if ((foxState == FoxState::Idle || foxState == FoxState::Selecting) && goUp && foxPeerSelection > 0) { foxPeerSelection--; playNextTone(); }
    else if ((foxState == FoxState::Idle || foxState == FoxState::Selecting) && goDown && foxPeerSelection + 1 < static_cast<int>(foxPeerCount)) { foxPeerSelection++; playNextTone(); }
    else if ((foxState == FoxState::Idle || foxState == FoxState::Selecting) && keys.enter && foxPackInviteSession && millis() - foxPackInviteAt <= supachat::fox::kPeerFreshMs) { joinPackHunt(); playNextTone(); }
    else if ((foxState == FoxState::Idle || foxState == FoxState::Selecting) && keys.enter && foxPeerCount) { beginFoxFinding(); playNextTone(); }
    renderDirty = true; return;
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
  if (screenMode == ScreenMode::VoiceMessages || screenMode == ScreenMode::Walkie) {
    if (screenMode == ScreenMode::VoiceMessages && goUp && voiceInboxSelection > 0) { voiceInboxSelection--; playNextTone(); }
    else if (screenMode == ScreenMode::VoiceMessages && goDown) { const auto inbox = voiceInbox(); if (voiceInboxSelection + 1 < static_cast<int>(inbox.size())) voiceInboxSelection++; playNextTone(); }
    else if (screenMode == ScreenMode::VoiceMessages && keys.enter && !voiceRecording) playSelectedVoiceMessage();
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
    if (goLeft) { frenchGravePending = false; screenMode = ScreenMode::Networks; networkPassword = ""; playNextTone(); renderDirty = true; return; }
    playNextTone();
    if (keys.enter) { flushFrenchDeadKey(networkPassword, 63, true); joinSelectedNetwork(); return; }
    if (keys.del) {
      if (frenchGravePending) frenchGravePending = false;
      else removeLastUtf8Character(networkPassword);
    }
    if (keys.space) typeFrenchCharacter(networkPassword, ' ', 63, true);
    for (const auto character : keys.word)
      if (character >= 0x20 && character <= 0x7e) typeFrenchCharacter(networkPassword, character, 63, true);
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::Language) {
    if (goLeft || goRight) {
      if (languageOverride == "auto") languageOverride = goRight ? "en" : "fr";
      else if (languageOverride == "en") languageOverride = goRight ? "fr" : "auto";
      else languageOverride = goRight ? "auto" : "en";
      saveLanguageOverride(); playNextTone();
    } else if (keys.enter) { screenMode = ScreenMode::Menu; playNextTone(); }
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::Status) {
    if (goLeft || goRight) { statusPage = 1 - statusPage; playNextTone(); }
    else if (keys.enter) { statusPage = 0; screenMode = ScreenMode::Menu; playNextTone(); }
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::Changelog) {
    const auto &entry = kSupaChatChangelog[changelogSelection];
    if (goUp && changelogLineOffset > 0) { changelogLineOffset--; playNextTone(); }
    else if (goDown && changelogLineOffset + 4 < entry.lineCount) { changelogLineOffset++; playNextTone(); }
    else if (goLeft && changelogSelection > 0) { changelogSelection--; changelogLineOffset = 0; playNextTone(); }
    else if (goRight && changelogSelection + 1 < kSupaChatChangelogCount) { changelogSelection++; changelogLineOffset = 0; playNextTone(); }
    else if (keys.enter) { screenMode = ScreenMode::Menu; playNextTone(); }
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::EmojiRecipes) {
    if (goUp && emojiRecipeSelection > 0) emojiRecipeSelection--;
    else if (goDown && emojiRecipeSelection + 1 < kEmojiRecipeCount) emojiRecipeSelection++;
    else if (goLeft || keys.enter) screenMode = ScreenMode::Menu;
    playNextTone(); renderDirty=true; return;
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
  if (keys.enter) {
    if (draft.isEmpty() && historyOffset > 0) {
      xSemaphoreTake(stateMutex, portMAX_DELAY); const int selected=static_cast<int>(messages.size())-static_cast<int>(historyOffset)-1;
      if(selected>=0){replyToMessageId=messages[selected].id; replyToAuthor=messages[selected].authorName; replyToBody=messages[selected].body; historyOffset=0;}
      xSemaphoreGive(stateMutex); renderDirty=true; return;
    }
    sendDraft(); return;
  }
  if (keys.del) {
    if (frenchGravePending) frenchGravePending = false;
    else removeLastUtf8Character(draft);
    renderDirty = true; return;
  }
  if (keys.space) typeFrenchCharacter(draft, ' ', kMessageLimit, false);
  for (const auto character : keys.word)
    if (character >= 0x20 && character <= 0x7e) typeFrenchCharacter(draft, character, kMessageLimit, false);
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
  inputQuarantined = true;
  render();
  Serial.printf("SUPACHAT ready node=%s wifi_profiles=%u sd=%d keyboard=%d board=%d\n", kDeviceId,
                wifiProfiles.size(), sdReady, keyboardReady, static_cast<int>(M5.getBoard()));
}

void loop() {
  M5Cardputer.update();
  if (M5Cardputer.BtnA.isPressed()) lastUserInputAt = millis();
  sampleBattery(); handleKeyboard(); serviceFoxFinding();
  if (chargingModeActive) {
    serviceChargingMode();
    if (!inputQuarantined && M5Cardputer.BtnA.wasClicked()) {
      if (chargingDimmed) {
        chargingDimmed = false;
        lastUserInputAt = millis();
        M5Cardputer.Display.setBrightness(kChargeBrightness);
        renderDirty = true;
      } else exitChargingMode();
    }
    if (!chargingDimmed && renderDirty && millis() - lastRenderAt >= 1000) render();
    delay(20); return;
  }
  serviceClockRender(); captureVoice(); serviceAudioPlayback(); serviceMessageNotification();
  if (screenMode == ScreenMode::VoiceMessages || screenMode == ScreenMode::Walkie) {
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
  } else if (!inputQuarantined && M5Cardputer.BtnA.wasClicked()) {
    playNextTone(); screenMode = screenMode == ScreenMode::Menu ? ScreenMode::Chat : ScreenMode::Menu; renderDirty = true;
  }
  if (screenMode != ScreenMode::VoiceMessages && screenMode != ScreenMode::Walkie && spacePttHeld) { spacePttHeld = false; spaceReleaseStartedAt = 0; stopVoiceRecording(); }
  if (kEspNowEnabled && millis() - lastEspNowBeaconAt >= kEspNowBeaconMs) sendEspNowBeacon();
  if (screenMode == ScreenMode::Status && statusPage == 1 && lastSyncError != "NONE" && millis() - lastRenderAt >= 1000) renderDirty = true;
  if (renderDirty && millis() - lastRenderAt >= kRenderIntervalMs) render();
  delay(2);
}
