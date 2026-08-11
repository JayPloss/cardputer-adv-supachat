#pragma once
#include <stdint.h>

// Generated from keypress-song.mid; MIDI format 0, 1 tracks, 480 ticks/beat.
constexpr uint16_t kKeypressSongFrequencies[] = {
    415, 370, 330, 415, 370, 370, 370, 330, 415, 494, 554, 440, 440, 415, 330, 415,
    415, 370, 370, 370, 277, 415, 370, 330, 330, 415, 494, 370, 370, 415, 330, 415,
    494, 370, 370, 415, 494, 554, 440, 440, 415, 415, 370, 330, 370, 330, 330, 415,
    494, 370, 370, 415, 330, 415, 494, 370, 370, 330, 330, 440, 415, 370, 370, 415,
    494, 554, 440, 440, 415, 370, 370, 330, 370, 330, 415, 494, 494, 494, 415, 659,
    494, 554, 494, 415, 494
};
constexpr size_t kKeypressSongLength = sizeof(kKeypressSongFrequencies) / sizeof(kKeypressSongFrequencies[0]);
