#pragma once

struct SupaChatChangelogEntry {
  const char *version;
  const char *lines[4];
};

// Canonical on-device release notes. The emulator reads this file directly so
// its Changelog screen cannot silently drift from the firmware.
constexpr SupaChatChangelogEntry kSupaChatChangelog[] = {
  {"v0.47", {"Paged menus", "Build changelog", "ESP-NOW local only", "Language menu fix"}},
  {"v0.46", {"Groups own rooms", "EN/FR preferences", "Automatic group language", "QR group invites"}},
  {"v0.45", {"Contextual arrows", "Correct French glyphs", "Fallback WiFi profiles", "Legacy mesh key fix"}},
  {"v0.44", {"French accents", "Complete key chorus", "Original startup song", "Bilingual web UI"}},
  {"v0.43", {"History + notices", "Sender names + colours", "ESP-NOW fallback", "Complete startup melody"}},
  {"v0.42", {"Wolfpack terminals", "Emma, Naomie, Andrew", "Voice + local replay", "Startup audio fixes"}},
};
constexpr int kSupaChatChangelogCount = sizeof(kSupaChatChangelog) / sizeof(kSupaChatChangelog[0]);
