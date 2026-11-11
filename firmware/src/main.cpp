#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include "changelog.h"
#include "clock_apps.h"
#include "fox_finding.h"
#include "french_keyboard.h"
#include "outbox.h"
#include "voice_codec.h"
#include <WebSocketsClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <mbedtls/gcm.h>
#include <algorithm>
#include <array>
#include <deque>
#include <ctime>
#include <vector>

#if __has_include("music_secrets.h")
#include "music_secrets.h"
#endif
#include "keypress_song.h"
#include "media_player.h"
#include "music_library.h"
#include "os.h"
#include "shared_text.h"
#include <new>
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
// SHA-256 fingerprint of supachat.net's leaf certificate, issued 2026-10-09, expiring 2027-01-07.
// Since v0.57 this is only the fallback: connections first check the certificate chain against
// ISRG Root X1 (kRootCa), which survives Caddy's renewals. A leaf pin breaks at every renewal:
// v0.48 pinned the August certificate and has reached no server since Caddy replaced it on Oct 9.
// The pin still helps while the clock is unset (chain checks need the date).
constexpr char kTlsFingerprint[] = "B1 42 A0 82 80 8D CD A1 41 AC 56 43 65 E3 18 E7 96 EB D0 38 DB 01 1B 56 87 19 3A 0E 5D DC 31 6D";
constexpr char kDeviceName[] = SUPACHAT_DEVICE_NAME;
constexpr char kDeviceId[] = SUPACHAT_DEVICE_ID;
#if defined(SUPACHAT_LANGUAGE_FR)
constexpr bool kBuildFrenchDefault = true;
#else
constexpr bool kBuildFrenchDefault = false;
#endif
bool frenchUi = kBuildFrenchDefault;
SharedText<8> languageOverride{"auto"};
const char *uiText(const char *english, const char *french) { return frenchUi ? french : english; }
// The library's and the player's failure texts are English; a French device shows these.
const char *failureText(const char *english) {
  static const char *const kFrench[][2] = {
      {"CARD NOT READ: LOW RAM", "CARTE NON LUE: MEMOIRE"},
      {"NOT AN MP3", "PAS UN MP3"},
      {"MP2, NOT MP3", "MP2, PAS UN MP3"},
      {"8-12 KHZ MP3: UNSUPPORTED", "MP3 8-12 KHZ: NON LU"},
  };
  if (frenchUi) for (const auto &pair : kFrench) if (!strcmp(english, pair[0])) return pair[1];
  return english;
}
constexpr char kFirmwareVersion[] = "v0.59";
constexpr size_t kMessageLimit = 140;
constexpr size_t kHistoryLimit = 100;  // What the first sync fetches; older pages go to the card.
constexpr size_t kRamMessageLimit = 40;
constexpr size_t kShrunkMessages = 5;
constexpr uint32_t kToneIntervalMs = 40;
constexpr uint32_t kToneDurationMs = 180;
constexpr uint8_t kDefaultVolumeLevel = 3;
constexpr uint8_t kVolumeValues[] = {0, 64, 128, 192, 255};
constexpr char const *kVolumeNames[] = {"MUTE", "LOW", "MEDIUM", "HIGH", "MAX"};
constexpr uint32_t kRenderIntervalMs = 50;
constexpr uint32_t kWifiRetryMs = 30000;
constexpr uint32_t kSyncPollMs = 3000;
constexpr uint32_t kMediaSyncPollMs = 60000;
constexpr uint32_t kBookmarkEveryMs = 30000;
constexpr uint32_t kBookSkipSeconds = 30;
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
constexpr char kChargeLogOldPath[] = "/supachat-charge-old.csv";
constexpr size_t kChargeLogMaxBytes = 256 * 1024;
constexpr uint32_t kSecureConnectTimeoutMs = 8000;
constexpr uint32_t kSyncBackoffMaxMs = 300000;
constexpr uint32_t kWifiRetryMaxMs = 300000;
constexpr size_t kScanListLimit = 24;
constexpr uint32_t kChatReconcileMs = 300000;
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
#ifdef SUPACHAT_NO_RADIO
// QEMU has no radio, and WiFi.mode() never returns there. Everything else runs as on the device.
constexpr bool kEspNowEnabled = false;
#else
constexpr bool kEspNowEnabled = true;
#endif
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
  int8_t mark;  // -1, or on the last line of a message of ours: see MessageMark.
};
// How far a message of ours has got, drawn at the right of its last line.
enum MessageMark : int8_t { MarkSending, MarkHeardNearby, MarkOnServer, MarkDelivered, MarkRead, MarkFailed };
struct ChatRoom { String id; String name; String groupId; String groupName; String defaultLanguage; int64_t latestMessageId; int64_t seenMessageId; };
enum class ChargeTrend : uint8_t { Waiting, Rising, Flat, Falling, Unstable };
struct ChargePlotPoint {
  uint32_t elapsedSeconds = 0;
  uint16_t filteredMv = 0;
  uint8_t estimatedPercent = 0;
  ChargeTrend trend = ChargeTrend::Waiting;
};
void applyEffectiveLanguage();
void stopMediaNow();
bool mediaOwnsRadio();
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

// Guards rooms and wifiProfiles, which the UI and network tasks share. A leaf: nothing else is
// taken while it is held.
SemaphoreHandle_t sharedListsMutex = nullptr;
struct ListsLock {
  ListsLock() { xSemaphoreTake(sharedListsMutex, portMAX_DELAY); }
  ~ListsLock() { xSemaphoreGive(sharedListsMutex); }
};

enum class ScreenMode { Chat, Menu, Rooms, Volume, Language, EmojiRecipes, Changelog, VoiceMessages, Walkie, Status, Networks, NetworkPassword, ChargingConfirm, Charging, FoxFinding, Home, Library, LibrarySearch, NowPlaying, Remote, System, Timer, Alarm, ScreenTime, Stopwatch, KeyHelp };

Preferences preferences;
M5Canvas uiCanvas(&M5Cardputer.Display);
WebSocketsClient walkieSocket;
std::vector<WifiProfile> wifiProfiles;
std::vector<ScannedNetwork> scannedNetworks;
std::vector<ChatMessage> messages;
std::vector<ChatRoom> rooms;
SharedText<40> currentRoomId{kDefaultRoomId};
SharedText<40> currentRoomName{kDefaultRoomName};
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
SharedText<32> networkStatus{"BOOT"};
SharedText<32> walkieStatus{"OFFLINE"};
SharedText<32> walkieSpeaker;
SharedText<33> currentSsid;
String selectedSsid;
String networkPassword;
frkb::Typist frenchTypist;  // The French layout's pending dead key, in chat.
int keyHelpPage = 0;
String voiceClientId;
ScreenMode screenMode = ScreenMode::Home;
bool sdReady = false;
bool renderDirty = true;
bool syncOverride = false;
bool timeKnown = false;
bool ntpAttempted = false;
bool timeSyncedByNtp = false;
bool initialSyncComplete = false;
volatile bool chatShrunk = false;
volatile bool networkTaskParked = false;
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
volatile bool radioOffWanted = false;
volatile bool radioIsOff = false;
uint32_t lastCardMediaAt = 0;
constexpr uint32_t kRadioOffWaitMs = 4000;
constexpr uint8_t kListeningToneVolume = 36;
// The radio stays off across the gap between chapters and short pauses in use.
constexpr uint32_t kRadioBackAfterMs = 10000;
volatile int64_t voiceDownloadRequestedId = 0;
volatile bool voicePlaybackCancelled = false;
volatile bool voiceDownloadActive = false;
int64_t voicePlayingMessageId = 0;
int voiceInboxSelection = 0;
int menuSelection = 0;
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
SharedText<24> lastSyncError{"NONE"};
SharedText<64> lastSyncDetail;
SharedText<40> lastSyncErrorRoom;
int lastSyncErrorHttpStatus = 0;
size_t lastSyncResponseBytes = 0;
uint32_t lastSyncErrorHeap = 0;
uint32_t lastSyncErrorAt = 0;
bool lastSyncErrorInitial = false;

// The outbox (outbox.h): typed messages the server hasn't confirmed. Kept in NVS. Guarded by stateMutex.
std::vector<outbox::Entry> outboxEntries;
SharedText<40> chatNotice;            // One line over the composer, e.g. "OUTBOX FULL".
uint32_t chatNoticeAt = 0;

// How the last attempt to reach the server went, step by step, for STATUS and the header.
enum class ServerStage : uint8_t { Unknown, NoWifi, Dns, Tcp, Tls, Cert, Auth, Http, Ok };
volatile ServerStage serverStage = ServerStage::Unknown;
volatile int serverStageStatus = 0;       // HTTP status for Auth/Http.
SharedText<48> serverStageDetail;          // mbedTLS's words, "via pin", the DNS answer.
volatile uint32_t serverStageAt = 0;
volatile uint32_t serverOkAt = 0;          // millis() of the last answer from the server.
volatile bool tlsChainWorks = false;       // The last good connection passed the chain check.
volatile int8_t wifiRssi = 0;
constexpr uint32_t kOnlineFreshMs = 120000;

// Nearby (ESP-NOW) copies of typed messages, rebroadcast until a nearby device acknowledges.
struct NearbySend { String clientId; uint32_t firstAt; uint32_t nextAt; uint8_t tries; uint8_t heardBy; };
std::vector<NearbySend> nearbySends;       // Guarded by stateMutex.
std::vector<String> pendingNearbyAcks;     // Guarded by stateMutex.
volatile bool radioAwake = false;         // Modem sleep off; see applyRadioSleep().
constexpr uint8_t kNearbyTries = 7;        // At 0, 1, 2, 4, 8, 16 and 30 s.

// Walkie playback: ESP-NOW frames go through a jitter buffer; walkie-socket audio (in order, over
// TCP) through a plain FIFO that waits for a cushion before it plays.
voice::JitterBuffer nearbyJitter;          // Guarded by stateMutex.
std::deque<int16_t> socketAudio;           // Guarded by stateMutex.
bool socketAudioPlaying = false;
constexpr size_t kSocketCushionSamples = 1920;  // 240 ms: TLS over Wi-Fi arrives in bursts.
constexpr size_t kSocketAudioMax = 8000;        // 1 s; older audio is dropped beyond this.
constexpr size_t kPlayBlockSamples = 480;       // 60 ms handed to the speaker at a time.
voice::Agc micAgc;
voice::AdpcmState nearbyEncoder;
int16_t nearbyPending[voice::kFrameSamples];
size_t nearbyPendingCount = 0;
uint16_t nearbySequence = 0;
uint16_t nearbyTalk = 0;
std::vector<uint8_t> nearbyLastPacket;     // Sent again with the next frame: a lost packet gets a second chance.
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
// The clock apps: timer, alarm, screen time and stopwatch. They run from the loop, whatever is on the screen.
clockapps::Countdown timerCountdown;
uint16_t timerTyped = 0;
clockapps::ScreenTime screenTime;
clockapps::Alarms alarms;
clockapps::TimeEntry alarmTyped;  // The time being typed on the selected row.
// The alarm list's cursor: a row (an alarm, or the last row that adds one) and a column on it.
enum class AlarmColumn : uint8_t { Time, Weekdays, Weekends };
uint8_t alarmRow = 0;
AlarmColumn alarmColumn = AlarmColumn::Time;
uint32_t alarmBumpAt = 0;
clockapps::Stopwatch stopwatch;
// Set by SNTP when a time server has answered. The clock seeded from the build time is not good enough to ring by.
volatile bool clockFromNtp = false;
bool alarmRinging = false;
uint32_t ringStartedAt = 0;
uint32_t ringKeysFrom = 0;
uint32_t nextRingToneAt = 0;
uint8_t ringStep = 0;
clockapps::Warning bannerWarning = clockapps::Warning::None;
uint32_t bannerUntil = 0;
uint32_t lastAlarmCheckAt = 0;
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
// Types 11 and 12 are new in v0.57. Older firmware decrypts and ignores them, and still sends type 3.
enum class EspNowType : uint8_t { Beacon = 1, Text = 2, Audio = 3, FoxBeacon = 4, FindStart = 5, FindAck = 6, PlacePrintRequest = 7, PlacePrintResult = 8, FindEnd = 9, PackObservation = 10, TextAck = 11, AudioAdpcm = 12 };
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
  currentRoomId.copyTo(packet.roomId, sizeof(packet.roomId));
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

// The splash's status line, so that a wait is explained.
void drawBootStatus(os::Boot phase) {
  auto &display = M5Cardputer.Display;
  display.setFont(&fonts::Font0); display.setTextSize(1); display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.fillRect(60, 125, 180, 10, TFT_BLACK); display.setCursor(64, 125); display.print(os::bootText(phase));
}

