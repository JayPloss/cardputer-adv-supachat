# What this device has already taught us

Each item says where it came from. Add to this list; don't delete from it unless the code that made it true is gone.

## Keys

- **The arrows are not arrow keys.** They share `;` `.` `,` `/`. On a text entry screen they type punctuation and need Fn to navigate; on a navigation screen they navigate with or without Fn. This was wrong twice (commits "Fix contextual arrows and French glyph rendering" and "fix physical menu navigation"). Every new screen must be listed correctly in `textEntryScreen` in `main.cpp`.
- **The key that dismisses the splash must not reach the next screen.** Boot and charging mode have an input quarantine (`docs/firmware-fixes-backlog.md`, item 1). A new full-screen transition that consumes a held key needs the same thought.
- **Back has two routes.** The rear button and the top-left key both go back one level (Fn + top-left while typing). A screen reachable from two places must go back to where it came from.

## Sound

- **Feedback tones follow the volume setting, and were painfully loud against speech.** Jay, 2026-11-01: "LOUD bloops and quiet talking are bad for ears." In the listening apps tones now sit at `kListeningToneVolume`. Any new sound goes through the same volume path as `playNextTone()`, and stays silent during recording, playback and charging mode, as that does.

## Text

- **`Font2` has no accents.** French came out wrong until chat moved to `Font0` (commit "Fix contextual arrows and French glyph rendering"). Check any new font with the specimen: an ASCII font can report that it has a letter and still draw the wrong one.
- **`Font0` holds CP437, not UTF-8.** Text that can hold accents must go through `printFont0Text`. A plain `print` draws garbage for anything people typed or names from the server.
- **Cut text by character, not by byte.** `utf8Head` and `utf8Tail` exist because `substring` can split an accent in half.
- **Non-integer scales are uneven.** `Font0` x1.5 (chat) gives letters with strokes of different widths, and `ç` is hard to read at that size. See `fonts.md`.
- **Passwords are shown in full on purpose** (`verify-cardputer-ui/references/screen-budget.md`): a tiny keyboard makes blind typing useless.

## Layout

- Header y=0..19; content from y=20; chat composer from y=106; footer text at or above y=123.
- **Paged menus show a page indicator and keep each page's own last selection** (README, "Paginated menus").
- **The 2x2 Home grid at `Font0` x2 tested well on the device.** Jay, 2026-11-01: "okay, menu worked well." A tile is 111 px wide, so a label is at most 9 characters at x2. Plan shorter French words, not smaller type.
- **Most secondary screens are still at `Font0` x1**, the smallest thing that can be drawn. That's history, not a decision to copy.

## Memory and time

- **No PSRAM.** Lists and strings on a screen cost real heap beside the player: a 200-song list ran out of memory, and opening a card folder now stops playback for that reason (commit "whole library on the card").
- **Keep drawing off the key path.** Rendering is throttled to about 20 fps so typing and tones stay responsive (README). A long flourish must be stepped by time in the loop, never with `delay()`.
