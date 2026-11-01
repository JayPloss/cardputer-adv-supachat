#pragma once

#include <Arduino.h>

// Plays MP3 from the microSD card or over plain LAN HTTP, decoding and feeding
// the speaker from its own task. There is no PSRAM on the Cardputer ADV, so every buffer here comes
// from internal RAM and is released as soon as playback stops.
namespace media {

enum class State : uint8_t { Idle, Connecting, Playing, Paused, Ended, Failed };

void begin();
// Starts url from the top; startSeconds only labels the position, because the
// server has already applied any time offset to the stream it sends.
void play(const String &url, uint32_t startSeconds);
// Plays a constant-bitrate file from the card, starting startSeconds in.
void playFile(const String &path, uint32_t startSeconds);
void togglePause();
void stop();

State state();
bool active();  // Connecting, Playing or Paused.
uint32_t positionSeconds();
uint32_t durationSeconds();  // 0 when unknown (streams).
const char *failure();
uint32_t lowestHeap();
uint32_t underruns();

}  // namespace media