void showBootSplash() {
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  size_t step = 0;
  const uint32_t bootStarted = millis();
  os::Boot shownPhase = os::Boot::Joining;
  drawBootStatus(shownPhase);
  for (;;) {
    // Control comes when chat's first sync has settled, or when it can't proceed.
    if (os::boot() != shownPhase) { shownPhase = os::boot(); drawBootStatus(shownPhase); }
    if (os::bootSettled()) { M5Cardputer.Speaker.stop(); return; }
    if (millis() - bootStarted >= os::kBootMaxMs) { os::setBoot(os::Boot::TimedOut); M5Cardputer.Speaker.stop(); return; }
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
        // A skip: chat keeps syncing, in the background and under its budget.
        os::setBoot(os::Boot::Skipped); M5Cardputer.Speaker.stop(); return;
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

// Keeps the newest messages in RAM. Unsent ones of this device's are never dropped here: until
// v0.57 forty newer messages pushed an unsent one off the list, and it was gone.
void trimHistory() {
  for (auto item = messages.begin(); messages.size() > kRamMessageLimit && item != messages.end();) {
    if (item->queued) ++item; else item = messages.erase(item);
  }
}

// --- The outbox: NVS copy, and keeping chat's list in step with it. Callers hold stateMutex. ---
void saveOutboxLocked() {
  std::vector<outbox::Entry> copy = outboxEntries;
  const std::string text = outbox::serialize(copy);
  preferences.begin("supachat", false);
  if (text.empty()) preferences.remove("outbox"); else preferences.putString("outbox", text.c_str());
  preferences.end();
}

void loadOutboxLocked() {
  preferences.begin("supachat", true);
  const String text = preferences.getString("outbox", "");
  preferences.end();
  outboxEntries = outbox::parse(std::string(text.c_str()));
}

outbox::Entry *findOutboxLocked(const String &clientId) {
  for (auto &entry : outboxEntries) if (clientId == entry.clientId.c_str()) return &entry;
  return nullptr;
}

String outboxStateName(const outbox::Entry &entry) { return entry.state == outbox::State::Failed ? "failed" : "queued"; }

// After a room's history loads: unsent messages from the card that the outbox doesn't know (sent
// from firmware before v0.57, or the card was swapped) join the outbox, and outbox messages the
// card lacks (no card, or a write that failed) join the list.
void reconcileOutboxLocked() {
  bool changed = false;
  const String roomId = currentRoomId.get();
  for (auto &message : messages) {
    if (!message.queued || message.authorId != kDeviceId || message.body.isEmpty()) continue;
    if (outbox::Entry *entry = findOutboxLocked(message.clientId)) { message.state = outboxStateName(*entry); continue; }
    if (outboxEntries.size() >= outbox::kMaxEntries) continue;
    outbox::Entry entry; entry.clientId = message.clientId.c_str(); entry.roomId = roomId.c_str(); entry.body = message.body.c_str();
    entry.replyToId = message.replyToId; entry.createdAt = message.createdAt;
    outboxEntries.push_back(entry); message.state = "queued"; changed = true;
  }
  for (const auto &entry : outboxEntries) {
    if (roomId != entry.roomId.c_str()) continue;
    const String clientId = entry.clientId.c_str();
    if (std::any_of(messages.begin(), messages.end(), [&](const ChatMessage &m) { return m.clientId == clientId; })) continue;
    ChatMessage message; message.roomId = roomId; message.clientId = clientId; message.authorId = kDeviceId; message.authorName = kDeviceName;
    message.body = entry.body.c_str(); message.createdAt = entry.createdAt; message.queued = true; message.replyToId = entry.replyToId;
    message.state = outboxStateName(entry);
    messages.push_back(message);
  }
  if (changed) saveOutboxLocked();
}

void setMessageStateLocked(const String &clientId, const String &state) {
  for (auto &message : messages) {
    if (message.clientId != clientId || (state == "queued" && message.state == "heard")) continue;
    message.state = state; renderDirty = true;
  }
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
  bool complete = true;
  for (const auto &message : messages) {
    complete = file.printf("%lld\t%s\t%s\t%s\t%s\t%lld\t%d\n", message.id, cleanField(message.clientId).c_str(),
                cleanField(message.authorId).c_str(), cleanField(message.authorName).c_str(), cleanField(message.body).c_str(),
                message.createdAt, message.queued ? 1 : 0) > 0 && complete;
  }
  file.close();
  if (!complete) { SD.remove(temporaryPath); return; }  // The card is full: keep what was saved.
  SD.remove(path);
  SD.rename(temporaryPath, path);
}

void loadHistoryFile();
void loadHistory() { loadHistoryFile(); reconcileOutboxLocked(); }
void loadHistoryFile() {
  if (!sdReady) return;
  String path = roomHistoryPath(currentRoomId);
  // Power lost between the save's remove and rename leaves only the new copy, which is whole.
  if (!SD.exists(path) && SD.exists(roomHistoryPath(currentRoomId, true))) path = roomHistoryPath(currentRoomId, true);
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
  uint8_t packedAlarms[clockapps::Alarms::kPackedBytes];
  const size_t packedLength = preferences.getBytesLength("alarms");
  if (packedLength && packedLength <= sizeof(packedAlarms)) {
    preferences.getBytes("alarms", packedAlarms, packedLength);
    alarms.unpack(packedAlarms, static_cast<int>(packedLength));
  } else if (preferences.isKey("alarm_h") && alarms.add()) {
    // v0.56 and v0.57 had one alarm with a repeat: Monday to Friday, every day, or once (now off).
    clockapps::AlarmEntry &entry = alarms.entries[0];
    entry.hour = std::min<uint8_t>(preferences.getUChar("alarm_h", 7), 23);
    entry.minute = std::min<uint8_t>(preferences.getUChar("alarm_m", 0), 59);
    const uint8_t repeat = preferences.getUChar("alarm_rep", 0), on = preferences.getBool("alarm_on", false);
    entry.weekdays = on && (repeat & 0x3E);
    entry.weekends = on && (repeat & 0x41);
  }
  timerCountdown.set(std::min<uint32_t>(preferences.getUInt("timer_ms", 5 * 60000u), (99 * 60 + 59) * 1000u));
  screenTime.countdown.set(std::min<uint32_t>(preferences.getUInt("screen_ms", 30 * 60000u), 120 * 60000u));
  preferences.end();
}

void saveClockSettings() {
  preferences.begin("supachat", false);
  uint8_t packedAlarms[clockapps::Alarms::kPackedBytes];
  preferences.putBytes("alarms", packedAlarms, alarms.pack(packedAlarms));
  preferences.putUInt("timer_ms", timerCountdown.durationMs); preferences.putUInt("screen_ms", screenTime.countdown.durationMs);
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
  ListsLock lists;
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

// Tones are full-scale and speech isn't, so beside a book or song they sit well below it.
uint8_t toneChannelVolume() {
  const bool listening = media::active() || screenMode == ScreenMode::Library || screenMode == ScreenMode::LibrarySearch || screenMode == ScreenMode::NowPlaying;
  return listening ? kListeningToneVolume : 255;
}

void playNextTone() {
  if (chargingModeActive || voiceRecording || audioPlaying || messageNotificationActive) return;
  const uint32_t now = millis();
  if (now - lastToneAt < kToneIntervalMs || kKeypressSongLength == 0) return;
  lastToneAt = now;
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  M5Cardputer.Speaker.setChannelVolume(0, toneChannelVolume());
  M5Cardputer.Speaker.tone(kKeypressSongFrequencies[songPosition], kToneDurationMs, 0, true);
  songPosition = (songPosition + 1) % kKeypressSongLength;
}

// "Can't go there": two falling notes, unlike anything in the key song.
void playDeniedTone() {
  if (chargingModeActive || voiceRecording || audioPlaying || messageNotificationActive) return;
  lastToneAt = millis();
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  M5Cardputer.Speaker.setChannelVolume(0, toneChannelVolume());
  M5Cardputer.Speaker.tone(294, 70, 0, true);
  M5Cardputer.Speaker.tone(196, 120, 0, false);
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
void flushNearbyFrame(bool last);
void processVoiceBlock(const int16_t *rawSamples) {
  if (chargingModeActive) return;
  // The level control works on a copy: the DMA block is the recorder's.
  int16_t samples[kVoiceCaptureBlock]; memcpy(samples, rawSamples, sizeof(samples));
  micAgc.process(samples, kVoiceCaptureBlock);
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
  stopMediaNow();
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
  micAgc.reset(); nearbyEncoder = voice::AdpcmState(); nearbyPendingCount = 0; nearbySequence = 0; nearbyTalk = static_cast<uint16_t>(esp_random());
  nearbyLastPacket.clear();
  memset(voiceDmaBlocks, 0, sizeof(voiceDmaBlocks));
  M5Cardputer.Speaker.stop(); M5Cardputer.Speaker.end();
  auto micConfig = M5Cardputer.Mic.config(); micConfig.sample_rate = kVoiceSampleRate;
  // Was 48 (three times M5's default) until v0.57, which clips a voice close to the microphone
  // before anything here can help. The level control (voice::Agc) adds up to 6x for a quiet voice.
  micConfig.magnification = 16; micConfig.noise_filter_level = 8;
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
  if (voiceLiveMode && kEspNowEnabled && !walkieConnected) flushNearbyFrame(true);
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

bool sendEspNowRaw(const std::vector<uint8_t> &bytes) {
  static const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
  return !bytes.empty() && esp_now_send(broadcast, bytes.data(), bytes.size()) == ESP_OK;
}

// One 30 ms ADPCM frame per packet (voice_codec.h). Each packet goes out again with the next one,
// so a frame is lost only if both copies are; the receiver's replay check drops the copy it already has.
void flushNearbyFrame(bool last) {
  if (!meshReady || !espNowActive) { nearbyPendingCount = 0; return; }
  if (nearbyPendingCount == 0 && !last) return;
  if (nearbyPendingCount < voice::kFrameSamples) memset(nearbyPending + nearbyPendingCount, 0, (voice::kFrameSamples - nearbyPendingCount) * sizeof(int16_t));
  voice::Frame frame{}; frame.sequence = nearbySequence++; frame.talk = nearbyTalk; frame.flags = last ? 1 : 0;
  voice::encodeFrame(nearbyEncoder, nearbyPending, frame);
  nearbyPendingCount = 0;
  EspNowPacket packet{}; packet.magic = kEspNowMagic; packet.type = static_cast<uint8_t>(EspNowType::AudioAdpcm); packet.sequence = ++espNowSequence;
  strncpy(packet.senderId, kDeviceId, sizeof(packet.senderId) - 1); strncpy(packet.senderName, kDeviceName, sizeof(packet.senderName) - 1);
  currentRoomId.copyTo(packet.roomId, sizeof(packet.roomId));
  if (!encryptMeshPacket(packet, reinterpret_cast<const uint8_t *>(&frame), sizeof(frame))) return;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&packet);
  std::vector<uint8_t> current(bytes, bytes + offsetof(EspNowPacket, payload) + packet.payloadLength);
  sendEspNowRaw(current);
  sendEspNowRaw(nearbyLastPacket);
  if (last) sendEspNowRaw(current);  // The last frame has no next packet to ride with.
  nearbyLastPacket.swap(current);
}

void sendEspNowAudio(const int16_t *samples, size_t count) {
  if (!meshReady) return;
  for (size_t offset = 0; offset < count;) {
    const size_t take = std::min(count - offset, voice::kFrameSamples - nearbyPendingCount);
    memcpy(nearbyPending + nearbyPendingCount, samples + offset, take * sizeof(int16_t));
    nearbyPendingCount += take; offset += take;
    if (nearbyPendingCount == voice::kFrameSamples) flushNearbyFrame(false);
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

// Live walkie audio, whichever way it came, goes to the speaker 60 ms at a time with two blocks
// queued, so a slow screen redraw doesn't leave the speaker empty.
bool serviceLiveAudio() {
  std::vector<int16_t> block;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (nearbyJitter.active()) {
    block.resize(kPlayBlockSamples);
    size_t filled = 0;
    while (filled < kPlayBlockSamples && nearbyJitter.pull(block.data() + filled, millis())) filled += voice::kFrameSamples;
    block.resize(filled);
  } else if (!socketAudio.empty()) {
    if (!socketAudioPlaying && socketAudio.size() >= kSocketCushionSamples) socketAudioPlaying = true;
    if (socketAudioPlaying) {
      const size_t take = std::min(kPlayBlockSamples, socketAudio.size());
      block.assign(socketAudio.begin(), socketAudio.begin() + take);
      socketAudio.erase(socketAudio.begin(), socketAudio.begin() + take);
    }
  }
  if (socketAudio.empty()) socketAudioPlaying = false;  // Ran dry: wait for a fresh cushion.
  xSemaphoreGive(stateMutex);
  if (block.empty()) return false;
  return queuePlaybackSamples(std::move(block));
}

void serviceAudioPlayback() {
  if (chargingModeActive) return;
  audioPlaying = M5Cardputer.Speaker.isPlaying(0) > 0;
  if (voiceRecording) return;
  while (M5Cardputer.Speaker.isPlaying(0) < 2 && serviceLiveAudio()) {}
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
  M5Cardputer.Speaker.stop(0); audioPlaying = false;
  xSemaphoreTake(stateMutex, portMAX_DELAY); incomingAudio.clear(); socketAudio.clear(); socketAudioPlaying = false; nearbyJitter.reset(); xSemaphoreGive(stateMutex);
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
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    for (size_t at = 0; at + 1 < length; at += 2) { int16_t sample; memcpy(&sample, payload + at, 2); socketAudio.push_back(sample); }
    while (socketAudio.size() > kSocketAudioMax) socketAudio.pop_front();
    xSemaphoreGive(stateMutex);
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
    // Raw PCM from firmware before v0.57: it gets the same cushion as walkie-socket audio.
    if (stateMutex) {
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      for (size_t at = 0; at + 1 < plain.size(); at += 2) { int16_t sample; memcpy(&sample, plain.data() + at, 2); socketAudio.push_back(sample); }
      while (socketAudio.size() > kSocketAudioMax) socketAudio.pop_front();
      xSemaphoreGive(stateMutex);
    }
  } else if (packet->type == static_cast<uint8_t>(EspNowType::AudioAdpcm) && plain.size() == sizeof(voice::Frame)) {
    voice::Frame frame; memcpy(&frame, plain.data(), sizeof(frame));
    if (stateMutex) { xSemaphoreTake(stateMutex, portMAX_DELAY); nearbyJitter.push(frame, millis()); xSemaphoreGive(stateMutex); }
  } else if (packet->type == static_cast<uint8_t>(EspNowType::TextAck)) {
    char clientId[sizeof(packet->clientId) + 1]{}; memcpy(clientId, packet->clientId, sizeof(packet->clientId));
    if (stateMutex) {
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      for (auto &send : nearbySends) if (send.clientId == clientId && send.heardBy < 255) send.heardBy++;
      for (auto &message : messages)
        if (message.clientId == clientId && message.authorId == kDeviceId && message.queued) { message.state = message.state == "failed" ? message.state : String("heard"); renderDirty = true; }
      xSemaphoreGive(stateMutex);
    }
  } else if (packet->type == static_cast<uint8_t>(EspNowType::Text) && packet->payloadLength > 0) {
    char clientId[sizeof(packet->clientId) + 1]{}; memcpy(clientId, packet->clientId, sizeof(packet->clientId));
    char text[sizeof(packet->payload) + 1]{}; memcpy(text, plain.data(), std::min(plain.size(), sizeof(packet->payload)));
    ChatMessage message; message.roomId = currentRoomId; message.clientId = clientId; message.authorId = packet->senderId; message.authorName = packet->senderName;
    message.body = text; message.state = "nearby";
    if (stateMutex) {
      bool inserted = false;
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      if (std::none_of(messages.begin(), messages.end(), [&](const ChatMessage &item){ return item.clientId == message.clientId; })) {
        messages.push_back(message); trimHistory(); saveHistoryLocked(); inserted = true;
      }
      xSemaphoreGive(stateMutex);
      if (inserted) messageNotificationPending = true;
      // Acknowledge every copy, so a sender whose first answer was lost still hears one. Sent from
      // the main loop: this runs on the Wi-Fi task.
      xSemaphoreTake(stateMutex, portMAX_DELAY); if (pendingNearbyAcks.size() < 8) pendingNearbyAcks.push_back(String(clientId)); xSemaphoreGive(stateMutex);
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
  currentRoomId.copyTo(packet.roomId, sizeof(packet.roomId));
  esp_now_send(broadcast, reinterpret_cast<uint8_t *>(&packet), offsetof(EspNowPacket, payload)); lastEspNowBeaconAt = millis();
}

bool connectKnownWifi() {
  if (chargingModeActive) return false;
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  radioAwake = false; WiFi.setSleep(true);  // applyRadioSleep() decides once joined.
  networkStatus = "SCANNING"; renderDirty = true;
  const int count = WiFi.scanNetworks(false, true);
  int bestProfile = -1; int bestRssi = -1000;
  String joinSsid, joinPassword;
  {
    ListsLock lists;
    for (int scan = 0; scan < count; scan++) {
      for (int profile = 0; profile < static_cast<int>(wifiProfiles.size()); profile++) {
        if (WiFi.SSID(scan) == wifiProfiles[profile].ssid && WiFi.RSSI(scan) > bestRssi) { bestProfile = profile; bestRssi = WiFi.RSSI(scan); }
      }
    }
    if (bestProfile >= 0) { joinSsid = wifiProfiles[bestProfile].ssid; joinPassword = wifiProfiles[bestProfile].password; }
  }
  WiFi.scanDelete();
  if (chargingModeActive) return false;
  if (bestProfile < 0) {
    if (kEspNowEnabled) esp_wifi_set_channel(kEspNowFallbackChannel, WIFI_SECOND_CHAN_NONE);
    networkStatus = "ESPNOW ONLY"; return false;
  }
  networkStatus = "JOINING"; renderDirty = true;
  if (chargingModeActive) return false;
  WiFi.begin(joinSsid.c_str(), joinPassword.c_str());
  const uint32_t started = millis();
  while (!chargingModeActive && WiFi.status() != WL_CONNECTED && millis() - started < 12000) vTaskDelay(pdMS_TO_TICKS(100));
  if (chargingModeActive) { WiFi.disconnect(false, false); return false; }
  if (WiFi.status() != WL_CONNECTED) { WiFi.disconnect(false, false); networkStatus = "JOIN FAILED"; return false; }
  currentSsid = WiFi.SSID(); networkStatus = "ONLINE"; renderDirty = true;
  return true;
}

void onTimeSynced(struct timeval *) { clockFromNtp = true; }

// The clock is seeded from the build time, so a plausible year proves nothing: only SNTP's callback
// says a server answered. SNTP keeps polling after this, so a first try that fails is not the last.
void ensureTime() {
  if (ntpAttempted) return;
  ntpAttempted = true;
  sntp_set_time_sync_notification_cb(onTimeSynced);
  configTzTime("EST5EDT,M3.2.0/2,M11.1.0/2", "time.cloudflare.com", "time.google.com", "pool.ntp.org");
  const uint32_t started = millis();
  while (millis() - started < kTimeWaitMs) {
    if (clockFromNtp) { timeKnown = true; timeSyncedByNtp = true; return; }
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
  // One older log is kept beside the current one, so the pair can't outgrow the card.
  File existing = SD.open(kChargeLogPath, FILE_READ);
  if (existing) {
    const size_t size = existing.size();
    existing.close();
    if (size > kChargeLogMaxBytes) { SD.remove(kChargeLogOldPath); SD.rename(kChargeLogPath, kChargeLogOldPath); }
  }
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
  stopMediaNow();
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
  screenMode = ScreenMode::Home; renderDirty = true;
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

void noteServerStage(ServerStage stage, int status, const String &detail) {
  serverStage = stage; serverStageStatus = status; serverStageDetail = detail; serverStageAt = millis();
  if (stage == ServerStage::Ok) serverOkAt = millis();
  renderDirty = true;
}

bool chainFailedWithoutClock = false;

// After a failed TLS connection: was it the network, or TLS? A plain TCP connection answers that.
bool portOpen(const IPAddress &address, uint32_t) {
  // Short: a TLS attempt has just waited its full time, and chat holds the secure slot meanwhile.
  WiFiClient probe;
  if (probe.connect(address, 443, 3000)) { probe.stop(); return true; }
  noteServerStage(ServerStage::Tcp, 0, address.toString() + ":443");
  return false;
}

// Opens a TLS connection to the server, recording which step failed. The certificate chain is
// checked against ISRG Root X1 first; if that fails, the leaf pin is tried, so a device whose
// clock isn't set yet still connects, and a renewed certificate doesn't cut every device off.
bool openServer(WiFiClientSecure &client, uint32_t timeoutMs) {
  if (WiFi.status() != WL_CONNECTED) { noteServerStage(ServerStage::NoWifi, 0, ""); return false; }
  wifiRssi = static_cast<int8_t>(WiFi.RSSI());
  IPAddress address;
  if (!WiFi.hostByName(kApiHost, address)) { noteServerStage(ServerStage::Dns, 0, "no address for supachat.net"); return false; }
  client.setTimeout(35); client.setHandshakeTimeout(20);
  String chainError;
  // Without the date the chain check fails every time; don't pay for it twice a sync.
  if (!(chainFailedWithoutClock && !clockFromNtp)) {
    client.setCACert(kRootCa);
    if (client.connect(kApiHost, 443, timeoutMs)) { tlsChainWorks = true; chainFailedWithoutClock = false; return true; }
    char tlsError[64]{}; client.lastError(tlsError, sizeof(tlsError)); chainError = tlsError;
    client.stop();
    chainFailedWithoutClock = !clockFromNtp;
    if (!portOpen(address, timeoutMs)) return false;
  } else chainError = "chain not tried";
  WiFiClientSecure &pinned = client;
  pinned.setInsecure();
  if (!pinned.connect(kApiHost, 443, timeoutMs)) {
    char tlsError[64]{}; pinned.lastError(tlsError, sizeof(tlsError)); pinned.stop();
    if (portOpen(address, timeoutMs)) noteServerStage(ServerStage::Tls, 0, tlsError[0] ? String(tlsError) : chainError);
    return false;
  }
  if (!pinned.verify(kTlsFingerprint, kApiHost)) {
    pinned.stop();
    noteServerStage(ServerStage::Cert, 0, (clockFromNtp ? "" : "clock not set; ") + chainError);
    return false;
  }
  tlsChainWorks = false;
  serverStageDetail = "via pin; chain: " + (clockFromNtp ? chainError : String("clock not set"));
  return true;
}

bool requestJson(const String &path, const char *method, const String &requestBody, String &responseBody, int &status) {
  if (chargingModeActive) { status = HTTPC_ERROR_CONNECTION_REFUSED; return false; }
  const os::SecureLease lease(os::Priority::Background);
  if (!lease.held) { status = HTTPC_ERROR_CONNECTION_REFUSED; return false; }
  WiFiClientSecure client;
  if (!openServer(client, kSecureConnectTimeoutMs)) { status = HTTPC_ERROR_CONNECTION_REFUSED; lastHttpStatus = status; return false; }
  const String viaPin = tlsChainWorks ? String() : serverStageDetail.get();
  HTTPClient http; http.setTimeout(35000); http.setReuse(false);
  if (!http.begin(client, String(kApiBase) + path)) return false;
  http.addHeader("Authorization", "Bearer " + deviceToken);
  if (requestBody.length()) http.addHeader("Content-Type", "application/json");
  status = strcmp(method, "GET") == 0 ? http.GET() : http.POST(requestBody);
  if (status < 0) {
    char tlsError[96]{}; const int error = client.lastError(tlsError, sizeof(tlsError));
    Serial.printf("https status=%d tls=%d detail=%s heap=%u\n", status, error, tlsError, ESP.getFreeHeap());
    noteServerStage(ServerStage::Tls, status, String("lost after connect ") + status);
  } else if (status == 401) noteServerStage(ServerStage::Auth, status, "device token refused");
  else if (status >= 200 && status < 300) noteServerStage(ServerStage::Ok, status, viaPin);
  else noteServerStage(ServerStage::Http, status, path.substring(0, 24));
  lastHttpStatus = status;
  if (status > 0) responseBody = http.getString();
  http.end(); return status > 0;
}

bool uploadVoiceClip() {
  const os::SecureLease lease(os::Priority::Foreground, 5000);
  if (!lease.held) return false;
  if (!voiceUploadPending || !voiceSampleCount || WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  if (!openServer(client, kSecureConnectTimeoutMs)) return false;
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
  const os::SecureLease lease(os::Priority::Foreground, 5000);
  if (!lease.held) return false;
  WiFiClientSecure client;
  if (!openServer(client, kSecureConnectTimeoutMs)) return false;
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

// How far a message of this device's has got: on the server, on someone's device, read.
int deliveryRank(const String &state) { return state == "read" ? 3 : state == "delivered" ? 2 : state == "server" || state == "saved" ? 1 : 0; }

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
  } else {
    // A later copy of the message (a re-sync, a restart) never takes back how far it had got.
    if (existing->authorId == kDeviceId && deliveryRank(existing->state) > deliveryRank(serverMessage.state)) serverMessage.state = existing->state;
    *existing = serverMessage;
  }
  lastServerId = std::max(lastServerId, id); trimHistory(); renderDirty = true;
}

// Sends every due outbox message, whatever room is on screen. Only the server's answer that it has
// the message takes it out of the outbox; a refusal marks it FAILED; anything else retries later.
void sendQueuedMessages() {
  struct Due { String clientId, roomId, body; int64_t replyToId; };
  std::vector<Due> due;
  const uint32_t now = millis();
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  for (const auto &entry : outboxEntries)
    if (entry.state == outbox::State::Sending && outbox::due(now, entry.nextAttemptAt))
      due.push_back({entry.clientId.c_str(), entry.roomId.c_str(), entry.body.c_str(), entry.replyToId});
  xSemaphoreGive(stateMutex);
  for (const auto &item : due) {
    const String payload = "{\"client_id\":\"" + jsonEscape(item.clientId) + "\",\"body\":\"" + jsonEscape(item.body) + "\",\"room_id\":\"" + jsonEscape(item.roomId) + "\"" + (item.replyToId > 0 ? ",\"reply_to_id\":" + String(item.replyToId) : "") + "}";
    String response; int status = 0;
    requestJson("/api/messages", "POST", payload, response, status);
    JsonDocument document;
    const bool parsed = (status == 200 || status == 201) && !deserializeJson(document, response) && !document["message"].isNull();
    if ((status == 200 || status == 201) && !parsed) status = -100;  // An answer we can't read is no proof: try again.
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    auto entry = std::find_if(outboxEntries.begin(), outboxEntries.end(), [&](const outbox::Entry &e) { return item.clientId == e.clientId.c_str(); });
    if (entry != outboxEntries.end()) {
      if (parsed) {
        outboxEntries.erase(entry);
        mergeServerMessage(document["message"].as<JsonObjectConst>());  // Only lands if its room is on screen.
        saveHistoryLocked();
      } else {
        outbox::recordAttempt(*entry, status, millis());
        setMessageStateLocked(item.clientId, outboxStateName(*entry));
        Serial.printf("outbox %s status=%d attempts=%u state=%s\n", item.clientId.c_str(), status, entry->attempts, outboxStateName(*entry).c_str());
      }
      saveOutboxLocked();
    }
    xSemaphoreGive(stateMutex);
    if (!parsed && outbox::classify(status) == outbox::Outcome::Retry) break;  // The server is out of reach: the rest wait too.
  }
}

// Enter on a FAILED message of this device's sends it again.
bool retryFailedMessage(const String &clientId) {
  bool found = false;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (outbox::Entry *entry = findOutboxLocked(clientId)) {
    if (entry->state == outbox::State::Failed) { outbox::retry(*entry, millis()); setMessageStateLocked(clientId, "queued"); saveOutboxLocked(); found = true; }
  }
  xSemaphoreGive(stateMutex);
  if (found) syncOverride = true;
  return found;
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

// Each first sync rewrites the room's archive, so reboots can't grow it.
bool archiveStarted = false;
void archiveMessage(JsonObjectConst object, bool startOver) {
  if (!sdReady) return;
  const String path = String("/supachat-archive-") + currentRoomId + ".tsv";
  if (startOver) { SD.remove(path); archiveStarted = true; }
  File file = SD.open(path, FILE_APPEND);
  if (!file) return;
  file.printf("%lld\t%s\t%s\t%s\t%s\t%lld\n", static_cast<long long>(object["id"] | 0LL), object["client_id"] | "", object["author_id"] | "",
              object["author_name"] | "", cleanField(String(object["body"] | "")).c_str(), static_cast<long long>(object["created_at"] | 0LL));
  file.close();
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
        int64_t seen = latest;
        {
          ListsLock lists;
          auto previous = std::find_if(rooms.begin(), rooms.end(), [&](const ChatRoom &room) { return room.id == id; });
          if (previous != rooms.end()) seen = previous->seenMessageId;
        }
        nextRooms.push_back({id, String(object["name"] | "Room"), String(object["group_id"] | ""),
          String(object["group_name"] | ""), String(object["default_language"] | "en"), latest, seen});
      }
      {
        ListsLock lists;
        rooms = std::move(nextRooms); roomsInitialized = true;
        auto active = std::find_if(rooms.begin(), rooms.end(), [&](const ChatRoom &room) { return room.id == currentRoomId; });
        if (active == rooms.end() && !rooms.empty()) { currentRoomId = rooms[0].id; currentRoomName = rooms[0].name; }
        else if (active != rooms.end()) currentRoomName = active->name;
      }
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
    recordSyncError(networkStatus.get().c_str(), jsonError.c_str(), status, response.length(), !initialSyncComplete);
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
    // RAM keeps the newest messages; older history pages go straight to the card.
    if (hydratingHistory && messages.size() >= kRamMessageLimit) archiveMessage(object, !archiveStarted);
    else mergeServerMessage(object);
    if (incoming) newlyRead.push_back(object["id"] | 0);
  }
  for (JsonObjectConst receipt : document["receipts"].as<JsonArrayConst>()) {
    const int64_t messageId = receipt["message_id"] | 0;
    const int64_t updatedAt = receipt["updated_at"] | 0;
    lastReceiptAt = std::max(lastReceiptAt, updatedAt);
    auto item = std::find_if(messages.begin(), messages.end(), [&](const ChatMessage &message) { return message.id == messageId; });
    if (item == messages.end()) continue;
    const String receiptState = String(receipt["state"] | "");
    if (String(receipt["user_id"] | "") == kDeviceId) { if (item->authorId != kDeviceId) item->state = receiptState; }
    // Someone else's receipt on a message of ours: it reached their device, or they read it.
    else if (item->authorId == kDeviceId && deliveryRank(receiptState) > deliveryRank(item->state)) item->state = receiptState;
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
  archiveStarted = false;
  {
    ListsLock lists;
    auto activeRoom = std::find_if(rooms.begin(), rooms.end(), [&](const ChatRoom &room) { return room.id == currentRoomId; });
    if (activeRoom != rooms.end()) activeRoom->seenMessageId = std::max(activeRoom->latestMessageId, lastServerId);
  }
}

// Which way a typed message travels right now. ONLINE: through the server, to everyone in the
// room. NEARBY: by ESP-NOW only, to devices in radio range on the same channel, until the server
// is back. Until v0.57 nothing on screen said which, so a device that had lost the server looked
// the same as one that hadn't.
enum class LinkMode : uint8_t { Starting, Online, Nearby, Offline };
LinkMode linkMode() {
  const bool fresh = serverOkAt && millis() - serverOkAt < kOnlineFreshMs;
  if (!localOnlyMode && fresh && WiFi.status() == WL_CONNECTED) return LinkMode::Online;
  if (!kEspNowEnabled || !meshReady || chargingModeActive) return serverStage == ServerStage::Unknown ? LinkMode::Starting : LinkMode::Offline;
  if (!localOnlyMode && serverStage == ServerStage::Unknown && millis() < 90000) return LinkMode::Starting;
  return LinkMode::Nearby;
}
const char *linkModeWord(LinkMode mode) {
  switch (mode) {
    case LinkMode::Online: return uiText("ONLINE", "EN LIGNE");
    case LinkMode::Nearby: return uiText("NEARBY", "PROXIMITE");
    case LinkMode::Offline: return uiText("OFFLINE", "HORS LIGNE");
    default: return nullptr;
  }
}
String serverStageText() {
  switch (serverStage) {
    case ServerStage::NoWifi: return uiText("NO WI-FI", "PAS DE WI-FI");
    case ServerStage::Dns: return uiText("DNS FAILED", "ECHEC DNS");
    case ServerStage::Tcp: return uiText("PORT 443 BLOCKED", "PORT 443 BLOQUE");
    case ServerStage::Tls: return uiText("TLS FAILED", "ECHEC TLS");
    case ServerStage::Cert: return uiText("CERT REJECTED", "CERTIFICAT REFUSE");
    case ServerStage::Auth: return uiText("LOGIN REFUSED", "CONNEXION REFUSEE");
    case ServerStage::Http: return String(uiText("SERVER HTTP ", "SERVEUR HTTP ")) + serverStageStatus;
    case ServerStage::Ok: return uiText("SERVER OK", "SERVEUR OK");
    default: return uiText("NOT TRIED YET", "PAS ENCORE ESSAYE");
  }
}

// Modem sleep saves power but leaves the radio deaf between the access point's beacons, and
// ESP-NOW packets that arrive then are lost. The radio stays awake whenever ESP-NOW is how
// messages travel (no server) and while the walkie is open.
void applyRadioSleep() {
  if (WiFi.status() != WL_CONNECTED) return;
  const bool awake = linkMode() != LinkMode::Online || screenMode == ScreenMode::Walkie;
  if (awake == radioAwake) return;
  WiFi.setSleep(!awake); radioAwake = awake;
  Serial.printf("radio sleep %s\n", awake ? "off" : "on");
}

void networkTask(void *) {
  os::bindTask(os::Owner::Chat);
  uint32_t lastReconcileAt = millis();
  uint32_t nextWifiAttempt = 0, wifiRetryMs = 0, syncBackoffMs = 0;
  for (;;) {
    try {
#ifdef SUPACHAT_NO_RADIO
    if (!os::bootSettled()) os::setBoot(os::Boot::NoNetwork);
    vTaskDelay(pdMS_TO_TICKS(1000)); continue;
#endif
    if (foxScanRequested) { foxScanRequested = false; collectFoxPlacePrint(); continue; }
    if (chatShrunk && !mediaOwnsRadio()) {
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      messages.clear(); loadHistory(); chatShrunk = false; renderDirty = true;
      xSemaphoreGive(stateMutex);
    }
    if (chargingModeActive) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if (radioOffWanted) {
      if (!radioIsOff) {
        if (espNowActive) { esp_now_deinit(); espNowActive = false; }
        WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF);
        currentSsid = ""; networkStatus = "RADIO OFF"; renderDirty = true;
        radioIsOff = true;
      }
      vTaskDelay(pdMS_TO_TICKS(200)); continue;
    }
    if (radioIsOff) {
      WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false); WiFi.persistent(false);
      if (kEspNowEnabled) initializeEspNow();
      radioIsOff = false; wifiResumeRequested = true;
    }
    if (manualWifiMode) { networkTaskParked = true; vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    networkTaskParked = false;
    if (localOnlyMode) {
      if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false, false);
      if (currentSsid.length() || networkStatus != "ESPNOW LOCAL") {
        currentSsid = ""; networkStatus = "ESPNOW LOCAL"; renderDirty = true;
      }
      vTaskDelay(pdMS_TO_TICKS(250)); continue;
    }
    if (wifiResumeRequested) { wifiResumeRequested = false; nextWifiAttempt = 0; wifiRetryMs = 0; syncBackoffMs = 0; }
    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED && now < nextWifiAttempt) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
    if (!connectKnownWifi()) {
      noteServerStage(ServerStage::NoWifi, 0, "");
      if (!os::bootSettled()) os::setBoot(os::Boot::NoNetwork);
      // Scanning costs battery and blocks ESP-NOW's channel, so away from known networks it slows down.
      wifiRetryMs = std::min<uint32_t>(wifiRetryMs ? wifiRetryMs * 2 : kWifiRetryMs, kWifiRetryMaxMs);
      nextWifiAttempt = millis() + wifiRetryMs; vTaskDelay(pdMS_TO_TICKS(500)); continue;
    }
    wifiRetryMs = 0;
    applyRadioSleep();
    if (os::boot() == os::Boot::Joining) os::setBoot(os::Boot::Syncing);
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
    // Chat waits while the player is on, and while the walkie socket holds the secure slot.
    // It catches up the moment there is room: one more secure connection doesn't fit beside the player.
    if (mediaOwnsRadio() || os::secureHeld() || !os::secureFits()) {
      if (!os::bootSettled() && !os::secureFits()) os::setBoot(os::Boot::ServerDown);
      if (!mediaOwnsRadio() && !os::secureHeld()) networkStatus = "CHAT WAITS";
      renderDirty = true; vTaskDelay(pdMS_TO_TICKS(500)); continue;
    }
    const bool hadSynced = initialSyncComplete;
    synchronize();
    if (!os::bootSettled()) {
      if (initialSyncComplete && !hadSynced) os::setBoot(os::Boot::Ready);
      else if (networkStatus.startsWith("SYNC ") || networkStatus.startsWith("ROOM ")) os::setBoot(os::Boot::ServerDown);
    }
    os::setState(os::Owner::Chat, screenMode == ScreenMode::Chat ? os::AppState::Foreground : os::AppState::Background);
    if (voiceUploadPending && !localReplayActive) uploadVoiceClip();
    if (syncOverride) syncOverride = false;
    // Blocks freed past the ledger's hooks (libc's own frees) would otherwise count against chat for good.
    if (millis() - lastReconcileAt > kChatReconcileMs) { lastReconcileAt = millis(); os::reconcile(os::Owner::Chat); }
    vTaskDelay(pdMS_TO_TICKS(kSyncPollMs));
    // A network that won't reach the server (a captive portal, no internet) isn't hammered.
    if (networkStatus.startsWith("SYNC ") && !networkStatus.startsWith("SYNCED") || networkStatus.startsWith("ROOM ")) {
      syncBackoffMs = std::min<uint32_t>(syncBackoffMs ? syncBackoffMs * 2 : 6000, kSyncBackoffMaxMs);
      for (uint32_t waited = 0; waited < syncBackoffMs && !syncOverride && !manualWifiMode && !wifiResumeRequested; waited += 250) vTaskDelay(pdMS_TO_TICKS(250));
    } else syncBackoffMs = 0;
    } catch (const std::bad_alloc &) {
      // Chat ran out. Everything since the last good state is dropped, and it starts over.
      if (stateMutex) xSemaphoreGive(stateMutex);
      os::recordFault(os::Owner::Chat, "out of memory, restarted");
      os::setState(os::Owner::Chat, os::AppState::Starting);
      if (!os::bootSettled()) os::setBoot(os::Boot::ServerDown);
      renderDirty = true;
      vTaskDelay(pdMS_TO_TICKS(kSyncPollMs));
    }
  }
}

void walkieTask(void *) {
  os::bindTask(os::Owner::Chat);
  bool holdsLease = false;
  for (;;) {
    try {
#ifdef SUPACHAT_NO_RADIO
    vTaskDelay(pdMS_TO_TICKS(1000)); continue;
#endif
    if (chargingModeActive) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    // The socket and its secure slot exist only while the walkie screen is on.
    if (mediaOwnsRadio() || screenMode != ScreenMode::Walkie) {
      if (walkieInitialized) { walkieSocket.disconnect(); walkieInitialized = false; walkieConnected = false; }
      if (holdsLease) { os::releaseSecure(); holdsLease = false; }
      vTaskDelay(pdMS_TO_TICKS(100)); continue;
    }
    if (WiFi.status() == WL_CONNECTED && timeKnown && initialSyncComplete) {
      if (!holdsLease && !(holdsLease = os::acquireSecure(os::Priority::Foreground, 500))) { walkieStatus = "WAITING FOR RAM"; renderDirty = true; continue; }
      if (!walkieInitialized) {
        walkieHeaders = "Authorization: Bearer " + deviceToken + "\r\n";
        walkieSocket.setExtraHeaders(walkieHeaders.c_str()); walkieSocket.onEvent(onWalkieEvent); walkieSocket.setReconnectInterval(2000);
        const String walkiePath = "/walkie?room=" + currentRoomId;
        // The same trust as chat's last good connection: the chain when that worked, else the pin.
        if (tlsChainWorks) walkieSocket.beginSslWithCA(kApiHost, 443, walkiePath.c_str(), kRootCa);
        else walkieSocket.beginSSL(kApiHost, 443, walkiePath.c_str(), kTlsFingerprint);
        walkieInitialized = true;
      }
      walkieSocket.loop();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    } catch (const std::bad_alloc &) {
      if (stateMutex) xSemaphoreGive(stateMutex);
      os::recordFault(os::Owner::Chat, "walkie out of memory");
      walkieSocket.disconnect(); walkieInitialized = false; walkieConnected = false;
      if (holdsLease) { os::releaseSecure(); holdsLease = false; }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

void drawHeader(const char *title) {
  auto &display = uiCanvas;
  display.fillRect(0, 0, 240, 20, TFT_DARKGREEN);
  display.setTextColor(TFT_WHITE, TFT_DARKGREEN); display.setTextSize(1); display.setCursor(5, 6); display.print(title);
  if (strcmp(title, "FAMLY") == 0) { display.setCursor(40, 6); display.print(easternClockText()); }
  if (!keyboardReady) { display.setTextColor(TFT_RED, TFT_DARKGREEN); display.setCursor(88, 6); display.print("K!"); }
  if (screenMode != ScreenMode::Charging && screenMode != ScreenMode::ChargingConfirm) {
    const LinkMode mode = linkMode();
    const char *word = linkModeWord(mode);
    display.setTextColor(mode == LinkMode::Online ? TFT_WHITE : mode == LinkMode::Nearby ? TFT_YELLOW : mode == LinkMode::Offline ? TFT_ORANGE : TFT_LIGHTGREY, TFT_DARKGREEN);
    // The Wi-Fi screens report their own progress (SCANNING, JOINING, SAVED, FAILED) here.
    const bool wifiScreen = screenMode == ScreenMode::Networks || screenMode == ScreenMode::NetworkPassword;
    if (wifiScreen) display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    display.setCursor(106, 6); display.print(word && !wifiScreen ? String(word) : networkStatus.substring(0, 10));
    display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
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

// Screen time's warnings take the whole header for a while, over every app. Drawn last, after a
// screen's own additions to its header, such as chat's room names.
void drawWarningBanner() {
  if (bannerWarning == clockapps::Warning::None) return;
  auto &display = uiCanvas;
  const bool one = bannerWarning == clockapps::Warning::OneMinute;
  const uint16_t background = one ? TFT_RED : TFT_YELLOW;
  const char *text = one ? uiText("SCREEN: 1 MIN LEFT", "ECRAN: ENCORE 1 MIN") : uiText("SCREEN: 5 MIN LEFT", "ECRAN: ENCORE 5 MIN");
  display.fillRect(0, 0, 240, 20, background); display.setTextSize(2);
  display.setTextColor(one ? TFT_WHITE : TFT_BLACK, background);
  display.setCursor((240 - static_cast<int>(strlen(text)) * 12) / 2, 2); display.print(text); display.setTextSize(1);
}

size_t utf8CharacterCount(const String &text);
String utf8Tail(const String &text, size_t maximumCharacters);
void printFont0Text(const String &text);

// Ticks drawn with lines, not a font: Font0 has no check mark that reads at this size.
void drawMessageMark(int8_t mark, int y) {
  if (mark < 0) return;
  auto &display = uiCanvas;
  auto tick = [&](int x, uint16_t colour) {
    display.drawLine(x, y + 5, x + 2, y + 8, colour); display.drawLine(x, y + 6, x + 2, y + 9, colour);
    display.drawLine(x + 2, y + 8, x + 6, y + 1, colour); display.drawLine(x + 2, y + 9, x + 6, y + 2, colour);
  };
  switch (mark) {
    case MarkSending:  // A small clock: waiting. Three dots read as text cut short.
      display.drawCircle(232, y + 5, 4, TFT_LIGHTGREY); display.drawFastVLine(232, y + 2, 4, TFT_LIGHTGREY); display.drawFastHLine(232, y + 5, 3, TFT_LIGHTGREY); break;
    case MarkHeardNearby: tick(230, TFT_YELLOW); break;
    case MarkOnServer: tick(230, TFT_LIGHTGREY); break;
    case MarkDelivered: tick(227, TFT_LIGHTGREY); tick(232, TFT_LIGHTGREY); break;
    case MarkRead: tick(227, TFT_CYAN); tick(232, TFT_CYAN); break;
    case MarkFailed: display.fillRect(232, y, 3, 7, TFT_RED); display.fillRect(232, y + 9, 3, 3, TFT_RED); break;
  }
}

void drawChat() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("");
  bool leftNew = false, rightNew = false;
  {
    ListsLock lists;
    const String activeId = currentRoomId.get();
    int activeIndex = -1; for (int index = 0; index < static_cast<int>(rooms.size()); index++) if (rooms[index].id == activeId) activeIndex = index;
    const int leftIndex = rooms.size() > 1 && activeIndex >= 0 ? (activeIndex - 1 + rooms.size()) % rooms.size() : -1;
    const int rightIndex = rooms.size() > 1 && activeIndex >= 0 ? (activeIndex + 1) % rooms.size() : -1;
    leftNew = leftIndex >= 0 && rooms[leftIndex].latestMessageId > rooms[leftIndex].seenMessageId;
    rightNew = rightIndex >= 0 && rooms[rightIndex].latestMessageId > rooms[rightIndex].seenMessageId;
  }
  display.setTextSize(1); display.setTextColor(leftNew ? TFT_YELLOW : TFT_DARKGREY, TFT_DARKGREEN); display.setCursor(4, 6); display.print(leftNew ? "<*" : "< ");
  const String roomTitle = currentRoomName.substring(0, 10); display.setTextColor(TFT_WHITE, TFT_DARKGREEN); display.setCursor(50 - roomTitle.length() * 3, 6); display.print(roomTitle);
  display.setTextColor(rightNew ? TFT_YELLOW : TFT_DARKGREY, TFT_DARKGREEN); display.setCursor(91, 6); display.print(rightNew ? "*>" : " >");
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  // Up once selects the newest message (since v0.57; before, the newest couldn't be selected), and
  // each Up after that one older, scrolling the list with it.
  const int end = std::max(0, static_cast<int>(messages.size()) - std::max(0, static_cast<int>(historyOffset) - 1));
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
      lines.push_back({line, sender, colour, mine, firstChunk, historyOffset > 0 && index == end - 1, -1});
      firstChunk = false;
    }
    if (mine && !lines.empty()) {
      const String &state = message.state;
      lines.back().mark = state == "failed" ? MarkFailed : state == "heard" ? MarkHeardNearby : message.queued ? MarkSending
          : state == "read" ? MarkRead : state == "delivered" ? MarkDelivered : MarkOnServer;
    }
  }
  // The hint over the composer: what a selected unsent message needs, a notice, or how many are unsent.
  String hint; uint16_t hintColour = TFT_RED;
  const int selectedIndex = static_cast<int>(messages.size()) - static_cast<int>(historyOffset);
  size_t failedCount = 0;
  for (const auto &entry : outboxEntries) failedCount += entry.state == outbox::State::Failed;
  if (historyOffset > 0 && selectedIndex >= 0 && selectedIndex < static_cast<int>(messages.size()) && messages[selectedIndex].state == "failed") {
    const outbox::Entry *entry = findOutboxLocked(messages[selectedIndex].clientId);
    hint = String(uiText("NOT SENT (", "NON ENVOYE (")) + (entry ? entry->lastStatus : 0) + uiText("). ENTER TO RETRY", "). ENTER: REESSAYER");
  } else if (chatNoticeAt) { hint = chatNotice.get(); hintColour = TFT_YELLOW; }
  else if (failedCount) hint = String(failedCount) + uiText(" NOT SENT. FN+UP, ENTER: RETRY", " NON ENVOYES. FN+HAUT PUIS ENTER");
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
    // Our lines leave 12 px at the right for the delivery mark.
    const int x = lines[index].mine ? std::max(3, 225 - display.textWidth(prefix + lines[index].text)) : 3;
    const uint32_t lineBackground = lines[index].selected ? 0x18C3 : TFT_BLACK;
    if (lines[index].selected) display.fillRoundRect(std::max(1, x - 2), y - 2, std::min(238 - x, display.textWidth(prefix + lines[index].text) + 5), 15, 2, lineBackground);
    display.setCursor(x, y);
    if (lines[index].showSender) {
      display.setTextColor(lines[index].colour, lineBackground); printFont0Text(lines[index].sender);
      display.setTextColor(TFT_WHITE, lineBackground); display.print(": ");
    } else display.setTextColor(TFT_WHITE, lineBackground);
    printFont0Text(lines[index].text);
    drawMessageMark(lines[index].mark, y);
    y += 17;
  }
  xSemaphoreGive(stateMutex);
  display.setFont(&fonts::Font0);
  display.drawFastHLine(0, 106, 240, TFT_DARKGREY);
  display.setTextSize(1);
  const bool twoLines = replyToMessageId > 0 || hint.length();
  if (replyToMessageId > 0) { display.setTextColor(TFT_CYAN, TFT_BLACK); display.setCursor(3, 109); display.print(uiText("REPLY ", "REPONDRE ")); printFont0Text(replyToAuthor.substring(0, 15)); }
  else if (hint.length()) { display.setTextColor(hintColour, TFT_BLACK); display.setCursor(3, 109); display.print(hint.substring(0, 39)); }
  display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(3, twoLines ? 120 : 112); display.print("> ");
  display.setTextColor(TFT_WHITE, TFT_BLACK); printFont0Text(utf8Tail(draft, twoLines ? 27 : 34));
  // A dead key waiting for its letter shows as its accent, in yellow; an empty French composer says where the keys are.
  if (frenchTypist.pending != frkb::Dead::None) { display.setTextColor(TFT_YELLOW, TFT_BLACK); printFont0Text(frkb::deadText(frenchTypist.pending)); }
  else if (frenchUi && draft.isEmpty()) { display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.print("TAB: TOUCHES"); }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(198, 126); display.printf("%d/140", utf8CharacterCount(draft));
}

const char *kMenuItemsEn[] = {"BACK TO CHAT", "ROOMS", "SYNC NOW", "VOICE MESSAGES", "WALKIE-TALKIE", "EMOJI RECIPES", "FOX FINDING"};
const char *kMenuItemsFr[] = {"RETOUR CHAT", "SALONS", "SYNCHRO", "MESSAGES VOCAUX", "WALKIE-TALKIE", "RECETTES EMOJI", "CHASSE AU RENARD"};
constexpr int kMenuItemCount = sizeof(kMenuItemsEn) / sizeof(kMenuItemsEn[0]);
void drawMenu() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("MENU");
  display.setTextSize(1);
  for (int row = 0; row < kMenuItemCount; row++) {
    const int y = 21 + row * 14; const bool selected = row == menuSelection;
    display.fillRoundRect(6, y, 228, 12, 3, selected ? TFT_GREEN : TFT_DARKGREY);
    display.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_GREEN : TFT_DARKGREY);
    display.setCursor(13, y + 3); display.print(frenchUi ? kMenuItemsFr[row] : kMenuItemsEn[row]);
  }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print(uiText("UP/DOWN MOVE      ENTER OPEN", "HAUT/BAS BOUGER   ENTER OUVRIR"));
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
  ListsLock lists;
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

String utf8Head(const String &text, size_t maximumCharacters);
void drawStatus() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("STATUS", "ETAT")); display.setTextSize(1);
  display.setTextColor(TFT_YELLOW, TFT_DARKGREEN); display.setCursor(78, 6); display.printf("%d/3", statusPage + 1);
  display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 29);
  if (statusPage == 0) {
    // The link: which way messages go, and the exact step that failed, large enough to read out.
    const LinkMode mode = linkMode();
    display.setTextSize(2); display.setCursor(6, 25);
    display.setTextColor(mode == LinkMode::Online ? TFT_GREEN : mode == LinkMode::Nearby ? TFT_YELLOW : TFT_ORANGE, TFT_BLACK);
    display.print(mode == LinkMode::Online ? uiText("ONLINE", "EN LIGNE") : mode == LinkMode::Nearby ? uiText("NEARBY ONLY", "PROXIMITE SEULE")
                  : mode == LinkMode::Offline ? uiText("OFFLINE", "HORS LIGNE") : uiText("STARTING", "DEMARRAGE"));
    display.setCursor(6, 45); display.setTextColor(serverStage == ServerStage::Ok ? TFT_GREEN : serverStage == ServerStage::Unknown ? TFT_LIGHTGREY : TFT_RED, TFT_BLACK);
    display.print(serverStageText());
    display.setTextSize(1); display.setTextColor(TFT_WHITE, TFT_BLACK);
    int y = 66;
    auto line = [&](const String &text) { display.setCursor(6, y); display.print(text.substring(0, 38)); y += 10; };
    if (mode == LinkMode::Nearby) line(uiText("TEXTS REACH NEARBY DEVICES ONLY", "TEXTES: APPAREILS PROCHES SEULEMENT"));
    const String status = networkStatus.get();
    if (WiFi.status() == WL_CONNECTED) line("WI-FI  " + utf8Head(currentSsid.get(), 18) + " " + String(wifiRssi) + " dBm");
    else line(String("WI-FI  ") + (status == "ESPNOW ONLY" ? uiText("no saved network here", "aucun reseau connu ici")
                                  : status == "JOIN FAILED" ? uiText("join failed (password?)", "echec (mot de passe?)") : status.c_str()));
    const String detail = serverStageDetail.get();
    if (serverStage == ServerStage::Auth || serverStage == ServerStage::Http) line(String("HTTP ") + serverStageStatus + "  " + detail);
    else if (detail.length()) line(detail);
    const uint32_t age = serverStageAt ? (millis() - serverStageAt) / 1000 : 0;
    line(String(uiText("TRIED ", "ESSAI ")) + (serverStageAt ? String(age) + "s" : String("-")) + uiText("  CLOCK ", "  HEURE ") +
         (clockFromNtp ? uiText("set", "ok") : uiText("not set", "inconnue")));
    size_t waiting = 0, failed = 0;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    for (const auto &entry : outboxEntries) (entry.state == outbox::State::Failed ? failed : waiting)++;
    xSemaphoreGive(stateMutex);
    if (y <= 106) line(String(uiText("OUTBOX ", "ENVOI ")) + waiting + uiText(" waiting, ", " en attente, ") + failed + uiText(" failed", " refuses"));
  } else if (statusPage == 1) {
    display.printf(frenchUi ? "APPAREIL  %s\n" : "NODE      %s\n", kDeviceName);
    display.printf("WIFI      %s\n", currentSsid.isEmpty() ? "offline" : currentSsid.get().c_str());
    display.printf(frenchUi ? "ETAT      %s\n" : "STATE     %s\n", networkStatus.get().c_str()); display.printf(frenchUi ? "HEURE     %s\n" : "TIME      %s\n", timeKnown ? uiText("known", "connue") : uiText("unknown", "inconnue"));
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
  display.setCursor(6, 121); display.print(uiText("TYPE  ENTER JOIN  MENU CANCEL", "TAPER ENTER JOINDRE MENU ANNULER"));
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
    display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print(packInvite ? uiText("LEFT BACK          ENTER JOIN", "GAUCHE RETOUR   ENTER REJOINDRE") : uiText("LEFT BACK        ENTER REQUEST", "GAUCHE RETOUR   ENTER DEMANDE")); return;
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

// HOME and its apps. SUPACHAT is the existing chat; the others live below.
enum class AppId : uint8_t { Chat, Music, Books, Remote, Timer, Alarm, ScreenTime, Stopwatch, Wifi, Volume, Status, Language, Charging, Local, Changelog, System };
struct HomeApp { AppId id; const char *english; const char *french; };
// Pages of four, in order. A label fits a tile at 9 characters: see design-reference-cardputer.
constexpr HomeApp kHomeApps[] = {{AppId::Chat, "SUPACHAT", "SUPACHAT"}, {AppId::Music, "MUSIC", "MUSIQUE"}, {AppId::Books, "BOOKS", "LIVRES"}, {AppId::Remote, "R/C", "R/C"},
                                 {AppId::Timer, "TIMER", "MINUTEUR"}, {AppId::Alarm, "ALARM", "REVEIL"}, {AppId::ScreenTime, "SCREEN", "ECRAN"}, {AppId::Stopwatch, "STOPWATCH", "CHRONO"},
                                 {AppId::Wifi, "WI-FI", "WI-FI"}, {AppId::Volume, "VOLUME", "VOLUME"}, {AppId::Status, "STATUS", "ETAT"}, {AppId::Language, "LANGUAGE", "LANGUE"},
                                 {AppId::Charging, "CHARGING", "CHARGE"}, {AppId::Local, "LOCAL OFF", "LOCAL NON"}, {AppId::Changelog, "CHANGELOG", "VERSIONS"}, {AppId::System, "SYSTEM", "SYSTEME"}};
constexpr int kHomeAppCount = sizeof(kHomeApps) / sizeof(kHomeApps[0]);
constexpr int kHomeAppsPerPage = 4;
constexpr int kHomePageCount = kHomeAppCount / kHomeAppsPerPage;
static_assert(kHomeAppCount % kHomeAppsPerPage == 0, "HOME's pages are full pages");
int homeSelection = 0;
// The bump at the end of the pages: the grid knocks against the edge it can't pass, then settles.
constexpr uint32_t kHomeBumpMs = 240;
uint32_t homeBumpAt = 0;
int homeBumpDirection = 0;
String homeNotice;

enum class LibraryView : uint8_t { MusicRoot, CardAlbums, Albums, Tracks, Liked, SearchResults };
library::Collection libraryCollection = library::Collection::Music;
LibraryView libraryView = LibraryView::MusicRoot;
std::vector<library::Item> libraryItems;
std::vector<library::Item> playQueue;
library::Collection queueCollection = library::Collection::Music;
String libraryTitle;
String libraryAlbumTitle;
String librarySearchText;
int librarySelection = 0;
int libraryOffset = 0;
int queueIndex = -1;
uint32_t lastBookmarkAt = 0;
uint32_t lastMediaRenderAt = 0;
bool tracksFromCardList = false;
String libraryFolder;
String queueFolder;
volatile bool mediaStarting = false;
os::Owner playingOwner = os::Owner::Music;
bool adjacentWanted = false;
bool mediaTasksWanted = false;
bool queueEnded = false;
uint32_t homeNoticeAt = 0;

String utf8Head(const String &text, size_t maximumCharacters) {
  size_t characters = 0;
  for (size_t index = 0; index < text.length(); index++) {
    if ((static_cast<uint8_t>(text[index]) & 0xC0) == 0x80) continue;
    if (characters == maximumCharacters) return text.substring(0, index);
    characters++;
  }
  return text;
}

String clockText(uint32_t seconds) {
  char text[12];
  if (seconds >= 3600) snprintf(text, sizeof(text), "%u:%02u:%02u", static_cast<unsigned>(seconds / 3600), static_cast<unsigned>(seconds / 60 % 60), static_cast<unsigned>(seconds % 60));
  else snprintf(text, sizeof(text), "%u:%02u", static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
  return text;
}

const library::Item *nowPlayingItem() {
  return queueIndex >= 0 && queueIndex < static_cast<int>(playQueue.size()) ? &playQueue[queueIndex] : nullptr;
}

String clockLine();
void drawHome() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("HOME");
  const int page = homeSelection / kHomeAppsPerPage;
  int bump = 0;
  if (homeBumpAt) {
    const uint32_t elapsed = millis() - homeBumpAt;
    bump = homeBumpDirection * (elapsed < 60 ? 6 : elapsed < 120 ? -3 : elapsed < 180 ? 2 : 0);
    if (homeBumpDirection) display.fillRect(homeBumpDirection < 0 ? 0 : 236, 24, 4, 77, TFT_RED);
  }
  for (int slot = 0; slot < kHomeAppsPerPage; slot++) {
    const int index = page * kHomeAppsPerPage + slot;
    const int x = 6 + (slot % 2) * 117 + bump; const int y = 24 + (slot / 2) * 41; const bool selected = index == homeSelection;
    display.fillRoundRect(x, y, 111, 36, 4, selected ? TFT_GREEN : TFT_DARKGREY);
    display.setTextSize(2); display.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_GREEN : TFT_DARKGREY);
    const char *label = frenchUi ? kHomeApps[index].french : kHomeApps[index].english;
    if (kHomeApps[index].id == AppId::Local && localOnlyMode) label = uiText("LOCAL ON", "LOCAL OUI");
    display.setCursor(x + (111 - static_cast<int>(strlen(label)) * 12) / 2, y + 10); display.print(label);
  }
  for (int dot = 0; dot < kHomePageCount; dot++) display.fillCircle(120 + (dot * 2 - kHomePageCount + 1) * 7, 105, 3, dot == page ? TFT_GREEN : TFT_DARKGREY);
  display.setTextSize(1);
  const auto *playing = nowPlayingItem();
  if (homeNotice.length()) { display.setTextColor(TFT_RED, TFT_BLACK); display.setCursor(6, 112); display.print(homeNotice.substring(0, 38)); }
  else if (clockLine().length()) { display.setTextColor(TFT_CYAN, TFT_BLACK); display.setCursor(6, 112); display.print(clockLine()); }
  else if (playing && media::active()) { display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(6, 112); printFont0Text(utf8Head(playing->title, 37)); }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print(uiText("ARROWS MOVE       ENTER OPEN", "FLECHES           ENTER OUVRIR"));
}

void drawLibrary() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(utf8Head(libraryTitle, 16).c_str());
  const auto status = library::status();
  if (status == library::Status::Loading) {
    display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(12, 50); display.print(uiText("LOADING...", "CHARGEMENT..."));
  } else if (status == library::Status::Failed) {
    display.setTextColor(TFT_RED, TFT_BLACK); display.setCursor(12, 50); display.print(failureText(library::failure()));
  } else if (libraryItems.empty()) {
    display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(12, 50); display.print(uiText("NOTHING HERE", "RIEN ICI"));
  }
  constexpr int kRows = 7;
  const int first = constrain(librarySelection - kRows / 2, 0, std::max(0, static_cast<int>(libraryItems.size()) - kRows));
  if (status != library::Status::Loading && status != library::Status::Failed) {
    for (int row = 0; row < kRows && first + row < static_cast<int>(libraryItems.size()); row++) {
      const auto &item = libraryItems[first + row]; const int y = 22 + row * 14; const bool selected = first + row == librarySelection;
      display.fillRoundRect(3, y, 234, 13, 3, selected ? TFT_GREEN : TFT_BLACK);
      display.setTextColor(selected ? TFT_BLACK : TFT_WHITE, selected ? TFT_GREEN : TFT_BLACK); display.setCursor(7, y + 3);
      printFont0Text(utf8Head(item.title, item.resumeMs || item.liked ? 34 : 37));
      if (item.resumeMs) { display.setCursor(222, y + 3); display.print(">"); }
      else if (item.liked) { display.setCursor(222, y + 3); display.print("*"); }
    }
  }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123);
  display.print(libraryView == LibraryView::Albums ? uiText("LEFT/RIGHT PAGE  ENTER OPEN", "GAUCHE/DROITE PAGE ENTER") : uiText("UP/DOWN MOVE      ENTER OPEN", "HAUT/BAS BOUGER   ENTER OUVRIR"));
}

void drawLibrarySearch() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("SEARCH", "CHERCHER"));
  display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 40); display.print(uiText("SONG OR ARTIST", "CHANSON OU ARTISTE"));
  display.fillRoundRect(5, 55, 230, 24, 4, TFT_DARKGREY); display.setTextColor(TFT_WHITE, TFT_DARKGREY); display.setCursor(10, 63); printFont0Text(utf8Tail(librarySearchText, 36));
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print(uiText("TYPE  ENTER SEARCH  MENU BACK", "TAPER ENTER CHERCHER MENU RETOUR"));
}

