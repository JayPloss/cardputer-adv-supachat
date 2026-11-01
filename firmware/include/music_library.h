#pragma once

#include <Arduino.h>

#include <vector>

// Talks to the home music server (Navidrome) through the Subsonic REST API. All
// network calls run on this module's own task, so the UI never waits on them.
namespace library {

struct Item {
  String id;
  String title;
  String detail;
  uint32_t seconds = 0;
  uint32_t resumeMs = 0;
  bool liked = false;
};

enum class Collection : uint8_t { Music, Books };
enum class Status : uint8_t { Idle, Loading, Ready, Failed };

constexpr int kPageSize = 20;

void begin();
bool configured();

void requestAlbums(Collection collection, int offset);
void requestLiked();
void requestSearch(const String &query, int offset);
// Tracks of one album, with any saved place marked in resumeMs.
void requestTracks(const String &albumId);
void saveBookmark(const String &trackId, uint32_t positionMs);
void deleteBookmark(const String &trackId);
void setLiked(const String &trackId, bool liked);

Status status();
const char *failure();
// Copies the finished list out for the screen; false until there is a new one.
bool takeItems(std::vector<Item> &items);

String streamUrl(const String &trackId, Collection collection, uint32_t offsetSeconds);

}  // namespace library
