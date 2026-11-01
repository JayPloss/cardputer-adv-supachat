#include "music_library.h"
#include "os.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_random.h>
#include <mbedtls/md5.h>

#include <algorithm>
#include <deque>
#include <new>

#if __has_include("music_secrets.h")
#include "music_secrets.h"
#endif
#ifndef SUPACHAT_MUSIC_HOST
#define SUPACHAT_MUSIC_HOST ""
#define SUPACHAT_MUSIC_USER ""
#define SUPACHAT_MUSIC_PASSWORD ""
#endif

namespace library {
namespace {
constexpr char kHost[] = SUPACHAT_MUSIC_HOST;
constexpr char kUser[] = SUPACHAT_MUSIC_USER;
constexpr char kPassword[] = SUPACHAT_MUSIC_PASSWORD;
constexpr char kBooksFolderName[] = "Books";
// Low enough that the server re-encodes, which is also what makes timeOffset work.
constexpr int kMusicKbps = 96;
constexpr int kBooksKbps = 48;
constexpr uint32_t kTimeoutMs = 4000;
// Away from home the server's address goes nowhere, and every wait for it would show on the screen.
constexpr uint32_t kConnectTimeoutMs = 2000;
constexpr uint32_t kServerRetryMs = 60000;
constexpr size_t kCardTrackLimit = 300;

enum class JobKind : uint8_t { CardAlbums, CardTracks, Adjacent, SavePlace, ClearPlace, Albums, Liked, Search, Tracks, SaveBookmark, DeleteBookmark, Like, Unlike };
struct Job { JobKind kind; String text; int number; Collection collection; String extra; uint32_t generation; };

SemaphoreHandle_t mutex = nullptr;
TaskHandle_t taskHandle = nullptr;
volatile bool endRequested = false;
std::deque<Job> jobs;
std::vector<Item> finished;
bool itemsFresh = false;
volatile uint32_t wantedGeneration = 0;
uint32_t runningGeneration = 0;
Item adjacentItem;
bool adjacentReady = false;
bool adjacentFound = false;
volatile Status currentStatus = Status::Idle;
const char *volatile failureText = "";
// getMusicFolders answers once; -1 means not looked up yet, 0 means no such library.
int musicFolderId = -1;
int booksFolderId = -1;

String urlEncode(const String &value) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    const uint8_t c = value[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

String apiUrl(const char *method) {
  char salt[13];
  snprintf(salt, sizeof(salt), "%08x%04x", static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random() & 0xffff));
  const String seed = String(kPassword) + salt;
  uint8_t digest[16];
  mbedtls_md5_ret(reinterpret_cast<const uint8_t *>(seed.c_str()), seed.length(), digest);
  char token[33];
  for (int i = 0; i < 16; i++) sprintf(token + i * 2, "%02x", digest[i]);
  return String("http://") + kHost + "/rest/" + method + "?u=" + urlEncode(kUser) + "&t=" + token + "&s=" + salt +
         "&v=1.16.1&c=cardputer&f=json";
}

uint32_t serverDownAt = 0;
bool serverWasDown = false;
bool serverKnownDown() { return serverWasDown && millis() - serverDownAt < kServerRetryMs; }

// What ArduinoJson reads the socket through. Stream's own timed read spins without yielding,
// and on a slow server that starves core 0's idle task until the watchdog restarts the device.
struct YieldingReader {
  WiFiClient &client;
  int read() {
    const uint32_t started = millis();
    while (millis() - started < kTimeoutMs) {
      const int value = client.read();
      if (value >= 0) return value;
      if (!client.connected()) return -1;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    return -1;
  }
  size_t readBytes(char *buffer, size_t length) {
    size_t count = 0;
    while (count < length) {
      const int value = read();
      if (value < 0) break;
      buffer[count++] = static_cast<char>(value);
    }
    return count;
  }
};

// Fetches a Subsonic call with only the filtered fields kept in RAM. HTTP/1.0
// keeps the server from chunking, so the JSON parser can read the socket directly.
bool fetch(const String &url, JsonDocument &filter, JsonDocument &document) {
  if (!configured()) { failureText = "SERVER NOT SET UP"; return false; }
  if (WiFi.status() != WL_CONNECTED) { failureText = "NO WIFI"; return false; }
  HTTPClient http;
  http.useHTTP10(true);
  if (serverKnownDown()) { failureText = "NO SERVER"; return false; }
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kTimeoutMs);
  http.begin(url);
  const int code = http.GET();
  if (code < 0) { serverDownAt = millis(); serverWasDown = true; }
  if (code != HTTP_CODE_OK) { failureText = code < 0 ? "NO SERVER" : "SERVER ERROR"; http.end(); return false; }
  serverWasDown = false;
  filter["subsonic-response"]["status"] = true;
  YieldingReader reader{http.getStream()};
  const DeserializationError error = deserializeJson(document, reader, DeserializationOption::Filter(filter));
  http.end();
  if (error) { failureText = "BAD REPLY"; return false; }
  if (strcmp(document["subsonic-response"]["status"] | "", "ok") != 0) { failureText = "LOGIN REFUSED"; return false; }
  return true;
}

void call(const String &url) {
  JsonDocument filter, document;
  fetch(url, filter, document);
}

void lookUpFolders() {
  if (musicFolderId >= 0) return;
  JsonDocument filter, document;
  filter["subsonic-response"]["musicFolders"]["musicFolder"][0]["id"] = true;
  filter["subsonic-response"]["musicFolders"]["musicFolder"][0]["name"] = true;
  if (!fetch(apiUrl("getMusicFolders"), filter, document)) return;
  int music = 0, books = 0;
  for (JsonObjectConst folder : document["subsonic-response"]["musicFolders"]["musicFolder"].as<JsonArrayConst>()) {
    if (strcmp(folder["name"] | "", kBooksFolderName) == 0) books = folder["id"] | 0;
    else if (!music) music = folder["id"] | 0;
  }
  musicFolderId = music;
  booksFolderId = books;
}

void publish(std::vector<Item> &&items) {
  if (runningGeneration != wantedGeneration) return;
  xSemaphoreTake(mutex, portMAX_DELAY);
  finished = std::move(items);
  itemsFresh = true;
  xSemaphoreGive(mutex);
  currentStatus = Status::Ready;
}

void addAlbum(std::vector<Item> &items, JsonObjectConst album) {
  Item item;
  item.id = String(album["id"] | "");
  item.title = String(album["name"] | "?");
  item.detail = String(album["artist"] | "");
  item.seconds = album["duration"] | 0;
  items.push_back(std::move(item));
}

void addTrack(std::vector<Item> &items, JsonObjectConst track) {
  Item item;
  item.id = String(track["id"] | "");
  item.title = String(track["title"] | "?");
  item.detail = String(track["artist"] | "");
  item.seconds = track["duration"] | 0;
  item.liked = !track["starred"].isNull();
  items.push_back(std::move(item));
}

const char *cardRoot(Collection collection) { return collection == Collection::Books ? "/books" : "/music"; }

String baseName(const String &path) { return path.substring(path.lastIndexOf('/') + 1); }

void addCardFolders(std::vector<Item> &items, Collection collection) {
  File root = SD.open(cardRoot(collection));
  if (!root || !root.isDirectory()) return;
  const size_t first = items.size();
  for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (entry.isDirectory()) {
      Item item;
      item.path = String(cardRoot(collection)) + "/" + baseName(entry.name());
      item.title = baseName(entry.name());
      items.push_back(std::move(item));
    }
    entry.close();
  }
  root.close();
  std::sort(items.begin() + first, items.end(), [](const Item &a, const Item &b) { return a.title < b.title; });
}

String placePath(const String &folder) { return folder + "/.place"; }

bool isMp3(const String &name) {
  return !name.startsWith(".") && (name.endsWith(".mp3") || name.endsWith(".MP3"));
}

// A place file ends in "ok", so one cut off by a power loss reads as no place, not a wrong one.
bool readPlace(const String &path, String &trackPath, uint32_t &seconds) {
  File place = SD.open(path, FILE_READ);
  if (!place) return false;
  trackPath = place.readStringUntil('\n');
  seconds = place.readStringUntil('\n').toInt();
  const String end = place.readStringUntil('\n');
  place.close();
  return end == "ok";
}

bool runCardTracks(const Job &job) {
  File root = SD.open(job.text);
  if (!root || !root.isDirectory()) { failureText = "NOT ON CARD"; return false; }
  std::vector<Item> items;
  items.reserve(100);
  for (File entry = root.openNextFile(); entry && items.size() < kCardTrackLimit; entry = root.openNextFile()) {
    const String name = baseName(entry.name());
    if (!entry.isDirectory() && isMp3(name)) {
      Item item;
      item.onCard = true;
      item.title = name.substring(0, name.length() - 4);
      items.push_back(std::move(item));
    }
    entry.close();
  }
  root.close();
  std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.title < b.title; });
  String trackPath;
  uint32_t seconds = 0;
  if (readPlace(placePath(job.text), trackPath, seconds) || readPlace(placePath(job.text) + ".new", trackPath, seconds)) {
    // A 1 ms floor still marks a saved place at the very start of a chapter.
    for (Item &item : items) if (job.text + "/" + item.title + ".mp3" == trackPath) item.resumeMs = std::max<uint32_t>(1, seconds * 1000);
  }
  publish(std::move(items));
  return true;
}