void drawNowPlaying() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(queueCollection == library::Collection::Books ? uiText("BOOKS", "LIVRES") : uiText("MUSIC", "MUSIQUE"));
  const auto *item = nowPlayingItem();
  if (!item) return;
  const auto state = media::state();
  display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 28); printFont0Text(utf8Head(item->title, 38));
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 42); printFont0Text(utf8Head(item->detail, 38));
  const uint32_t position = media::positionSeconds();
  const uint32_t duration = item->seconds ? item->seconds : media::durationSeconds();
  display.drawRoundRect(6, 60, 228, 8, 3, TFT_DARKGREY);
  if (duration) display.fillRoundRect(7, 61, std::min<uint64_t>(226, 226ULL * position / duration), 6, 2, TFT_GREEN);
  display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, 74); display.print(clockText(position));
  const String total = clockText(duration); display.setCursor(234 - static_cast<int>(total.length()) * 6, 74); display.print(total);
  display.setCursor(6, 92);
  if (state == media::State::Failed) { display.setTextColor(TFT_RED, TFT_BLACK); display.print(failureText(media::failure())); }
  else {
    display.setTextColor(TFT_YELLOW, TFT_BLACK);
    display.print(state == media::State::Connecting ? uiText("BUFFERING", "CHARGEMENT") : state == media::State::Paused ? uiText("PAUSED", "EN PAUSE") : state == media::State::Playing ? uiText("PLAYING", "LECTURE") : uiText("STOPPED", "ARRET"));
  }
  display.setCursor(178, 92); display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.print(kVolumeNames[volumeLevel]);
  if (item->liked) { display.setCursor(160, 92); display.setTextColor(TFT_YELLOW, TFT_BLACK); display.print("*"); }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.setCursor(6, 110); display.print(queueCollection == library::Collection::Books ? uiText("LEFT/RIGHT 30s    UP/DOWN VOL", "GAUCHE/DROITE 30s HAUT/BAS VOL") : uiText("LEFT/RIGHT SONG   UP/DOWN VOL", "GAUCHE/DROITE     HAUT/BAS VOL"));
  display.setCursor(6, 123); display.print(queueCollection == library::Collection::Books ? uiText("ENTER PAUSE       S STOP", "ENTER PAUSE       S ARRET") : uiText("ENTER PAUSE  L LIKE  S STOP", "ENTER PAUSE  L AIME  S ARRET"));
}

