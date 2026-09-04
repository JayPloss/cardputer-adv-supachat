#pragma once

struct SupaChatChangelogEntry {
  const char *version;
  const char *lines[8];
  int lineCount;
};

// Canonical on-device release notes. The emulator reads this file directly so
// its Changelog screen cannot silently drift from the firmware.
constexpr SupaChatChangelogEntry kSupaChatChangelog[] = {
  {"v0.49", {"Low-power charging mode", "Charge history plot", "Battery event logging", "Boot key quarantine", "Verbose sync diagnostics"}, 5},
  {"v0.48", {"Scrollable build changes", "Working language controls", "Reliable room switching", "Zero-copy history sync", "Fixed cable detection", "Clean battery digits", "Voice / walkie split"}, 7},
  {"v0.47", {"Paged menus", "Build changelog", "ESP-NOW local only", "Language menu fix"}, 4},
  {"v0.46", {"Groups own rooms", "EN/FR preferences", "Automatic group language", "QR group invites"}, 4},
  {"v0.45", {"Contextual arrows", "Correct French glyphs", "Fallback WiFi profiles", "Legacy mesh key fix"}, 4},
  {"v0.44", {"French accents", "Complete key chorus", "Original startup song", "Bilingual web UI"}, 4},
  {"v0.43", {"History + notices", "Sender names + colours", "ESP-NOW fallback", "Complete startup melody"}, 4},
  {"v0.42", {"Wolfpack terminals", "Emma, Naomie, Andrew", "Voice + local replay", "Startup audio fixes"}, 4},
};
constexpr int kSupaChatChangelogCount = sizeof(kSupaChatChangelog) / sizeof(kSupaChatChangelog[0]);