void runAdjacent(const Job &job) {
  File root = SD.open(job.text);
  String best;
  const bool forward = job.number > 0;
  if (root && root.isDirectory()) {
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
      const String name = baseName(entry.name());
      if (!entry.isDirectory() && isMp3(name)) {
        const String title = name.substring(0, name.length() - 4);
        const bool past = forward ? title > job.extra : title < job.extra;
        if (past && (best.isEmpty() || (forward ? title < best : title > best))) best = title;
      }
      entry.close();
    }
    root.close();
  }
  xSemaphoreTake(mutex, portMAX_DELAY);
  adjacentItem = Item();
  adjacentItem.onCard = true;
  adjacentItem.title = best;
  adjacentFound = best.length() > 0;
  adjacentReady = true;
  xSemaphoreGive(mutex);
}

void writeCardPlace(const Job &job) {
  // Written whole under a new name first: a power loss at any moment leaves one good copy.
  const String path = placePath(job.text);
  const String fresh = path + ".new";
  // A cut between an earlier save's remove and rename left the only good copy under the new name.
  if (!SD.exists(path) && SD.exists(fresh)) {
    String ignoredPath;
    uint32_t ignoredSeconds = 0;
    if (readPlace(fresh, ignoredPath, ignoredSeconds)) SD.rename(fresh, path);
  }
  File place = SD.open(fresh, FILE_WRITE);
  if (!place) return;
  size_t wrote = place.print(job.extra);
  wrote += place.print('\n'); wrote += place.print(job.number); wrote += place.print("\nok\n");
  const bool complete = wrote >= job.extra.length() + 5;
  place.close();
  if (!complete) { SD.remove(fresh); return; }  // The card is full: keep the old place.
  SD.remove(path);
  SD.rename(fresh, path);
}