void drawSystem() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("SYSTEM");
  display.setTextSize(1);
  int y = 24;
  for (const os::Owner owner : {os::Owner::Ui, os::Owner::Chat, os::Owner::Music, os::Owner::Books}) {
    display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(6, y); display.print(os::name(owner));
    display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(66, y); display.print(os::used(owner) / 1024); display.print("K");
    if (os::budget(owner)) { display.print(" / "); display.print(os::budget(owner) / 1024); display.print("K"); }
    display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(180, y); display.print(os::stateName(os::state(owner)));
    y += 12;
  }
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(6, y + 2); display.print("FREE "); display.print(ESP.getFreeHeap() / 1024); display.print("K  LARGEST "); display.print(ESP.getMaxAllocHeap() / 1024); display.print("K");
  display.setCursor(6, y + 14); display.print("LOWEST "); display.print(ESP.getMinFreeHeap() / 1024); display.print("K  SECURE "); display.print(os::secureHeld() ? "IN USE" : "IDLE");
  display.setCursor(6, y + 26);
  if (os::faultCount()) { display.setTextColor(TFT_RED, TFT_BLACK); display.print(String(os::lastFault()).substring(0, 38)); }
  else { display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.print(uiText("NO FAULTS", "AUCUNE ERREUR")); }
  if (os::uncountedBlocks() || os::refusedAllocations()) {
    display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(6, y + 38);
    display.print("UNCOUNTED "); display.print(os::uncountedBlocks()); display.print(" REFUSED "); display.print(os::refusedAllocations());
  }
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print(uiText("LEFT BACK", "GAUCHE RETOUR"));
}

