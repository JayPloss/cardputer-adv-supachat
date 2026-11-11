#include "music_library.h"
#include "os.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_random.h>
#include <mbedtls/md5.h>

#include <algorithm>
#include <cerrno>
#include <dirent.h>
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
  item.id = album["id"] | "";
  item.title = album["name"] | "?";
  item.detail = album["artist"] | "";
  item.seconds = album["duration"] | 0;
  items.push_back(std::move(item));
}

void addTrack(std::vector<Item> &items, JsonObjectConst track) {
  Item item;
  item.id = track["id"] | "";
  item.title = track["title"] | "?";
  item.detail = track["artist"] | "";
  item.seconds = track["duration"] | 0;
  item.liked = !track["starred"].isNull();
  items.push_back(std::move(item));
}

const char *cardRoot(Collection collection) { return collection == Collection::Books ? "/books" : "/music"; }

bool isMp3(const char *name) {
  const size_t length = strlen(name);
  return name[0] != '.' && length > 4 && (!strcmp(name + length - 4, ".mp3") || !strcmp(name + length - 4, ".MP3"));
}

// Reading a card folder. The core's openNextFile opens every entry it passes (a stat, then an opendir or a
// file with a 4 KB buffer), and when one of those allocations is refused (this app's budget, or the floor
// while Wi-Fi scans away from home) it returns an empty File, which ends the loop as if the folder had
// ended: a short list, no error. So the folder is read with readdir, which allocates nothing per entry
// (FAT's long names are on the stack: CONFIG_FATFS_LFN_STACK), each name's String is checked, and a list
// counts only when two passes in a row agree.
// Strings are only ever assigned from char pointers here: the core's String::move copies a byte out of a
// String whose allocation failed, from address 0, and the task crashes (WString.cpp, move()).
constexpr int kListPasses = 6;
constexpr uint32_t kListRetryMs = 300;
constexpr char kCardMount[] = "/sd";  // where SD.begin mounts the card

struct Pass {
  bool read = true;  // false: the card or the RAM failed part way
  size_t count = 0;
  uint32_t hash = 2166136261u;
};