bool runAlbums(const Job &job) {
  std::vector<Item> items;
  if (job.number == 0) addCardFolders(items, job.collection);
  const bool hasCardItems = !items.empty();
  // The card shows at once. The server's albums join the list if and when they arrive.
  if (hasCardItems) { std::vector<Item> first = items; publish(std::move(first)); }
  const char *const nothing = job.collection == Collection::Books ? "NO BOOKS ON CARD, NO SERVER" : "NO MUSIC ON CARD, NO SERVER";
  if (!configured() || WiFi.status() != WL_CONNECTED || serverKnownDown()) {
    failureText = nothing;
    return hasCardItems;
  }
  lookUpFolders();
  const int folder = job.collection == Collection::Books ? booksFolderId : musicFolderId;
  if (musicFolderId < 0 || (job.collection == Collection::Books && folder == 0)) {
    failureText = nothing;
    return hasCardItems;
  }
  JsonDocument filter, document;
  JsonObject album = filter["subsonic-response"]["albumList2"]["album"][0].to<JsonObject>();
  album["id"] = true; album["name"] = true; album["artist"] = true; album["duration"] = true;
  String url = apiUrl("getAlbumList2") + "&type=alphabeticalByName&size=" + kPageSize + "&offset=" + job.number;
  if (folder) url += String("&musicFolderId=") + folder;
  if (!fetch(url, filter, document)) {
    failureText = nothing;
    return hasCardItems;
  }
  for (JsonObjectConst value : document["subsonic-response"]["albumList2"]["album"].as<JsonArrayConst>()) addAlbum(items, value);
  publish(std::move(items));
  return true;
}