void drawRemote() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader("R/C");
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(12, 56); display.print(uiText("NOTHING CONNECTED YET", "RIEN DE CONNECTE"));
  display.setCursor(6, 123); display.print(uiText("LEFT BACK", "GAUCHE RETOUR"));
}

// The clock apps. Their screens are navigation screens: the arrows need no Fn, and digits type times.
enum class Ringing : uint8_t { None, Alarm, ScreenTime, Timer };
Ringing ringingNow() {
  if (alarmRinging) return Ringing::Alarm;
  if (screenTime.countdown.state == clockapps::Countdown::State::Ringing) return Ringing::ScreenTime;
  if (timerCountdown.state == clockapps::Countdown::State::Ringing) return Ringing::Timer;
  return Ringing::None;
}

bool isClockScreen() {
  return screenMode == ScreenMode::Timer || screenMode == ScreenMode::Alarm || screenMode == ScreenMode::ScreenTime || screenMode == ScreenMode::Stopwatch;
}

// The wall clock, only once a time server has set it.
bool wallClock(struct tm &value) { return clockFromNtp && getLocalTime(&value, 10); }

String alarmTimeText(uint8_t hour, uint8_t minute) {
  char text[12];
  if (frenchUi) snprintf(text, sizeof(text), "%02u:%02u", hour, minute);
  else snprintf(text, sizeof(text), "%u:%02u %s", hour % 12 == 0 ? 12u : hour % 12u, minute, hour < 12 ? "AM" : "PM");
  return text;
}

// The time of the alarm that rang last, for the ring screen.
String ringingAlarmText() {
  if (alarms.ringing < 0 || alarms.ringing >= alarms.count) return "";
  const clockapps::AlarmEntry &entry = alarms.entries[alarms.ringing];
  return alarmTimeText(entry.hour, entry.minute);
}

String clockLine() {
  const uint32_t now = millis();
  if (screenTime.countdown.state == clockapps::Countdown::State::Running || screenTime.countdown.state == clockapps::Countdown::State::Paused)
    return String(uiText("SCREEN TIME ", "TEMPS D'ECRAN ")) + clockText(screenTime.countdown.leftSeconds(now)) + uiText(" LEFT", "");
  if (timerCountdown.state == clockapps::Countdown::State::Running) return String(uiText("TIMER ", "MINUTEUR ")) + clockText(timerCountdown.leftSeconds(now));
  if (alarms.snoozed) return uiText("ALARM SNOOZED", "REVEIL REPORTE");
  return "";
}

void centerText(const String &text, int y, uint8_t size, uint16_t colour, uint16_t background) {
  auto &display = uiCanvas;
  display.setTextSize(size); display.setTextColor(colour, background);
  display.setCursor((240 - static_cast<int>(text.length()) * 6 * size) / 2, y); display.print(text); display.setTextSize(1);
}

void drawFooter(const char *first, const char *second) {
  auto &display = uiCanvas; display.setTextSize(1); display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (first && *first) { display.setCursor(6, 112); display.print(first); }
  display.setCursor(6, 123); display.print(second);
}

void drawTimer() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("TIMER", "MINUTEUR"));
  using State = clockapps::Countdown::State;
  const State state = timerCountdown.state;
  centerText(clockText(timerCountdown.leftSeconds(millis())), 30, 4, state == State::Running ? TFT_GREEN : state == State::Paused ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
  if (state == State::Running) {
    centerText(uiText("RUNNING", "EN COURS"), 72, 2, TFT_GREEN, TFT_BLACK);
    drawFooter(uiText("KEEPS GOING IN OTHER APPS", "CONTINUE DANS LES AUTRES APPS"), uiText("ENTER PAUSE        DEL RESET", "ENTER PAUSE        DEL REMETTRE"));
  } else if (state == State::Paused) {
    centerText(uiText("PAUSED", "EN PAUSE"), 72, 2, TFT_YELLOW, TFT_BLACK);
    drawFooter("", uiText("ENTER CONTINUE     DEL RESET", "ENTER REPRENDRE    DEL REMETTRE"));
  } else {
    centerText(timerCountdown.durationMs ? uiText("READY", "PRET") : uiText("TYPE A TIME", "TAPE UN TEMPS"), 72, 2, TFT_WHITE, TFT_BLACK);
    drawFooter(uiText("0-9 TYPE   < > PRESET   ^v 1 MIN", "0-9 TAPER  < > CHOIX   ^v 1 MIN"), uiText("ENTER START        DEL CLEAR", "ENTER DEMARRER     DEL EFFACER"));
  }
}

void drawScreenTime() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("SCREEN TIME", "TEMPS D'ECRAN"));
  using State = clockapps::Countdown::State;
  const State state = screenTime.countdown.state;
  if (state == State::Idle) {
    centerText(String(screenTime.countdown.durationMs / 60000) + " MIN", 30, 4, TFT_WHITE, TFT_BLACK);
    centerText(uiText("ENTER TO START", "ENTER: DEMARRER"), 72, 2, TFT_GREEN, TFT_BLACK);
    drawFooter("", uiText("ARROWS CHANGE THE TIME", "FLECHES: CHANGER LE TEMPS"));
    return;
  }
  const uint32_t left = screenTime.countdown.leftMs(millis());
  const uint16_t colour = state == State::Paused ? TFT_YELLOW : left <= 60000 ? TFT_RED : left <= 5 * 60000u ? TFT_YELLOW : TFT_GREEN;
  centerText(clockText(screenTime.countdown.leftSeconds(millis())), 30, 4, colour, TFT_BLACK);
  centerText(state == State::Paused ? uiText("PAUSED", "EN PAUSE") : uiText("TIME LEFT", "TEMPS RESTANT"), 72, 2, colour, TFT_BLACK);
  drawFooter(uiText("KEEPS GOING IN OTHER APPS", "CONTINUE DANS LES AUTRES APPS"),
             state == State::Paused ? uiText("ENTER CONTINUE     DEL STOP", "ENTER REPRENDRE    DEL ARRETER") : uiText("ENTER PAUSE        DEL STOP", "ENTER PAUSE        DEL ARRETER"));
}

// The alarm list: one row an alarm, its time at x2 and two switches, Monday to Friday and the
// weekend; a last row adds one while there is room. The cursor is a box around one cell.
constexpr int kAlarmRowsTop = 32;
constexpr int kAlarmRowHeight = 19;
constexpr int kAlarmWeekdaysX = 150;
constexpr int kAlarmWeekendsX = 206;

int alarmRowCount() { return alarms.count + (alarms.count < clockapps::kMaxAlarms ? 1 : 0); }
bool alarmOnAddRow() { return alarmRow >= alarms.count; }

void drawAlarmSwitch(int centreX, int y, bool on, bool selected) {
  auto &display = uiCanvas;
  const int left = centreX - 8;
  if (on) {
    display.fillRect(left, y, 16, 16, TFT_GREEN);
    // A check mark, drawn: Font0 has none that reads at this size.
    for (int thick = 0; thick < 2; thick++) {
      display.drawLine(left + 3, y + 8 + thick, left + 6, y + 11 + thick, TFT_BLACK);
      display.drawLine(left + 6, y + 11 + thick, left + 12, y + 4 + thick, TFT_BLACK);
    }
  } else display.drawRect(left, y, 16, 16, TFT_DARKGREY);
  if (selected) {
    const uint16_t colour = millis() - alarmBumpAt < kHomeBumpMs ? TFT_RED : TFT_YELLOW;
    display.drawRect(left - 3, y - 2, 22, 20, colour); display.drawRect(left - 2, y - 1, 20, 18, colour);
  }
}

void drawAlarm() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("ALARM", "REVEIL"));
  const bool bumping = millis() - alarmBumpAt < kHomeBumpMs;
  display.setTextSize(1); display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (alarms.count) {
    const char *weekdays = uiText("MON-FRI", "LUN-VEN"), *weekends = uiText("SAT-SUN", "SAM-DIM");
    display.setCursor(kAlarmWeekdaysX - static_cast<int>(strlen(weekdays)) * 3, 22); display.print(weekdays);
    display.setCursor(kAlarmWeekendsX - static_cast<int>(strlen(weekends)) * 3, 22); display.print(weekends);
  }
  for (int row = 0; row < alarmRowCount(); row++) {
    const int y = kAlarmRowsTop + row * kAlarmRowHeight;
    const bool selectedRow = row == alarmRow;
    display.setTextSize(2);
    if (row >= alarms.count) {
      const String add = uiText("+ NEW ALARM", "+ NOUVEAU REVEIL");
      if (selectedRow) {
        display.fillRect(2, y - 1, static_cast<int>(add.length()) * 12 + 8, 18, bumping ? TFT_RED : TFT_YELLOW);
        display.setTextColor(TFT_BLACK, bumping ? TFT_RED : TFT_YELLOW);
      } else display.setTextColor(TFT_DARKGREY, TFT_BLACK);
      display.setCursor(6, y + 1); display.print(add);
      continue;
    }
    const clockapps::AlarmEntry &entry = alarms.entries[row];
    // While typing, the cell shows the digits as typed, HH:MM from 00:00; red once four digits make no time.
    const bool typing = selectedRow && alarmTyped.active();
    char typed[6];
    snprintf(typed, sizeof(typed), "%02u:%02u", static_cast<unsigned>(alarmTyped.digits / 100), static_cast<unsigned>(alarmTyped.digits % 100));
    const String time = typing ? String(typed) : alarmTimeText(entry.hour, entry.minute);
    const uint16_t timeColour = entry.on() ? TFT_GREEN : TFT_DARKGREY;
    if (selectedRow && alarmColumn == AlarmColumn::Time) {
      const uint16_t box = bumping || (typing && alarmTyped.full() && !alarmTyped.valid()) ? TFT_RED : typing ? TFT_CYAN : TFT_YELLOW;
      display.fillRect(2, y - 1, static_cast<int>(time.length()) * 12 + 8, 18, box);
      display.setTextColor(TFT_BLACK, box);
    } else display.setTextColor(timeColour, TFT_BLACK);
    display.setCursor(6, y + 1); display.print(time);
    drawAlarmSwitch(kAlarmWeekdaysX, y, entry.weekdays, selectedRow && alarmColumn == AlarmColumn::Weekdays);
    drawAlarmSwitch(kAlarmWeekendsX, y, entry.weekends, selectedRow && alarmColumn == AlarmColumn::Weekends);
  }
  display.setTextSize(1);
  // The line above the keys: what the clock allows, then the snooze, then the next ring.
  struct tm now{};
  char status[48] = "";
  uint16_t statusColour = TFT_WHITE;
  if (alarmTyped.active() && !alarmOnAddRow()) {
    // What Enter would set, or why it can't.
    if (alarmTyped.valid()) {
      snprintf(status, sizeof(status), "%s%s", uiText("ENTER SETS ", "ENTER: "), alarmTimeText(alarmTyped.hour(), alarmTyped.minute()).c_str());
      statusColour = TFT_GREEN;
    } else if (alarmTyped.full()) {
      snprintf(status, sizeof(status), "%s", uiText("NOT A TIME: DEL UNDOES A DIGIT", "PAS UNE HEURE: DEL EFFACE"));
      statusColour = TFT_RED;
    } else snprintf(status, sizeof(status), "%s", uiText("KEEP TYPING: 0730 IS 7:30 AM", "CONTINUE: 0730 DONNE 07:30"));
  } else if (!wallClock(now)) {
    snprintf(status, sizeof(status), "%s", uiText("CLOCK NOT SET: NEEDS WI-FI ONCE", "HEURE INCONNUE: IL FAUT LE WI-FI"));
    statusColour = TFT_RED;
  } else if (alarms.snoozed) {
    const uint32_t left = alarms.snoozeUntil - millis();
    snprintf(status, sizeof(status), "%s%u MIN", uiText("SNOOZED, RINGS AGAIN IN ", "REPORTE, SONNE DANS "), static_cast<unsigned>((left + 59999) / 60000));
    statusColour = TFT_YELLOW;
  } else {
    int next = -1;
    const int32_t minutes = alarms.minutesUntil(now.tm_wday, now.tm_hour, now.tm_min, &next);
    if (minutes < 0) snprintf(status, sizeof(status), "%s", alarms.count ? uiText("NO ALARM IS ON", "AUCUN REVEIL ACTIF") : "");
    else if (minutes >= 60) snprintf(status, sizeof(status), "%s%d H %d MIN", uiText("NEXT RINGS IN ", "PROCHAIN DANS "), static_cast<int>(minutes / 60), static_cast<int>(minutes % 60));
    else snprintf(status, sizeof(status), "%s%d MIN", uiText("NEXT RINGS IN ", "PROCHAIN DANS "), static_cast<int>(minutes));
    statusColour = minutes < 0 ? TFT_DARKGREY : TFT_GREEN;
  }
  display.setTextColor(statusColour, TFT_BLACK); display.setCursor(6, 112); display.print(status);
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123);
  if (alarmOnAddRow()) display.print(uiText("ENTER OR 0-9: NEW ALARM", "ENTER OU 0-9: NOUVEAU"));
  else if (alarmTyped.active()) display.print(uiText("0-9 DIGIT  DEL UNDO  ENTER OK", "0-9 CHIFFRE  DEL ANNULE  ENTER OK"));
  else if (alarmColumn == AlarmColumn::Time) display.print(uiText("0-9 TIME  < > DAYS  DEL DELETE", "0-9 HEURE < > JOURS DEL EFFACER"));
  else display.print(uiText("ENTER ON/OFF  0-9 TIME  DEL DELETE", "ENTER OUI/NON 0-9 HEURE DEL EFFACER"));
}

void drawStopwatch() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK); drawHeader(uiText("STOPWATCH", "CHRONO"));
  const uint32_t elapsed = stopwatch.elapsedMs(millis());
  char text[16];
  const unsigned seconds = elapsed / 1000;
  if (seconds >= 3600) snprintf(text, sizeof(text), "%u:%02u:%02u", seconds / 3600, seconds / 60 % 60, seconds % 60);
  else snprintf(text, sizeof(text), "%u:%02u.%u", seconds / 60, seconds % 60, static_cast<unsigned>(elapsed / 100 % 10));
  centerText(text, 40, 4, stopwatch.running ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
  drawFooter(stopwatch.running ? uiText("KEEPS GOING IN OTHER APPS", "CONTINUE DANS LES AUTRES APPS") : "",
             stopwatch.running ? uiText("ENTER STOP         DEL RESET", "ENTER ARRET        DEL ZERO") : uiText("ENTER START        DEL RESET", "ENTER DEPART       DEL ZERO"));
}

// The alert: the screen flashes, and the tone repeats until a key or the rear button stops it.
void drawRinging() {
  auto &display = uiCanvas;
  const bool flash = (millis() - ringStartedAt) / 500 % 2 == 0;
  const uint16_t background = flash ? TFT_RED : TFT_BLACK;
  const uint16_t colour = flash ? TFT_WHITE : TFT_YELLOW;
  display.fillScreen(background);
  const Ringing what = ringingNow();
  if (what == Ringing::Alarm) {
    centerText(uiText("ALARM", "REVEIL"), 8, 3, colour, background);
    centerText(ringingAlarmText(), 40, 4, colour, background);
    centerText(uiText("ENTER = STOP", "ENTER = ARRET"), 86, 2, colour, background);
    centerText(uiText("OTHER KEY: SNOOZE", "AUTRE: DANS 5 MIN"), 108, 2, colour, background);
    return;
  }
  centerText(uiText("TIME'S UP!", "C'EST FINI !"), 14, 3, colour, background);
  if (what == Ringing::ScreenTime) centerText(uiText("SCREEN TIME IS OVER", "TEMPS D'ECRAN FINI"), 54, 2, colour, background);
  else centerText(String(uiText("TIMER ", "MINUTEUR ")) + clockText(timerCountdown.durationMs / 1000), 54, 2, colour, background);
  centerText(uiText("ANY KEY = STOP", "UNE TOUCHE = ARRET"), 100, 2, colour, background);
}

struct RingNote { uint16_t frequency; uint16_t ms; uint16_t gapMs; };
// Three different calls, so a kid can tell them apart from the next room.
constexpr RingNote kAlarmRing[] = {{1760, 140, 60}, {1319, 140, 60}, {1760, 140, 60}, {1319, 140, 600}};
constexpr RingNote kTimerRing[] = {{2093, 120, 90}, {2093, 120, 90}, {2093, 120, 700}};
constexpr RingNote kScreenRing[] = {{1568, 180, 40}, {1319, 180, 40}, {1047, 300, 700}};

void startRinging() {
  const uint32_t now = millis();
  // Charging mode has the speaker off and the screen dim; an alert brings the device back.
  if (chargingModeActive) exitChargingMode();
  if (media::active()) stopMediaNow();
  if (audioPlaying) stopVoicePlayback();
  messageNotificationPending = false; messageNotificationActive = false;
  ringStartedAt = now; ringKeysFrom = now + 1000; nextRingToneAt = now; ringStep = 0;
  renderDirty = true;
  Serial.printf("CLOCK ringing %d\n", static_cast<int>(ringingNow()));
}

void dismissRinging(bool stop) {
  const Ringing what = ringingNow();
  if (what == Ringing::Alarm) { alarmRinging = false; if (!stop) alarms.snooze(millis()); }
  else if (what == Ringing::ScreenTime) screenTime.countdown.reset();
  else if (what == Ringing::Timer) timerCountdown.reset();
  M5Cardputer.Speaker.stop(0);
  if (ringingNow() != Ringing::None) startRinging();
  Serial.printf("CLOCK dismissed %d %s\n", static_cast<int>(what), stop ? "stop" : "snooze");
  renderDirty = true;
}