// Sets `text` to `first` (its first `length` bytes), plus "/" and `second` when given. A refused
// allocation is tried again a few times: a dip in the heap passes in a moment.
bool assign(String &text, const char *first, size_t length, const char *second = nullptr) {
  const size_t want = length + (second ? 1 + strlen(second) : 0);
  for (int attempt = 0; attempt < 5; attempt++) {
    text = "";
    if (text.reserve(want) && text.concat(first, length) && (!second || (text.concat('/') && text.concat(second))) && text.length() == want) return true;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return false;
}

// One pass over `path`, keeping folders (or MP3s). With `items`, it also builds the list.
Pass readFolder(const String &path, bool folders, size_t limit, std::vector<Item> *items) {
  Pass pass;
  char where[320];
  snprintf(where, sizeof where, "%s%s", kCardMount, path.c_str());
  DIR *dir = opendir(where);
  if (!dir) { pass.read = !SD.exists(path); return pass; }  // There but not opened: no room for it.
  for (;;) {
    errno = 0;
    const dirent *entry = readdir(dir);
    if (!entry) { if (errno) pass.read = false; break; }
    const char *name = entry->d_name;
    const bool isDirectory = entry->d_type == DT_DIR;
    if (!isDirectory && entry->d_type != DT_REG) continue;
    if (folders ? !isDirectory : (isDirectory || !isMp3(name))) continue;
    if (folders && name[0] == '.') continue;
    if (pass.count >= limit) break;
    pass.count++;
    for (const char *c = name; *c; c++) pass.hash = (pass.hash ^ static_cast<uint8_t>(*c)) * 16777619u;
    pass.hash = (pass.hash ^ '/') * 16777619u;
    if (!items) continue;
    const size_t titleLength = strlen(name) - (folders ? 0 : 4);
    Item item;
    item.onCard = !folders;
    if (!assign(item.title, name, titleLength) || (folders && !assign(item.path, path.c_str(), path.length(), name)) || items->size() >= items->capacity()) {
      // This pass's list is lost, but it reads on: its count sizes the next one.
      pass.read = false;
      items = nullptr;
      continue;
    }
    items->push_back(std::move(item));
  }
  closedir(dir);
  return pass;
}

// The whole list: counted, then built, until two passes agree. False if they never did.
bool readFolderList(const String &path, bool folders, size_t limit, std::vector<Item> &items) {
  const size_t first = items.size();
  Pass last = readFolder(path, folders, limit, nullptr);
  for (int attempt = 0; attempt < kListPasses; attempt++) {
    items.resize(first);
    try {
      items.reserve(first + last.count + 1);
    } catch (const std::bad_alloc &) {
      vTaskDelay(pdMS_TO_TICKS(kListRetryMs));
      continue;
    }
    const Pass pass = readFolder(path, folders, limit, &items);
    if (pass.read && last.read && pass.count == last.count && pass.hash == last.hash) {
      std::sort(items.begin() + first, items.end(), [](const Item &a, const Item &b) { return a.title < b.title; });
      return true;
    }
    last = pass;
    vTaskDelay(pdMS_TO_TICKS(kListRetryMs));
  }
  return false;
}

constexpr const char *kCardNotRead = "CARD NOT READ: LOW RAM";

// False when the card's list couldn't be read whole; nothing is added then, rather than part of it.
bool addCardFolders(std::vector<Item> &items, Collection collection) {
  const size_t first = items.size();
  if (readFolderList(cardRoot(collection), true, SIZE_MAX, items)) return true;
  items.resize(first);
  return false;
}

String placePath(const String &folder) { return folder + "/.place"; }

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
  std::vector<Item> items;
  if (!readFolderList(job.text, false, kCardTrackLimit, items)) {
    failureText = SD.exists(job.text) ? kCardNotRead : "NOT ON CARD";
    return false;
  }
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
  String best;
  const bool forward = job.number > 0;
  char where[320];
  snprintf(where, sizeof where, "%s%s", kCardMount, job.text.c_str());
  if (DIR *dir = opendir(where)) {
    while (const dirent *entry = readdir(dir)) {
      if (entry->d_type != DT_REG || !isMp3(entry->d_name)) continue;
      String title = entry->d_name;
      if (title.length() != strlen(entry->d_name)) continue;
      title.remove(title.length() - 4);
      const bool past = forward ? title > job.extra : title < job.extra;
      if (past && (best.isEmpty() || (forward ? title < best : title > best))) best = title.c_str();
    }
    closedir(dir);
  }
  xSemaphoreTake(mutex, portMAX_DELAY);
  adjacentItem = Item();
  adjacentItem.onCard = true;
  adjacentItem.title = best.c_str();
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
  const bool cardRead = job.number != 0 || addCardFolders(items, job.collection);
  const bool hasCardItems = !items.empty();
  const char *const nothing = !cardRead ? kCardNotRead : job.collection == Collection::Books ? "NO BOOKS ON CARD, NO SERVER" : "NO MUSIC ON CARD, NO SERVER";
  if (!configured() || WiFi.status() != WL_CONNECTED || serverKnownDown()) {
    failureText = nothing;
    if (hasCardItems) publish(std::move(items));
    return hasCardItems;
  }
  // The card shows at once. The server's albums join the list if and when they arrive. The copy is
  // checked: a String that couldn't be copied comes out empty, and a list with a blank book isn't shown.
  if (hasCardItems) {
    std::vector<Item> first;
    bool whole = true;
    try { first = items; } catch (const std::bad_alloc &) { whole = false; }
    for (size_t i = 0; whole && i < items.size(); i++) whole = first[i].title.length() == items[i].title.length() && first[i].path.length() == items[i].path.length();
    if (whole) publish(std::move(first));
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
  os::bindTask(os::Owner::Music);
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
        if (addCardFolders(items, job.collection)) publish(std::move(items));
        else { failureText = kCardNotRead; ok = false; }
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

void forget() { wantedGeneration = wantedGeneration + 1; if (currentStatus == Status::Loading || currentStatus == Status::Failed) currentStatus = Status::Idle; }

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
