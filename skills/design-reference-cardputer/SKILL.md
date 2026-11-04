---
name: design-reference-cardputer
description: Ground any Cardputer screen in this device's real limits and history before drawing it. Use when designing or changing a firmware screen, menu, label, font size, key behaviour or UI sound on the M5Stack Cardputer ADV. Use it in place of the general design-reference skill, whose sources are phones and web apps. Then prove the result with verify-cardputer-ui and the runtime emulator.
---

# Design reference: Cardputer

The screen is 240x135 on a 1.14 inch diagonal, held in the hand, and used by children and adults in two languages. Web and phone guidance doesn't transfer. Every choice here trades what fits against what can be read.

## Before drawing

1. Read [references/lessons.md](references/lessons.md): what has already gone wrong on this device.
2. Read [references/fonts.md](references/fonts.md), then pick the size from the job of the text, not from what fits.
3. Read [references/shipped.md](references/shipped.md) for what other Cardputer firmware does.
4. Write the longest real label in both languages against the space it gets, in pixels. English alone proves nothing: Emmanuelle, Naomie and Andrew run in French.

## Measure

```
cd ~/Work/cardputer-emulator && python3 -I tools/font-specimen.py <scratch folder>
```

It draws every candidate in `firmware/lab/font_samples.h` with the real M5GFX and writes `fonts.tsv` (letter heights counted from pixels, columns, lines, accents the font lacks) and `fonts.png` at 2x. Add a candidate to that list to measure it.

A screen on a PC monitor says nothing about readability on the glass. For that, the `font-lab` firmware environment shows the same drawings on the device, with Left and Right stepping through them. Readability is only settled on the glass, by the person holding it.

## After drawing

- Save screens from a runtime emulator scenario (`shot`), look at them at 1x and scaled by whole numbers only, and add `expect-no-clipping`. It catches text running off the right edge only: check overlap, the bottom edge and neighbouring boxes by eye.
- Change `emulator/state.mjs`, `emulator/ui.mjs` and `server/test/emulator-visual.mjs` too: the browser mock is a separate copy of the menus, and the upload gate tests it.
- Add a line to `firmware/include/changelog.h` for the release, and to `emulator/state.mjs`, which keeps a copy.
- Add new lessons to `references/lessons.md` in the same commit as the fix, with where each came from.