// Louder over time: from the volume setting (at least the middle step) up to full in 30 seconds.
void serviceRingTone(uint32_t now) {
  const Ringing what = ringingNow();
  if (what == Ringing::None || !clockapps::reached(now, nextRingToneAt)) return;
  const RingNote *notes = what == Ringing::Alarm ? kAlarmRing : what == Ringing::Timer ? kTimerRing : kScreenRing;
  const uint8_t count = what == Ringing::Alarm ? 4 : 3;
  const RingNote note = notes[ringStep % count];
  ringStep++;
  nextRingToneAt = now + note.ms + note.gapMs;
  messageNotificationPending = false; messageNotificationActive = false;
  if (voiceRecording) return;  // The microphone has the codec; the screen still flashes.
  const uint8_t level = std::min<uint8_t>(4, std::max<uint8_t>(volumeLevel, 2) + (now - ringStartedAt) / 10000);
  M5Cardputer.Speaker.setVolume(kVolumeValues[level]); M5Cardputer.Speaker.setChannelVolume(0, 255);
  M5Cardputer.Speaker.tone(note.frequency, note.ms, 0, true);
  lastToneAt = now;
}

void serviceClockApps() {
  const uint32_t now = millis();
  static bool clockReported = false;
  if (clockFromNtp && !clockReported) {
    clockReported = true; struct tm wall{};
    if (getLocalTime(&wall, 10)) Serial.printf("CLOCK set by NTP %04d-%02d-%02d %02d:%02d:%02d\n", wall.tm_year + 1900, wall.tm_mon + 1, wall.tm_mday, wall.tm_hour, wall.tm_min, wall.tm_sec);
  }
  bool ring = timerCountdown.expire(now);
  const clockapps::Warning warning = screenTime.warning(now);
  if (screenTime.countdown.expire(now)) ring = true;
  if (alarms.snoozeDue(now)) { alarmRinging = true; ring = true; }
  if (now - lastAlarmCheckAt >= 1000) {
    lastAlarmCheckAt = now;
    struct tm wall{};
    if (wallClock(wall) && alarms.due(wall.tm_wday, wall.tm_yday, wall.tm_hour, wall.tm_min)) { alarmRinging = true; ring = true; }
  }
  if (ring) startRinging();
  if (warning != clockapps::Warning::None && !ring) {
    Serial.printf("CLOCK warning %s\n", warning == clockapps::Warning::OneMinute ? "1 min" : "5 min");
    if (!chargingModeActive) {
      bannerWarning = warning; bannerUntil = now + 10000; renderDirty = true;
      if (!voiceRecording) {
        M5Cardputer.Speaker.setVolume(kVolumeValues[std::max<uint8_t>(volumeLevel, 2)]); M5Cardputer.Speaker.setChannelVolume(0, 255);
        M5Cardputer.Speaker.tone(1319, 150, 0, true); M5Cardputer.Speaker.tone(1760, 250, 0, false);
        lastToneAt = now;
      }
    }
  }
  if (bannerWarning != clockapps::Warning::None && clockapps::reached(now, bannerUntil)) { bannerWarning = clockapps::Warning::None; renderDirty = true; }
  serviceRingTone(now);
  // Redraws: the flash, the counting screens, and HOME's line while something counts.
  const uint32_t since = now - lastRenderAt;
  if (ringingNow() != Ringing::None) { if (since >= 250) renderDirty = true; return; }
  if (screenMode == ScreenMode::Stopwatch && stopwatch.running && since >= 100) renderDirty = true;
  else if ((screenMode == ScreenMode::Timer || screenMode == ScreenMode::ScreenTime) && since >= 250) renderDirty = true;
  else if (screenMode == ScreenMode::Alarm && (since >= 1000 || (alarmBumpAt && millis() - alarmBumpAt < kHomeBumpMs + 100 && since >= 50))) renderDirty = true;
  else if (screenMode == ScreenMode::Home && clockLine().length() && since >= 1000) renderDirty = true;
}

constexpr uint16_t kTimerPresetMinutes[] = {1, 2, 3, 5, 10, 15, 20, 30, 45, 60};
constexpr uint16_t kScreenTimeMinutes[] = {15, 30, 45, 60, 90, 120};

void editAlarm(int row, uint8_t hour, uint8_t minute) {
  clockapps::AlarmEntry &entry = alarms.entries[row];
  entry.hour = hour; entry.minute = minute;
  if (alarms.ringing == row) alarms.snoozed = false;
  // Setting the time to the minute it is now doesn't ring at once.
  struct tm wall{};
  if (wallClock(wall)) entry.lastFired = clockapps::Alarms::minuteKey(wall.tm_yday, wall.tm_hour, wall.tm_min);
  saveClockSettings();
}

// A key that can't act here: a falling tone and the cursor turns red for a moment.
void bumpAlarm() { alarmBumpAt = millis(); if (!alarmBumpAt) alarmBumpAt = 1; playDeniedTone(); }

// The time being typed goes into the alarm if it makes one. False, with a bump, if it doesn't, so
// the cursor stays on it: Del takes back digits.
bool commitAlarmTyping() {
  if (!alarmTyped.active()) return true;
  if (!alarmTyped.valid()) { bumpAlarm(); return false; }
  editAlarm(alarmRow, alarmTyped.hour(), alarmTyped.minute());
  alarmTyped.clear();
  return true;
}

// The alarm list's keys. False when the key did nothing, so no key tone plays over the bump.
bool handleAlarmKeys(const Keyboard_Class::KeysState &keys, int digit, bool goUp, bool goDown, bool goLeft, bool goRight) {
  if (goUp || goDown) {
    const int target = alarmRow + (goDown ? 1 : -1);
    if (target < 0 || target >= alarmRowCount()) { bumpAlarm(); return false; }
    if (!commitAlarmTyping()) return false;
    alarmRow = static_cast<uint8_t>(target);
    return true;
  }
  if (alarmOnAddRow()) {
    if (!keys.enter && digit < 0) { bumpAlarm(); return false; }
    if (!alarms.add()) { bumpAlarm(); return false; }
    alarmRow = alarms.count - 1; alarmColumn = AlarmColumn::Time; alarmTyped.clear();
    editAlarm(alarmRow, alarms.entries[alarmRow].hour, alarms.entries[alarmRow].minute);
    if (digit >= 0) alarmTyped.push(digit);
    return true;
  }
  clockapps::AlarmEntry &entry = alarms.entries[alarmRow];
  if (goLeft || goRight) {
    const int target = static_cast<int>(alarmColumn) + (goRight ? 1 : -1);
    if (target < 0 || target > static_cast<int>(AlarmColumn::Weekends)) { bumpAlarm(); return false; }
    if (!commitAlarmTyping()) return false;
    alarmColumn = static_cast<AlarmColumn>(target);
    return true;
  }
  if (keys.del) {
    // While typing, Del takes back one digit; otherwise it deletes the alarm.
    if (alarmTyped.undo()) return true;
    alarms.remove(alarmRow); alarmColumn = AlarmColumn::Time;
    saveClockSettings();
    return true;
  }
  if (digit >= 0) {
    // A digit on a switch types the time too.
    alarmColumn = AlarmColumn::Time;
    alarmTyped.push(digit);
    if (alarmTyped.full()) {
      if (!alarmTyped.valid()) { bumpAlarm(); return false; }
      commitAlarmTyping();
    }
    return true;
  }
  if (keys.enter) {
    // On the time, Enter takes what is typed, or steps to the first switch; on a switch, it flips it.
    if (alarmColumn == AlarmColumn::Time) {
      if (alarmTyped.active()) return commitAlarmTyping();
      alarmColumn = AlarmColumn::Weekdays; return true;
    }
    bool &day = alarmColumn == AlarmColumn::Weekdays ? entry.weekdays : entry.weekends;
    day = !day;
    if (alarms.ringing == alarmRow && !entry.on()) alarms.snoozed = false;
    editAlarm(alarmRow, entry.hour, entry.minute);
    return true;
  }
  bumpAlarm();
  return false;
}

bool handleClockKeys(const Keyboard_Class::KeysState &keys, bool goUp, bool goDown, bool goLeft, bool goRight) {
  const uint32_t now = millis();
  using State = clockapps::Countdown::State;
  int digit = -1;
  for (const auto character : keys.word) if (character >= '0' && character <= '9') digit = character - '0';
  if (screenMode == ScreenMode::Timer) {
    const State state = timerCountdown.state;
    if (keys.enter) {
      if (state == State::Running) timerCountdown.pause(now);
      else if (timerCountdown.start(now)) saveClockSettings();
      timerTyped = 0;
    } else if (keys.del) {
      if (state == State::Idle) timerCountdown.set(0);
      else timerCountdown.reset();
      timerTyped = 0;
    } else if (state != State::Idle) return true;
    else if (digit >= 0) { timerTyped = clockapps::timerPushDigit(timerTyped, digit); timerCountdown.set(clockapps::timerDigitsMs(timerTyped)); }
    else if (goUp || goDown) {
      const uint32_t minute = 60000, most = (99 * 60 + 59) * 1000u;
      const uint32_t current = timerCountdown.durationMs;
      timerCountdown.set(goUp ? std::min(most, current + minute) : current > minute ? current - minute : 0);
      timerTyped = 0;
    } else if (goLeft || goRight) {
      const uint32_t current = timerCountdown.durationMs;
      uint32_t next = current;
      if (goRight) { for (const uint16_t preset : kTimerPresetMinutes) if (preset * 60000u > current) { next = preset * 60000u; break; } }
      else { for (const uint16_t preset : kTimerPresetMinutes) if (preset * 60000u < current) next = preset * 60000u; }
      timerCountdown.set(next); timerTyped = 0;
    }
    return true;
  }
  if (screenMode == ScreenMode::ScreenTime) {
    clockapps::Countdown &countdown = screenTime.countdown;
    if (keys.enter) {
      if (countdown.state == State::Running) countdown.pause(now);
      else if (screenTime.start(now)) saveClockSettings();
    } else if (keys.del) countdown.reset();
    else if (countdown.state == State::Idle && (goUp || goDown || goLeft || goRight)) {
      const int count = sizeof(kScreenTimeMinutes) / sizeof(kScreenTimeMinutes[0]);
      int index = 0;
      for (int choice = 0; choice < count; choice++) if (kScreenTimeMinutes[choice] * 60000u == countdown.durationMs) index = choice;
      index = (index + ((goUp || goRight) ? 1 : count - 1)) % count;
      countdown.set(kScreenTimeMinutes[index] * 60000u);
    }
    return true;
  }
  if (screenMode == ScreenMode::Alarm) return handleAlarmKeys(keys, digit, goUp, goDown, goLeft, goRight);
  // Stopwatch.
  if (keys.enter) stopwatch.toggle(now);
  else if (keys.del) stopwatch.reset();
  return true;
}

