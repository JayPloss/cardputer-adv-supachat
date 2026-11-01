#pragma once

#include <Arduino.h>

// Streams MP3 over plain LAN HTTP, decodes it and feeds the speaker from its
// own task. There is no PSRAM on the Cardputer ADV, so every buffer here comes
// from internal RAM and is released as soon as playback stops.
namespace media {

enum class State : uint8_t { Idle, Connecting, Playing, Paused, Ended, Failed };

void begin();
// Starts url from the top; startSeconds only labels the position, because the
// server has already applied any time offset to the stream it sends.
void play(const String &url, uint32_t startSeconds);
void togglePause();
void stop();

State state();
bool active();  // Connecting, Playing or Paused.
uint32_t positionSeconds();
const char *failure();
uint32_t lowestHeap();
uint32_t underruns();

}  // namespace media
