#pragma once
#include <stdint.h>

// Generated from the 53-note MIDI verse plus the complete 78-note Hooktheory chorus.
// Source MIDI format 0, 1 tracks, 480 ticks/beat.
constexpr uint16_t kKeypressSongFrequencies[] = {
    415, 370, 330, 415, 370, 370, 370, 330, 415, 494, 554, 440, 440, 415, 330, 415,
    415, 370, 370, 370, 277, 415, 370, 330, 330, 415, 494, 370, 370, 415, 330, 415,
    494, 370, 370, 415, 494, 554, 440, 440, 415, 415, 370, 330, 370, 330, 330, 415,
    494, 370, 370, 415, 330, 415, 494, 370, 370, 330, 330, 440, 415, 370, 370, 440,
    494, 554, 440, 440, 415, 415, 370, 330, 370, 330, 415, 494, 554, 415, 494, 659,
    494, 554, 494, 440, 494, 494, 440, 415, 370, 370, 494, 415, 415, 494, 440, 440,
    415, 370, 330, 330, 370, 415, 494, 415, 370, 659, 494, 554, 415, 415, 494, 494,
    440, 415, 370, 370, 494, 415, 415, 494, 440, 440, 415, 370, 330, 330, 370, 415,
    494, 415, 370
};
constexpr size_t kKeypressSongLength = sizeof(kKeypressSongFrequencies) / sizeof(kKeypressSongFrequencies[0]);