void drawLanguage();
void drawChangelog();
void drawKeyHelp();
void render() {
  if (ringingNow() != Ringing::None) drawRinging();
  else if (screenMode == ScreenMode::Timer) drawTimer(); else if (screenMode == ScreenMode::Alarm) drawAlarm();
  else if (screenMode == ScreenMode::ScreenTime) drawScreenTime(); else if (screenMode == ScreenMode::Stopwatch) drawStopwatch();
  else if (screenMode == ScreenMode::Chat) drawChat(); else if (screenMode == ScreenMode::Menu) drawMenu(); else if (screenMode == ScreenMode::Rooms) drawRooms();
  else if (screenMode == ScreenMode::Volume) drawVolume(); else if (screenMode == ScreenMode::Language) drawLanguage(); else if (screenMode == ScreenMode::Changelog) drawChangelog();
  else if (screenMode == ScreenMode::EmojiRecipes) drawEmojiRecipes();
  else if (screenMode == ScreenMode::KeyHelp) drawKeyHelp();
  else if (screenMode == ScreenMode::VoiceMessages || screenMode == ScreenMode::Walkie) drawWalkie();
  else if (screenMode == ScreenMode::Status) drawStatus();
  else if (screenMode == ScreenMode::NetworkPassword) drawNetworkPassword();
  else if (screenMode == ScreenMode::ChargingConfirm) drawChargingConfirm();
  else if (screenMode == ScreenMode::Charging) drawCharging();
  else if (screenMode == ScreenMode::FoxFinding) drawFoxFinding();
  else if (screenMode == ScreenMode::Home) drawHome();
  else if (screenMode == ScreenMode::Library) drawLibrary();
  else if (screenMode == ScreenMode::LibrarySearch) drawLibrarySearch();
  else if (screenMode == ScreenMode::NowPlaying) drawNowPlaying();
  else if (screenMode == ScreenMode::Remote) drawRemote();
  else if (screenMode == ScreenMode::System) drawSystem();
  else drawNetworks();
  if (ringingNow() == Ringing::None) drawWarningBanner();
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
  if (enabled) stopMediaNow();
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

// The French keyboard's combinations, generated from french_keyboard.h: what isn't on a keycap.
frkb::Recipe keyHelpRecipes[48];
int keyHelpCount = -1;
constexpr int kKeyHelpRows = 5;
int keyHelpPageCount() {
  if (keyHelpCount < 0) keyHelpCount = std::min(48, frkb::recipes(keyHelpRecipes, 48));
  return (keyHelpCount + kKeyHelpRows - 1) / kKeyHelpRows;
}

void drawKeyHelp() {
  auto &display = uiCanvas; display.fillScreen(TFT_BLACK);
  const int pages = keyHelpPageCount();
  keyHelpPage = std::max(0, std::min(keyHelpPage, pages - 1));
  const String title = String("TOUCHES ") + (keyHelpPage + 1) + "/" + pages;
  drawHeader(title.c_str());
  display.setFont(&fonts::Font0); display.setTextSize(2);
  for (int row = 0; row < kKeyHelpRows; row++) {
    const int index = keyHelpPage * kKeyHelpRows + row;
    if (index >= keyHelpCount) break;
    const int y = 23 + row * 17;
    display.setTextColor(TFT_YELLOW, TFT_BLACK); display.setCursor(8, y); printFont0Text(keyHelpRecipes[index].text);
    display.setTextColor(TFT_WHITE, TFT_BLACK); display.setCursor(32, y); display.print(keyHelpRecipes[index].keys);
  }
  display.setTextSize(1); display.setTextColor(TFT_LIGHTGREY, TFT_BLACK); display.setCursor(6, 111); display.print("SHIFT+LETTRE: MAJUSCULE");
  display.setTextColor(TFT_DARKGREY, TFT_BLACK); display.setCursor(6, 123); display.print("HAUT/BAS PAGE      ENTER RETOUR");
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
    ListsLock lists;
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
    if ((first == 0xC3 || first == 0xC2) && index + 1 < text.length()) {
      const uint8_t second = static_cast<uint8_t>(text[index + 1]);
      // The CP437 slot of each letter the French keyboard types. CP437 has no capital À Â È Ê Ë Î Ï Ô
      // Ù Û: Font0 draws those as the plain capital, and the message keeps its accent.
      static const uint8_t kC3[64] = {
          'A', 'A', 'A', 'A', 0x8E, 0x8F, 0x92, 0x80, 'E', 0x90, 'E', 'E', 'I', 'I', 'I', 'I',   // À..Ï
          0, 0xA5, 'O', 'O', 'O', 'O', 0x99, 0, 0, 'U', 'U', 'U', 0x9A, 'Y', 0, 0xE1,           // Ð..ß
          0x85, 0xA0, 0x83, 0, 0x84, 0x86, 0x91, 0x87, 0x8A, 0x82, 0x88, 0x89, 0x8D, 0xA1, 0x8C, 0x8B,  // à..ï
          0, 0xA4, 0x95, 0xA2, 0x93, 0, 0x94, 0xF6, 0, 0x97, 0xA3, 0x96, 0x81, 0, 0, 0x98};     // ð..ÿ
      uint8_t slot = 0;
      if (first == 0xC3 && second >= 0x80 && second < 0xC0) slot = kC3[second - 0x80];
      else if (first == 0xC2) slot = second == 0xAB ? 0xAE : second == 0xBB ? 0xAF : second == 0xB0 ? 0xF8 : second == 0xA8 ? '"' : 0;
      if (slot) { encoded += static_cast<char>(slot); index++; continue; }
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

// Chat in French types the CSA layout of french_keyboard.h, read from the keys' positions, since
// the M5 library only knows the English keycaps. Elsewhere, and in English, keys type their keycaps.
void typeFrenchKeys(const Keyboard_Class::KeysState &keys, String &target, size_t limit) {
  const bool shift = keys.shift || keys.ctrl, alt = keys.alt;
  auto out = [&](const char *text) { if (text) appendKeyboardText(target, text, limit, false); };
  for (const auto &key : M5Cardputer.Keyboard.keyList()) frenchTypist.press(key.x, key.y, shift, alt, out);
}

void flushFrenchDeadKey(String &target, size_t limit) {
  frenchTypist.flush([&](const char *text) { appendKeyboardText(target, text, limit, false); });
}

void showChatNotice(const char *english, const char *french) { chatNotice = uiText(english, french); chatNoticeAt = millis(); renderDirty = true; }

// Broadcasts a typed message to devices in radio range. Nothing acknowledges a broadcast at the
// radio level, so serviceNearbySends() repeats it until a nearby device answers with TextAck.
bool broadcastNearbyText(const String &clientId, const String &body) {
  if (!kEspNowEnabled || !meshReady || !espNowActive) return false;
  static const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
  EspNowPacket packet{}; packet.magic = kEspNowMagic; packet.type = static_cast<uint8_t>(EspNowType::Text); packet.sequence = ++espNowSequence;
  strncpy(packet.senderId, kDeviceId, sizeof(packet.senderId) - 1); strncpy(packet.senderName, kDeviceName, sizeof(packet.senderName) - 1);
  currentRoomId.copyTo(packet.roomId, sizeof(packet.roomId));
  strncpy(packet.clientId, clientId.c_str(), sizeof(packet.clientId) - 1);
  const size_t length = std::min<size_t>(body.length(), sizeof(packet.payload));
  if (!encryptMeshPacket(packet, reinterpret_cast<const uint8_t *>(body.c_str()), length)) return false;
  return esp_now_send(broadcast, reinterpret_cast<uint8_t *>(&packet), offsetof(EspNowPacket, payload) + packet.payloadLength) == ESP_OK;
}

void sendNearbyAck(const char *clientId) {
  if (!kEspNowEnabled || !meshReady || !espNowActive) return;
  static const uint8_t broadcast[] = {0xff,0xff,0xff,0xff,0xff,0xff};
  EspNowPacket packet{}; packet.magic = kEspNowMagic; packet.type = static_cast<uint8_t>(EspNowType::TextAck); packet.sequence = ++espNowSequence;
  strncpy(packet.senderId, kDeviceId, sizeof(packet.senderId) - 1); strncpy(packet.senderName, kDeviceName, sizeof(packet.senderName) - 1);
  currentRoomId.copyTo(packet.roomId, sizeof(packet.roomId));
  strncpy(packet.clientId, clientId, sizeof(packet.clientId) - 1);
  const uint8_t marker = 1;
  if (encryptMeshPacket(packet, &marker, 1)) esp_now_send(broadcast, reinterpret_cast<uint8_t *>(&packet), offsetof(EspNowPacket, payload) + packet.payloadLength);
}

void serviceNearbySends() {
  if (chargingModeActive) return;
  std::vector<String> acks;
  xSemaphoreTake(stateMutex, portMAX_DELAY); acks.swap(pendingNearbyAcks); xSemaphoreGive(stateMutex);
  for (const auto &clientId : acks) sendNearbyAck(clientId.c_str());
  if (nearbySends.empty()) return;
  const uint32_t now = millis();
  struct Resend { String clientId, body; };
  std::vector<Resend> resend;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  for (auto item = nearbySends.begin(); item != nearbySends.end();) {
    if (item->heardBy || item->tries >= kNearbyTries) { item = nearbySends.erase(item); continue; }
    if (outbox::due(now, item->nextAt)) {
      auto message = std::find_if(messages.begin(), messages.end(), [&](const ChatMessage &m) { return m.clientId == item->clientId; });
      if (message != messages.end()) resend.push_back({item->clientId, message->body});
      item->tries++;
      item->nextAt = now + (item->tries >= 6 ? 14000 : 1000u << (item->tries - 1));
    }
    ++item;
  }
  xSemaphoreGive(stateMutex);
  for (const auto &item : resend) broadcastNearbyText(item.clientId, item.body);
}

void sendDraft() {
  flushFrenchDeadKey(draft, kMessageLimit);
  draft.trim(); if (draft.isEmpty()) return;
  ChatMessage message; message.roomId = currentRoomId; message.clientId = nextClientId(); message.authorId = kDeviceId; message.authorName = kDeviceName;
  message.body = draft; message.createdAt = time(nullptr) * 1000LL; message.queued = true; message.state = "queued";
  message.replyToId = replyToMessageId; message.replyAuthor = replyToAuthor; message.replyBody = replyToBody;
  outbox::Entry entry; entry.clientId = message.clientId.c_str(); entry.roomId = message.roomId.c_str(); entry.body = message.body.c_str();
  entry.replyToId = message.replyToId; entry.createdAt = message.createdAt; entry.nextAttemptAt = millis();
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  // A full outbox refuses the message and says so; the text stays in the composer.
  const bool full = outboxEntries.size() >= outbox::kMaxEntries;
  if (!full) {
    outboxEntries.push_back(entry); saveOutboxLocked();
    messages.push_back(message); trimHistory(); saveHistoryLocked();
    nearbySends.push_back({message.clientId, millis(), millis(), 0, 0});
  }
  xSemaphoreGive(stateMutex);
  if (full) { showChatNotice("OUTBOX FULL: 16 NOT SENT YET", "BOITE PLEINE: 16 EN ATTENTE"); playDeniedTone(); return; }
  serviceNearbySends();
  syncOverride = true;
  draft = ""; historyOffset = 0; replyToMessageId = 0; replyToAuthor = ""; replyToBody = ""; renderDirty = true;
}

void scanForNetworks() {
  manualWifiMode = true; networkStatus = "SCANNING"; scannedNetworks.clear(); networkSelection = 0;
  renderDirty = true; render();
  // The network task may be mid-join or mid-scan; two scans at once make the second fail.
  for (const uint32_t started = millis(); !networkTaskParked && millis() - started < 20000;) delay(50);
  WiFi.disconnect(false, false); delay(100);
  int count = WiFi.scanNetworks(false, true);
  for (int attempt = 0; attempt < 5 && count < 0; attempt++) { delay(1000); count = WiFi.scanNetworks(false, true); }
  for (int index = 0; index < count && scannedNetworks.size() < kScanListLimit; index++) {
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
  {
    ListsLock lists;
    auto saved = std::find_if(wifiProfiles.begin(), wifiProfiles.end(),
                              [&](const WifiProfile &profile) { return profile.ssid == selectedSsid; });
    if (saved != wifiProfiles.end()) networkPassword = saved->password;
  }
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
  networkStatus = "SAVED + CONNECTED"; screenMode = ScreenMode::Networks; manualWifiMode = false; wifiResumeRequested = true; renderDirty = true;
}

void openSelectedMenuItem() {
  const int selection = menuSelection;
  if (selection == 0) screenMode = ScreenMode::Chat;
  else if (selection == 1) screenMode = ScreenMode::Rooms;
  else if (selection == 2) { if (!localOnlyMode) syncOverride = true; networkStatus = localOnlyMode ? "ESPNOW LOCAL" : "SYNC REQUESTED"; screenMode = ScreenMode::Chat; }
  else if (selection == 3) { screenMode = ScreenMode::VoiceMessages; walkieStatus = "READY"; }
  else if (selection == 4) { screenMode = ScreenMode::Walkie; walkieStatus = localOnlyMode ? "ESPNOW LOCAL" : "READY TO TALK"; }
  else if (selection == 5) screenMode = ScreenMode::EmojiRecipes;
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

int roomCount() { ListsLock lists; return static_cast<int>(rooms.size()); }

void selectRoom(int next) {
  String nextId, nextName;
  {
    ListsLock lists;
    if (next < 0 || next >= static_cast<int>(rooms.size()) || rooms[next].id == currentRoomId) return;
    nextId = rooms[next].id; nextName = rooms[next].name;
  }
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  saveHistoryLocked();
  currentRoomId = nextId; currentRoomName = nextName; roomSelection = next;
  applyEffectiveLanguage();
  messages.clear(); lastServerId = 0; lastReceiptAt = 0; historyOffset = 0; initialSyncComplete = false;
  historyBeforeId = 0; historyHydratedCount = 0; syncOverride = true;
  loadHistory();
  xSemaphoreGive(stateMutex);
  walkieSocket.disconnect(); walkieInitialized = false; networkStatus = "SWITCHING"; renderDirty = true;
}

void switchRoom(int direction) {
  int target = -1;
  {
    ListsLock lists;
    if (rooms.size() < 2) return;
    int active = 0; for (int index = 0; index < static_cast<int>(rooms.size()); index++) if (rooms[index].id == currentRoomId) active = index;
    target = (active + direction + static_cast<int>(rooms.size())) % static_cast<int>(rooms.size());
  }
  selectRoom(target);
}

String trackPath(const library::Item &item) { return queueFolder + "/" + item.title + ".mp3"; }

void savePlace() {
  const auto *item = nowPlayingItem();
  if (!item || queueCollection != library::Collection::Books) return;
  if (item->onCard) library::saveCardPlace(queueFolder, trackPath(*item), media::positionSeconds());
  else library::saveBookmark(item->id, media::positionSeconds() * 1000);
}

bool mediaOwnsRadio() { return mediaStarting || media::active() || radioOffWanted; }

void stopMediaNow() {
  if (!media::active()) return;
  const auto *item = nowPlayingItem();
  savePlace();
  media::stop();
  const uint32_t started = millis();
  while (media::state() != media::State::Idle && millis() - started < 1500) delay(5);
}

void shrinkChatForPlayback() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (!chatShrunk && initialSyncComplete && sdReady && messages.size() > kShrunkMessages) {
    saveHistoryLocked();
    messages.erase(messages.begin(), messages.end() - kShrunkMessages);
    messages.shrink_to_fit();
    historyOffset = 0;
    chatShrunk = true;
  }
  xSemaphoreGive(stateMutex);
}

void startQueuedTrack(uint32_t offsetSeconds) {
  const auto *item = nowPlayingItem();
  if (!item) return;
  // The microphone and the speaker share the audio hardware.
  M5Cardputer.Mic.end(); M5Cardputer.Speaker.begin(); M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  // Give the walkie socket's RAM back before the player asks for its own, and chat's
  // messages too: they are on the card, and come back when playback ends.
  mediaStarting = true;
  shrinkChatForPlayback();
  for (const uint32_t started = millis(); walkieInitialized && millis() - started < 1500;) delay(10);
  // The card needs no radio, and the Wi-Fi driver's buffers are the biggest thing the player can get back.
  if (!item->onCard) radioOffWanted = false;
  if (item->onCard) {
    radioOffWanted = true;
    lastCardMediaAt = millis();
    for (const uint32_t started = millis(); !radioIsOff && millis() - started < kRadioOffWaitMs;) delay(10);
  }
  if (item->onCard) media::playFile(trackPath(*item), offsetSeconds);
  else media::play(library::streamUrl(item->id, queueCollection, offsetSeconds), offsetSeconds);
  mediaStarting = false;
  lastBookmarkAt = millis();
}

// A media app asks for its tasks; they start once any secure connection in flight has
// finished, and until then its first request waits in the queue with LOADING on the screen.
void startMediaTasks() { mediaTasksWanted = true; os::holdSecure(true); }

void openLibraryList(LibraryView view, const String &title) {
  libraryView = view; libraryTitle = title; libraryItems.clear(); librarySelection = 0;
  screenMode = ScreenMode::Library; renderDirty = true;
}

void openMusicRoot() {
  libraryCollection = library::Collection::Music; libraryView = LibraryView::MusicRoot;
  os::setMediaOwner(os::Owner::Music);
  startMediaTasks();
  library::forget();
  libraryTitle = uiText("MUSIC", "MUSIQUE"); libraryItems.clear(); librarySelection = 0;
  for (const char *label : {uiText("ON THE CARD", "SUR LA CARTE"), uiText("ALBUMS", "ALBUMS"), uiText("LIKED SONGS", "CHANSONS AIMEES"), uiText("SEARCH", "CHERCHER")}) {
    library::Item item; item.title = label; libraryItems.push_back(item);
  }
  if (media::active()) { library::Item item; item.title = uiText("NOW PLAYING", "EN LECTURE"); libraryItems.push_back(item); }
  screenMode = ScreenMode::Library; renderDirty = true;
}

void openBooks() {
  libraryCollection = library::Collection::Books; libraryOffset = 0;
  os::setMediaOwner(os::Owner::Books);
  startMediaTasks();
  openLibraryList(LibraryView::Albums, uiText("BOOKS", "LIVRES"));
  library::requestAlbums(library::Collection::Books, 0);
}

void bumpHome(int direction) {
  homeBumpAt = millis(); if (!homeBumpAt) homeBumpAt = 1;
  homeBumpDirection = direction;
  playDeniedTone(); renderDirty = true;
}

void openHomeApp(int index) {
  library::forget();
  const AppId app = kHomeApps[index].id;
  if (app == AppId::Chat) screenMode = ScreenMode::Chat;
  else if (app == AppId::Music) openMusicRoot();
  else if (app == AppId::Books) openBooks();
  else if (app == AppId::Remote) screenMode = ScreenMode::Remote;
  else if (app == AppId::Timer) { timerTyped = 0; screenMode = ScreenMode::Timer; }
  else if (app == AppId::Alarm) { alarmTyped.clear(); alarmRow = 0; alarmColumn = AlarmColumn::Time; screenMode = ScreenMode::Alarm; }
  else if (app == AppId::ScreenTime) screenMode = ScreenMode::ScreenTime;
  else if (app == AppId::Stopwatch) screenMode = ScreenMode::Stopwatch;
  else if (app == AppId::Wifi) {
    // Wi-Fi is off while local mode is on, and joining a network would have nowhere to go.
    if (localOnlyMode) { homeNotice = uiText("TURN LOCAL OFF FIRST", "COUPER LOCAL D'ABORD"); homeNoticeAt = millis(); bumpHome(0); return; }
    screenMode = ScreenMode::Networks; scanForNetworks();
  }
  else if (app == AppId::Volume) screenMode = ScreenMode::Volume;
  else if (app == AppId::Status) screenMode = ScreenMode::Status;
  else if (app == AppId::Language) screenMode = ScreenMode::Language;
  else if (app == AppId::Charging) screenMode = ScreenMode::ChargingConfirm;
  else if (app == AppId::Local) setLocalOnlyMode(!localOnlyMode);
  else if (app == AppId::Changelog) screenMode = ScreenMode::Changelog;
  else screenMode = ScreenMode::System;
  renderDirty = true;
}

// The device's settings: apps on HOME, each going back to it.
bool isSettingsScreen() {
  return screenMode == ScreenMode::Networks || screenMode == ScreenMode::NetworkPassword || screenMode == ScreenMode::Volume || screenMode == ScreenMode::Status ||
         screenMode == ScreenMode::Language || screenMode == ScreenMode::ChargingConfirm || screenMode == ScreenMode::Changelog;
}

void leaveSettingsScreen() {
  if (screenMode == ScreenMode::NetworkPassword) { networkPassword = ""; screenMode = ScreenMode::Networks; return; }
  manualWifiMode = false; statusPage = 0; screenMode = ScreenMode::Home;
}

// One level back within HOME's apps. False when the screen is not one of them.
bool goBackFromApp() {
  if (screenMode == ScreenMode::Home) return true;
  if (screenMode == ScreenMode::KeyHelp) { screenMode = ScreenMode::Chat; return true; }
  if (screenMode == ScreenMode::Remote || screenMode == ScreenMode::System || isClockScreen()) { screenMode = ScreenMode::Home; return true; }
  if (isSettingsScreen()) { leaveSettingsScreen(); return true; }
  if (screenMode == ScreenMode::LibrarySearch) { openMusicRoot(); return true; }
  if (screenMode == ScreenMode::NowPlaying) {
    if (queueCollection == library::Collection::Books) openBooks(); else openMusicRoot();
    return true;
  }
  if (screenMode != ScreenMode::Library) return false;
  if (libraryView == LibraryView::Tracks && tracksFromCardList) {
    openLibraryList(LibraryView::CardAlbums, uiText("ON THE CARD", "SUR LA CARTE")); library::requestCardAlbums(libraryCollection);
  } else if (libraryView == LibraryView::Tracks) {
    libraryOffset = 0; openLibraryList(LibraryView::Albums, libraryCollection == library::Collection::Books ? uiText("BOOKS", "LIVRES") : uiText("ALBUMS", "ALBUMS"));
    library::requestAlbums(libraryCollection, 0);
  } else if (libraryView == LibraryView::MusicRoot || libraryCollection == library::Collection::Books) screenMode = ScreenMode::Home;
  else openMusicRoot();
  return true;
}

void openLibrarySelection() {
  if (libraryView == LibraryView::MusicRoot) {
    if (librarySelection == 0) { libraryOffset = 0; openLibraryList(LibraryView::CardAlbums, uiText("ON THE CARD", "SUR LA CARTE")); library::requestCardAlbums(library::Collection::Music); }
    else if (librarySelection == 1) { radioOffWanted = false; libraryOffset = 0; openLibraryList(LibraryView::Albums, uiText("ALBUMS", "ALBUMS")); library::requestAlbums(library::Collection::Music, 0); }
    else if (librarySelection == 2) { radioOffWanted = false; openLibraryList(LibraryView::Liked, uiText("LIKED", "AIMEES")); library::requestLiked(); }
    else if (librarySelection == 3) { radioOffWanted = false; librarySearchText = ""; screenMode = ScreenMode::LibrarySearch; }
    else screenMode = ScreenMode::NowPlaying;
    renderDirty = true; return;
  }
  if (librarySelection >= static_cast<int>(libraryItems.size())) return;
  if (libraryView == LibraryView::Albums || libraryView == LibraryView::CardAlbums) {
    const auto album = libraryItems[librarySelection];
    tracksFromCardList = libraryView == LibraryView::CardAlbums;
    openLibraryList(LibraryView::Tracks, album.title);
    libraryFolder = album.path;
    // A folder's song list doesn't fit beside the player, so opening one on the card stops what's playing.
    if (album.path.length()) { stopMediaNow(); library::requestCardTracks(album.path); } else library::requestTracks(album.id);
    return;
  }
  // A list becomes the play queue, so the next song follows on its own.
  stopMediaNow();
  // Moved, not copied: a long book's list twice over doesn't fit next to the player.
  queueIndex = librarySelection; queueFolder = libraryFolder; queueCollection = libraryCollection;
  playingOwner = os::mediaOwner();
  if (libraryItems[librarySelection].onCard) {
    // A chapter on the card needs no list beside the player: the next is found when wanted.
    const library::Item chosen = libraryItems[librarySelection];
    std::vector<library::Item>().swap(libraryItems);
    std::vector<library::Item>().swap(playQueue);
    playQueue.push_back(chosen);
    queueIndex = 0;
  } else {
    playQueue = std::move(libraryItems); std::vector<library::Item>().swap(libraryItems);
  }
  const auto *item = nowPlayingItem();
  startQueuedTrack(item->resumeMs / 1000);
  screenMode = ScreenMode::NowPlaying; renderDirty = true;
}

void savePlaceAtStart() {
  const auto *item = nowPlayingItem();
  if (item && item->onCard && queueCollection == library::Collection::Books) library::saveCardPlace(queueFolder, trackPath(*item), 0);
}

void moveQueue(int direction) {
  if (queueIndex >= 0 && queueIndex < static_cast<int>(playQueue.size()) && playQueue[queueIndex].onCard) {
    stopMediaNow();
    // The radio and chat's RAM stay with the player across the gap between chapters.
    mediaStarting = true;
    library::requestAdjacentTrack(queueFolder, playQueue[queueIndex].title, direction);
    adjacentWanted = true;
    return;
  }
  const int next = queueIndex + direction;
  if (next < 0 || next >= static_cast<int>(playQueue.size())) return;
  stopMediaNow();
  // The place moves with the chapter: a card book keeps one file, a server book one bookmark.
  if (queueCollection == library::Collection::Books && !playQueue[queueIndex].onCard) library::deleteBookmark(playQueue[queueIndex].id);
  queueIndex = next; startQueuedTrack(0); savePlaceAtStart(); renderDirty = true;
}

// Which app the current screen belongs to; chat owns its own screens and the settings belong to no app.
os::Owner frontApp() {
  switch (screenMode) {
    case ScreenMode::Home: case ScreenMode::System: case ScreenMode::Remote:
    case ScreenMode::Networks: case ScreenMode::NetworkPassword: case ScreenMode::Volume: case ScreenMode::Status:
    case ScreenMode::Language: case ScreenMode::ChargingConfirm: case ScreenMode::Changelog:
    case ScreenMode::Timer: case ScreenMode::Alarm: case ScreenMode::ScreenTime: case ScreenMode::Stopwatch: return os::Owner::Ui;
    case ScreenMode::Library: case ScreenMode::LibrarySearch: case ScreenMode::NowPlaying: return os::mediaOwner();
    default: return os::Owner::Chat;
  }
}

void serviceApps() {
  const os::Owner front = frontApp();
  for (const os::Owner app : {os::Owner::Music, os::Owner::Books}) {
    const bool playing = media::active() && playingOwner == app;
    os::setState(app, front == app ? os::AppState::Foreground : playing ? os::AppState::Background : os::AppState::Stopped);
  }
  if (initialSyncComplete) os::setState(os::Owner::Chat, front == os::Owner::Chat ? os::AppState::Foreground : os::AppState::Background);
  os::setState(os::Owner::Ui, front == os::Owner::Ui ? os::AppState::Foreground : os::AppState::Background);
  // A media app that went away gives its list back.
  if (screenMode == ScreenMode::Home && !libraryItems.empty()) { std::vector<library::Item>().swap(libraryItems); renderDirty = true; }
  // And with nothing playing, the tasks too: their stacks are chat's room for a connection.
  if (front != os::mediaOwner() && !media::active() && !mediaStarting) {
    media::end(); library::end(); mediaTasksWanted = false; os::holdSecure(false);
    os::reconcile(os::Owner::Music); os::reconcile(os::Owner::Books);
  } else if (mediaTasksWanted && !os::secureHeld()) {
    if (!library::begin() || !media::begin()) os::recordFault(os::mediaOwner(), "no RAM to start");
    mediaTasksWanted = false;
  }
  char notice[64];
  if (os::takeFaultNotice(notice, sizeof(notice))) { homeNotice = notice; homeNoticeAt = millis(); renderDirty = true; }
  if (homeNotice.length() && millis() - homeNoticeAt > 10000) { homeNotice = ""; renderDirty = true; }
}

void serviceMedia() {
  serviceApps();
  if (radioOffWanted) {
    // Choosing the next chapter or book needs the same room as the last one had: chat stays out until he leaves.
    const bool inListeningApp = screenMode == ScreenMode::Library || screenMode == ScreenMode::LibrarySearch || screenMode == ScreenMode::NowPlaying;
    if (media::active() || mediaStarting || adjacentWanted || inListeningApp) lastCardMediaAt = millis();
    else if (millis() - lastCardMediaAt > kRadioBackAfterMs) radioOffWanted = false;
  }
  const bool listHadItems = !libraryItems.empty();
  if (library::takeItems(libraryItems)) {
    // A list growing (the server's albums joining the card's) keeps the selection in place.
    if (!listHadItems) librarySelection = 0;
    else if (librarySelection >= static_cast<int>(libraryItems.size())) librarySelection = 0;
    // A book reopens on the chapter holding its saved place.
    for (int index = 0; index < static_cast<int>(libraryItems.size()); index++) if (libraryItems[index].resumeMs) librarySelection = index;
    renderDirty = true;
  }
  static library::Status lastStatus = library::Status::Idle;
  if (library::status() != lastStatus) { lastStatus = library::status(); renderDirty = true; }
  library::Item adjacent;
  bool adjacentFound = false;
  if (adjacentWanted && library::takeAdjacentTrack(adjacent, adjacentFound)) {
    adjacentWanted = false;
    if (adjacentFound) {
      playQueue[queueIndex] = adjacent;
      startQueuedTrack(0); savePlaceAtStart();
    } else if (queueEnded && queueCollection == library::Collection::Books) {
      library::clearCardPlace(queueFolder);  // The last chapter ended: the book is finished.
    }
    if (!adjacentFound) mediaStarting = false;
    queueEnded = false;
    renderDirty = true;
  }
  const auto state = media::state();
  static media::State lastState = media::State::Idle;
  if (state != lastState) { lastState = state; renderDirty = true; }
  if (state == media::State::Ended && !adjacentWanted) {
    const auto *item = nowPlayingItem();
    if (item && item->onCard) { queueEnded = true; media::stop(); moveQueue(1); }
    else if (queueIndex + 1 < static_cast<int>(playQueue.size())) moveQueue(1);
    else {
      if (item && queueCollection == library::Collection::Books) {
        if (item->onCard) library::clearCardPlace(queueFolder); else library::deleteBookmark(item->id);
      }
      media::stop();
    }
    return;
  }
  if (state != media::State::Playing) return;
  if (screenMode == ScreenMode::NowPlaying && millis() - lastMediaRenderAt >= 1000) { lastMediaRenderAt = millis(); renderDirty = true; }
  const auto *item = nowPlayingItem();
  if (item && queueCollection == library::Collection::Books && millis() - lastBookmarkAt >= kBookmarkEveryMs) {
    lastBookmarkAt = millis(); savePlace();
  }
}

// Keys for HOME and its apps, plus the back key from chat and the menu.
bool handleAppKeys(const Keyboard_Class::KeysState &keys, bool goUp, bool goDown, bool goLeft, bool goRight) {
  const bool escape = physicalKeyAt(0, 0) && (screenMode == ScreenMode::Chat || screenMode == ScreenMode::LibrarySearch || screenMode == ScreenMode::NetworkPassword ? keys.fn : true);
  if (escape && screenMode == ScreenMode::KeyHelp) { screenMode = ScreenMode::Chat; playNextTone(); renderDirty = true; return true; }
  if (escape && (screenMode == ScreenMode::Chat || screenMode == ScreenMode::Menu)) { screenMode = ScreenMode::Home; playNextTone(); renderDirty = true; return true; }
  if (escape && isSettingsScreen()) { leaveSettingsScreen(); playNextTone(); renderDirty = true; return true; }
  const bool ownScreen = screenMode == ScreenMode::Home || screenMode == ScreenMode::Library || screenMode == ScreenMode::LibrarySearch ||
                         screenMode == ScreenMode::NowPlaying || screenMode == ScreenMode::Remote || screenMode == ScreenMode::System || isClockScreen();
  if (!ownScreen) return false;
  if (escape) { goBackFromApp(); playNextTone(); renderDirty = true; return true; }
  if (isClockScreen()) { if (handleClockKeys(keys, goUp, goDown, goLeft, goRight)) playNextTone(); renderDirty = true; return true; }
  if (screenMode == ScreenMode::Home) {
    const int page = homeSelection / kHomeAppsPerPage; const int slot = homeSelection % kHomeAppsPerPage;
    const bool onLeft = slot % 2 == 0;
    if (goUp || goDown) homeSelection = page * kHomeAppsPerPage + (slot + 2) % kHomeAppsPerPage;
    else if (goLeft && onLeft) {
      if (page == 0) { bumpHome(-1); return true; }
      homeSelection = (page - 1) * kHomeAppsPerPage + slot + 1;
    } else if (goRight && !onLeft) {
      if (page + 1 == kHomePageCount) { bumpHome(1); return true; }
      homeSelection = (page + 1) * kHomeAppsPerPage + slot - 1;
    } else if (goLeft || goRight) homeSelection += onLeft ? 1 : -1;
    else if (keys.enter) { openHomeApp(homeSelection); if (homeBumpAt) return true; }
    else {
      for (const auto character : keys.word) {
        if (character >= '1' && character < '1' + kHomeAppsPerPage) { homeSelection = page * kHomeAppsPerPage + (character - '1'); openHomeApp(homeSelection); if (homeBumpAt) return true; }
        else if (character == '0') screenMode = ScreenMode::System;
      }
    }
    playNextTone(); renderDirty = true; return true;
  }
  if (screenMode == ScreenMode::Remote || screenMode == ScreenMode::System) {
    if (goLeft || keys.enter) { screenMode = ScreenMode::Home; playNextTone(); renderDirty = true; }
    return true;
  }
  if (screenMode == ScreenMode::Library) {
    const int count = static_cast<int>(libraryItems.size());
    if (goUp && count) librarySelection = (librarySelection + count - 1) % count;
    else if (goDown && count) librarySelection = (librarySelection + 1) % count;
    else if (keys.enter) openLibrarySelection();
    else if (libraryView == LibraryView::Albums && goRight && count == library::kPageSize) {
      libraryOffset += library::kPageSize; libraryItems.clear(); library::requestAlbums(libraryCollection, libraryOffset);
    } else if (libraryView == LibraryView::Albums && goLeft && libraryOffset > 0) {
      libraryOffset = std::max(0, libraryOffset - library::kPageSize); libraryItems.clear(); library::requestAlbums(libraryCollection, libraryOffset);
    } else if (goLeft) goBackFromApp();
    playNextTone(); renderDirty = true; return true;
  }
  if (screenMode == ScreenMode::LibrarySearch) {
    if (keys.enter) {
      if (librarySearchText.length()) { openLibraryList(LibraryView::SearchResults, librarySearchText); library::requestSearch(librarySearchText, 0); }
    } else if (keys.del) removeLastUtf8Character(librarySearchText);
    else {
      if (keys.space) librarySearchText += ' ';
      for (const auto character : keys.word) if (character >= 0x20 && character <= 0x7e && librarySearchText.length() < 40) librarySearchText += character;
    }
    playNextTone(); renderDirty = true; return true;
  }
  // Now playing. Music keeps going after leaving this screen; S stops it.
  if (keys.enter) {
    media::togglePause();
    savePlace();
  } else if (goUp && volumeLevel < 4) { volumeLevel++; saveVolume(); }
  else if (goDown && volumeLevel > 0) { volumeLevel--; saveVolume(); }
  else if (goLeft || goRight) {
    if (queueCollection == library::Collection::Books) {
      const uint32_t position = media::positionSeconds();
      const uint32_t target = goRight ? position + kBookSkipSeconds : (position > kBookSkipSeconds ? position - kBookSkipSeconds : 0);
      stopMediaNow(); startQueuedTrack(target);
    } else moveQueue(goRight ? 1 : -1);
  } else {
    for (const auto character : keys.word) {
      if (character == 's' || character == 'S') { stopMediaNow(); goBackFromApp(); }
      else if ((character == 'l' || character == 'L') && queueIndex >= 0 && queueCollection == library::Collection::Music) {
        auto &item = playQueue[queueIndex];
        if (!item.onCard) { item.liked = !item.liked; library::setLiked(item.id, item.liked); }
      }
    }
  }
  renderDirty = true; return true;
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
  // A ringing alert takes every key, and none for its first second, so typing that was under way can't stop it.
  if (ringingNow() != Ringing::None) { if (clockapps::reached(millis(), ringKeysFrom)) dismissRinging(keys.enter); return; }
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
  const bool textEntryScreen = screenMode == ScreenMode::Chat || screenMode == ScreenMode::NetworkPassword || screenMode == ScreenMode::LibrarySearch;
  const bool otherModifier = keys.shift || keys.ctrl || keys.alt || keys.opt;
  // Text entry requires Fn to select the physical arrow layer. Navigation
  // screens treat those physical positions as arrows with or without Fn.
  const bool navigationChord = !otherModifier && (textEntryScreen ? keys.fn : !keys.fn);
  const bool goUp = navigationChord && navUp();
  const bool goDown = navigationChord && navDown();
  const bool goLeft = navigationChord && navLeft();
  const bool goRight = navigationChord && navRight();
  if (handleAppKeys(keys, goUp, goDown, goLeft, goRight)) return;
  if (screenMode == ScreenMode::Menu) {
    if (goUp) { menuSelection = (menuSelection + kMenuItemCount - 1) % kMenuItemCount; playNextTone(); }
    else if (goDown) { menuSelection = (menuSelection + 1) % kMenuItemCount; playNextTone(); }
    else if (goLeft) { screenMode = ScreenMode::Chat; playNextTone(); }
    else if (keys.enter) { openSelectedMenuItem(); playNextTone(); }
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::ChargingConfirm) {
    if (goLeft) { leaveSettingsScreen(); playNextTone(); }
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
    else if (goDown && roomSelection + 1 < roomCount()) roomSelection++;
    else if (goLeft) screenMode = ScreenMode::Menu;
    else if ((goRight || keys.enter) && roomCount() > 0) { selectRoom(roomSelection); screenMode=ScreenMode::Chat; }
    playNextTone(); renderDirty=true; return;
  }
  if (screenMode == ScreenMode::Volume) {
    if (goLeft && volumeLevel > 0) { volumeLevel--; saveVolume(); playNextTone(); }
    else if (goRight && volumeLevel < 4) { volumeLevel++; saveVolume(); playNextTone(); }
    else if (keys.enter) { leaveSettingsScreen(); playNextTone(); }
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
    else if (goLeft) { leaveSettingsScreen(); playNextTone(); }
    else if (goRight || keys.enter) { selectNetwork(); playNextTone(); }
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::NetworkPassword) {
    if (goLeft) { leaveSettingsScreen(); playNextTone(); renderDirty = true; return; }
    playNextTone();
    // A password types exactly what the keycaps show, in both languages: it is copied from a card.
    if (keys.enter) { joinSelectedNetwork(); return; }
    if (keys.del) removeLastUtf8Character(networkPassword);
    if (keys.space) appendKeyboardText(networkPassword, " ", 63, true);
    for (const auto character : keys.word)
      if (character >= 0x20 && character <= 0x7e) { const char text[] = {character, '\0'}; appendKeyboardText(networkPassword, text, 63, true); }
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::Language) {
    if (goLeft || goRight) {
      if (languageOverride == "auto") languageOverride = goRight ? "en" : "fr";
      else if (languageOverride == "en") languageOverride = goRight ? "fr" : "auto";
      else languageOverride = goRight ? "auto" : "en";
      saveLanguageOverride(); playNextTone();
    } else if (keys.enter) { leaveSettingsScreen(); playNextTone(); }
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::Status) {
    if (goLeft || goRight) { statusPage = (statusPage + (goRight ? 1 : 2)) % 3; playNextTone(); }
    else if (keys.enter) { leaveSettingsScreen(); playNextTone(); }
    renderDirty = true; return;
  }
  if (screenMode == ScreenMode::Changelog) {
    const auto &entry = kSupaChatChangelog[changelogSelection];
    if (goUp && changelogLineOffset > 0) { changelogLineOffset--; playNextTone(); }
    else if (goDown && changelogLineOffset + 4 < entry.lineCount) { changelogLineOffset++; playNextTone(); }
    else if (goLeft && changelogSelection > 0) { changelogSelection--; changelogLineOffset = 0; playNextTone(); }
    else if (goRight && changelogSelection + 1 < kSupaChatChangelogCount) { changelogSelection++; changelogLineOffset = 0; playNextTone(); }
    else if (keys.enter) { leaveSettingsScreen(); playNextTone(); }
    renderDirty = true; return;
  }
  // The French keys' page, opened by Tab in chat: arrows page through it, and every exit is back to chat.
  if (screenMode == ScreenMode::KeyHelp) {
    const int pages = keyHelpPageCount();
    if (goUp || goLeft) { if (keyHelpPage > 0) { keyHelpPage--; playNextTone(); } else playDeniedTone(); }
    else if (goDown || goRight) { if (keyHelpPage + 1 < pages) { keyHelpPage++; playNextTone(); } else playDeniedTone(); }
    else if (keys.enter || keys.tab) { screenMode = ScreenMode::Chat; playNextTone(); }
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
    const size_t maximum = messages.size();
    historyOffset = std::min(maximum, historyOffset + 1);
    xSemaphoreGive(stateMutex); playNextTone(); renderDirty = true; return;
  }
  if (goDown) { if (historyOffset > 0) historyOffset--; playNextTone(); renderDirty = true; return; }
  if (goLeft) { switchRoom(-1); playNextTone(); return; }
  if (goRight) { switchRoom(1); playNextTone(); return; }
  playNextTone();
  if (keys.enter) {
    if (draft.isEmpty() && historyOffset > 0) {
      xSemaphoreTake(stateMutex, portMAX_DELAY); const int selected=static_cast<int>(messages.size())-static_cast<int>(historyOffset);
      String retryId;
      if (selected >= 0 && messages[selected].authorId == kDeviceId && messages[selected].state == "failed") retryId = messages[selected].clientId;
      else if(selected>=0){replyToMessageId=messages[selected].id; replyToAuthor=messages[selected].authorName; replyToBody=messages[selected].body; historyOffset=0;}
      xSemaphoreGive(stateMutex);
      if (retryId.length()) { retryFailedMessage(retryId); historyOffset = 0; }
      renderDirty=true; return;
    }
    sendDraft(); return;
  }
  if (keys.del) {
    if (!frenchTypist.cancel()) removeLastUtf8Character(draft);
    renderDirty = true; return;
  }
  if (frenchUi && keys.tab) { screenMode = ScreenMode::KeyHelp; keyHelpPage = 0; renderDirty = true; return; }
  if (frenchUi) { typeFrenchKeys(keys, draft, kMessageLimit); renderDirty = true; return; }
  if (keys.space) appendKeyboardText(draft, " ", kMessageLimit, false);
  for (const auto character : keys.word)
    if (character >= 0x20 && character <= 0x7e) { const char text[] = {character, '\0'}; appendKeyboardText(draft, text, kMessageLimit, false); }
  renderDirty = true;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  sharedListsMutex = xSemaphoreCreateMutex();
  os::bindTask(os::Owner::Ui);
  auto config = M5.config(); config.output_power = true;
  M5Cardputer.begin(config, true); M5Cardputer.Display.setRotation(1); M5Cardputer.Display.setTextWrap(false);
  loadConfiguration();
#ifdef SUPACHAT_HOME_SSID
  // A network this device has never saved; every later boot finds it in NVS.
  if (std::none_of(wifiProfiles.begin(), wifiProfiles.end(), [](const WifiProfile &profile) { return profile.ssid == SUPACHAT_HOME_SSID; }))
    rememberWifi(SUPACHAT_HOME_SSID, SUPACHAT_HOME_PSK);
#endif
  seedBuildTime();
  M5Cardputer.Speaker.setVolume(kVolumeValues[volumeLevel]);
  sampleBattery(true);
  drawBootSplash();
  uiCanvas.setPsram(false); uiCanvas.setColorDepth(16);
  uiCanvasReady = uiCanvas.createSprite(240, 135) != nullptr; uiCanvas.setTextWrap(false);
  stateMutex = xSemaphoreCreateMutex(); bootNonce = esp_random();
  os::begin();
  // Cardputer ADV's explicit SD bus keeps GPIO11 free for the TCA8418
  // keyboard interrupt. Default ESP32-S3 SPI pins would disable all keys.
  SPI.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
  loadOutboxLocked();
  sdReady = SD.begin(kSdCsPin, SPI, 25000000); loadHistory();
  Serial.println("setup: card done");
  keyboardReady = M5Cardputer.In_I2C.scanID(0x34, 400000);
  Serial.println("setup: keyboard done");
#ifndef SUPACHAT_NO_RADIO
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false); WiFi.persistent(false); WiFi.disconnect(false, false);
#endif
  Serial.println("setup: wifi done");
  if (kEspNowEnabled) initializeEspNow();
  Serial.println("setup: esp-now done");
  xTaskCreatePinnedToCore(networkTask, "supachat-net", 16384, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(walkieTask, "supachat-ws", 12288, nullptr, 1, nullptr, 1);
  // Everything is up; the splash now waits for chat's first sync.
  Serial.printf("SUPACHAT ready node=%s wifi_profiles=%u sd=%d keyboard=%d board=%d\n", kDeviceId,
                wifiProfiles.size(), sdReady, keyboardReady, static_cast<int>(M5.getBoard()));
  showBootSplash();
  Serial.printf("BOOT %s at %u ms\n", os::bootText(os::boot()), millis());
  inputQuarantined = true;
  render();
}

static void loopBody();

void loop() {
  // The screen and keyboard live on the reserved floor, so this is a last resort. An out of
  // memory here drops to HOME, where the least is held.
  try {
    loopBody();
  } catch (const std::bad_alloc &) {
    if (stateMutex) xSemaphoreGive(stateMutex);
    os::recordFault(os::Owner::Ui, "out of memory, back to HOME");
    std::vector<library::Item>().swap(libraryItems);
    screenMode = ScreenMode::Home; renderDirty = true;
  }
}

static void loopBody() {
  M5Cardputer.update();
  if (M5Cardputer.BtnA.isPressed()) lastUserInputAt = millis();
  sampleBattery(); handleKeyboard(); serviceFoxFinding(); serviceClockApps();
  if (ringingNow() != Ringing::None && M5Cardputer.BtnA.wasClicked()) {
    if (clockapps::reached(millis(), ringKeysFrom)) dismissRinging(true);
    delay(2); return;
  }
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
  serviceClockRender(); captureVoice(); serviceAudioPlayback(); serviceMessageNotification(); serviceMedia();
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
    playNextTone();
    if (!goBackFromApp()) screenMode = screenMode == ScreenMode::Menu ? ScreenMode::Home : ScreenMode::Menu;
    renderDirty = true;
  }
  if (screenMode != ScreenMode::VoiceMessages && screenMode != ScreenMode::Walkie && spacePttHeld) { spacePttHeld = false; spaceReleaseStartedAt = 0; stopVoiceRecording(); }
  if (kEspNowEnabled && millis() - lastEspNowBeaconAt >= kEspNowBeaconMs) sendEspNowBeacon();
  if (homeBumpAt) { if (millis() - homeBumpAt >= kHomeBumpMs) homeBumpAt = 0; renderDirty = true; }
  if (screenMode == ScreenMode::Status && millis() - lastRenderAt >= 1000) renderDirty = true;
  if (screenMode == ScreenMode::Chat && chatNoticeAt && millis() - chatNoticeAt > 6000) { chatNoticeAt = 0; renderDirty = true; }
  serviceNearbySends();
  if (screenMode == ScreenMode::System && millis() - lastRenderAt >= 1000) renderDirty = true;
  if (renderDirty && millis() - lastRenderAt >= kRenderIntervalMs) render();
  delay(2);
}
