#pragma once

#include <Arduino.h>

#include <vector>

// Lists what can be played: folders on the microSD card first, then the home
// music server (Navidrome, through the Subsonic REST API). All of it runs on this
// module's own task, so the UI never waits on the card or the network.
namespace library {

struct Item {
  String id;
  String path;  // A folder on the card, for albums and books.
  bool onCard = false;  // A track named title + ".mp3" in the list's folder.
  String title;
  String detail;
  uint32_t seconds = 0;
  uint32_t resumeMs = 0;
  bool liked = false;
};

enum class Collection : uint8_t { Music, Books };
enum class Status : uint8_t { Idle, Loading, Ready, Failed };

constexpr int kPageSize = 20;

// Start and stop the library's task, as with the player.
bool begin();
void end();
bool configured();

// Offset 0 lists the card's folders ahead of the server's albums.
void requestAlbums(Collection collection, int offset);
// Only what is on the card.
void requestCardAlbums(Collection collection);
void requestCardTracks(const String &folder);
// Where a book on the card was left; each book folder keeps one place.
void saveCardPlace(const String &folder, const String &trackPath, uint32_t seconds);
void clearCardPlace(const String &folder);
// The chapter after (or before) one on the card, found without listing the folder into RAM.
void requestAdjacentTrack(const String &folder, const String &title, int direction);
// True once the answer is in; found says whether there was one.
bool takeAdjacentTrack(Item &item, bool &found);
void requestLiked();
void requestSearch(const String &query, int offset);
// Tracks of one album, with any saved place marked in resumeMs.
void requestTracks(const String &albumId);
void saveBookmark(const String &trackId, uint32_t positionMs);
void deleteBookmark(const String &trackId);
void setLiked(const String &trackId, bool liked);

// A list still being fetched is no longer wanted: its answer is dropped, not shown.
void forget();

Status status();
const char *failure();
// Copies the finished list out for the screen; false until there is a new one.
bool takeItems(std::vector<Item> &items);

String streamUrl(const String &trackId, Collection collection, uint32_t offsetSeconds);

}  // namespace library