bool runLiked() {
  JsonDocument filter, document;
  JsonObject song = filter["subsonic-response"]["starred2"]["song"][0].to<JsonObject>();
  song["id"] = true; song["title"] = true; song["artist"] = true; song["duration"] = true; song["starred"] = true;
  if (!fetch(apiUrl("getStarred2"), filter, document)) return false;
  std::vector<Item> items;
  for (JsonObjectConst value : document["subsonic-response"]["starred2"]["song"].as<JsonArrayConst>()) addTrack(items, value);
  publish(std::move(items));
  return true;
}

bool runSearch(const Job &job) {
  JsonDocument filter, document;
  JsonObject song = filter["subsonic-response"]["searchResult3"]["song"][0].to<JsonObject>();
  song["id"] = true; song["title"] = true; song["artist"] = true; song["duration"] = true; song["starred"] = true;
  String url = apiUrl("search3") + "&query=" + urlEncode(job.text) + "&artistCount=0&albumCount=0&songCount=" + kPageSize + "&songOffset=" + job.number;
  if (!fetch(url, filter, document)) return false;
  std::vector<Item> items;
  for (JsonObjectConst value : document["subsonic-response"]["searchResult3"]["song"].as<JsonArrayConst>()) addTrack(items, value);
  publish(std::move(items));
  return true;
}

bool runTracks(const Job &job) {
  JsonDocument filter, document;
  JsonObject song = filter["subsonic-response"]["album"]["song"][0].to<JsonObject>();
  song["id"] = true; song["title"] = true; song["artist"] = true; song["duration"] = true; song["starred"] = true;
  if (!fetch(apiUrl("getAlbum") + "&id=" + urlEncode(job.text), filter, document)) return false;
  std::vector<Item> items;
  for (JsonObjectConst value : document["subsonic-response"]["album"]["song"].as<JsonArrayConst>()) addTrack(items, value);

  JsonDocument marksFilter, marks;
  JsonObject mark = marksFilter["subsonic-response"]["bookmarks"]["bookmark"][0].to<JsonObject>();
  mark["position"] = true; mark["entry"]["id"] = true;
  if (fetch(apiUrl("getBookmarks"), marksFilter, marks)) {
    for (JsonObjectConst value : marks["subsonic-response"]["bookmarks"]["bookmark"].as<JsonArrayConst>()) {
      const char *trackId = value["entry"]["id"] | "";
      for (Item &item : items) if (item.id == trackId) item.resumeMs = value["position"] | 0;
    }
  }
  publish(std::move(items));
  return true;
}

void workerTask(void *) {
  for (;;) {
    if (endRequested) { endRequested = false; taskHandle = nullptr; vTaskDelete(nullptr); }
    xSemaphoreTake(mutex, portMAX_DELAY);
    const bool empty = jobs.empty();
    Job job{};
    if (!empty) { job = jobs.front(); jobs.pop_front(); }
    xSemaphoreGive(mutex);
    if (empty) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    bool ok = true;
    runningGeneration = job.generation;
    // Running out of RAM here shows on the screen instead of restarting the device.
    try {
    switch (job.kind) {
      case JobKind::CardAlbums: {
        std::vector<Item> items;
        addCardFolders(items, job.collection);
        publish(std::move(items));
        break;
      }
      case JobKind::CardTracks: ok = runCardTracks(job); break;
      case JobKind::Adjacent: runAdjacent(job); break;
      case JobKind::SavePlace: writeCardPlace(job); break;
      case JobKind::ClearPlace: SD.remove(placePath(job.text)); SD.remove(placePath(job.text) + ".new"); break;
      case JobKind::Albums: ok = runAlbums(job); break;
      case JobKind::Liked: ok = runLiked(); break;
      case JobKind::Search: ok = runSearch(job); break;
      case JobKind::Tracks: ok = runTracks(job); break;
      case JobKind::SaveBookmark: call(apiUrl("createBookmark") + "&id=" + urlEncode(job.text) + "&position=" + job.number); break;
      case JobKind::DeleteBookmark: call(apiUrl("deleteBookmark") + "&id=" + urlEncode(job.text)); break;
      case JobKind::Like: call(apiUrl("star") + "&id=" + urlEncode(job.text)); break;
      case JobKind::Unlike: call(apiUrl("unstar") + "&id=" + urlEncode(job.text)); break;
    }
    } catch (const std::bad_alloc &) {
      failureText = "OUT OF MEMORY";
      os::recordFault(os::mediaOwner(), "list out of memory");
      ok = false;
    }
    if (!ok && runningGeneration == wantedGeneration) currentStatus = Status::Failed;
  }
}

void enqueue(Job job, bool loadsList) {
  if (!mutex) mutex = xSemaphoreCreateMutex();
  if (loadsList) { wantedGeneration = wantedGeneration + 1; currentStatus = Status::Loading; }
  job.generation = wantedGeneration;
  xSemaphoreTake(mutex, portMAX_DELAY);
  jobs.push_back(std::move(job));
  xSemaphoreGive(mutex);
}
}  // namespace

bool begin() {
  // Wanted again before it had ended: it just carries on.
  if (taskHandle) { endRequested = false; return true; }
  if (!mutex) mutex = xSemaphoreCreateMutex();
  return xTaskCreatePinnedToCore(workerTask, "library", 12288, nullptr, 1, &taskHandle, 0) == pdPASS;
}

void end() { if (taskHandle) endRequested = true; }

bool configured() { return kHost[0] != 0; }

void requestAlbums(Collection collection, int offset) { enqueue({JobKind::Albums, "", offset, collection}, true); }
void requestCardAlbums(Collection collection) { enqueue({JobKind::CardAlbums, "", 0, collection}, true); }
void requestCardTracks(const String &folder) { enqueue({JobKind::CardTracks, folder, 0, Collection::Books}, true); }
void saveCardPlace(const String &folder, const String &trackPath, uint32_t seconds) { enqueue({JobKind::SavePlace, folder, static_cast<int>(seconds), Collection::Books, trackPath}, false); }
void clearCardPlace(const String &folder) { enqueue({JobKind::ClearPlace, folder, 0, Collection::Books}, false); }
void requestAdjacentTrack(const String &folder, const String &title, int direction) { enqueue({JobKind::Adjacent, folder, direction, Collection::Books, title}, false); }
bool takeAdjacentTrack(Item &item, bool &found) {
  if (!mutex) return false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  const bool ready = adjacentReady;
  if (ready) { item = adjacentItem; found = adjacentFound; adjacentReady = false; }
  xSemaphoreGive(mutex);
  return ready;
}
void requestLiked() { enqueue({JobKind::Liked, "", 0, Collection::Music}, true); }
void requestSearch(const String &query, int offset) { enqueue({JobKind::Search, query, offset, Collection::Music}, true); }
void requestTracks(const String &albumId) { enqueue({JobKind::Tracks, albumId, 0, Collection::Music}, true); }
void saveBookmark(const String &trackId, uint32_t positionMs) { enqueue({JobKind::SaveBookmark, trackId, static_cast<int>(positionMs), Collection::Books}, false); }
void deleteBookmark(const String &trackId) { enqueue({JobKind::DeleteBookmark, trackId, 0, Collection::Books}, false); }
void setLiked(const String &trackId, bool liked) { enqueue({liked ? JobKind::Like : JobKind::Unlike, trackId, 0, Collection::Music}, false); }

void forget() { wantedGeneration = wantedGeneration + 1; if (currentStatus == Status::Loading) currentStatus = Status::Idle; }

Status status() { return currentStatus; }
const char *failure() { return failureText; }

bool takeItems(std::vector<Item> &items) {
  if (!mutex) return false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  const bool fresh = itemsFresh;
  if (fresh) { items = std::move(finished); finished.clear(); itemsFresh = false; }
  xSemaphoreGive(mutex);
  return fresh;
}

String streamUrl(const String &trackId, Collection collection, uint32_t offsetSeconds) {
  String url = apiUrl("stream") + "&id=" + urlEncode(trackId) + "&format=mp3&maxBitRate=" + (collection == Collection::Books ? kBooksKbps : kMusicKbps);
  if (offsetSeconds) url += String("&timeOffset=") + offsetSeconds;
  return url;
}

}  // namespace library
