# Fonts on 240x135

Measured with `tools/font-specimen.py` on M5GFX as built for the firmware, 2026-11-04. Heights are counted from drawn pixels. Millimetres assume a 1.14 inch diagonal (about 0.105 mm a pixel), calculated from the panel's published size, not measured.

| Font | Capital height | At 30 cm | Characters per line | Lines under a header | French |
|---|---|---|---|---|---|
| Font0 x1 | 7 px, 0.74 mm | 8.4' | 39 | 11 | all, through CP437 |
| Font0 x1.5 | 10 px, 1.05 mm | 12.0' | 26 | 8 | all, uneven strokes |
| Font0 x2 | 14 px, 1.47 mm | 16.9' | 19 | 6 | all |
| Font0 x3 | 21 px, 2.21 mm | 25.3' | 13 | 4 | all |
| DejaVu12 | 9 px, 0.95 mm | 10.8' | 26 | 7 | none |
| DejaVu18 | 13 px, 1.37 mm | 15.7' | 19 | 5 | none |
| Font2 | 10 px, 1.05 mm | 12.0' | 33 | 6 | none |
| AsciiFont8x16 | 10 px, 1.05 mm | 12.0' | 29 | 6 | draws wrong letters |
| efont 12 | 7 px, 0.74 mm | 8.4' | 39 | 8 | lacks â ç ë î ï ô û É |
| efont 16 | 10 px, 1.05 mm | 12.0' | 29 | 6 | lacks â ç ë î ï ô û É |
| FreeSans9 | 13 px, 1.37 mm | 15.7' | 23 | 5 | none |

`'` is minutes of arc. The usual figure for a minimum capital height on displays is 16', with 20 to 22' preferred (ANSI/HFES 100, recalled and seen second-hand; the standard itself wasn't checked). Treat it as a yardstick. 30 cm is an assumed distance: at 20 cm each figure is half as large again, and children hold things closer. The judgement that counts is on the glass.

## What this means

- **Font0 is the only built-in font with all of French.** Its capital É is no taller than a lowercase letter.
- **Only Font0 x2 clears the 16' yardstick and also has French.** It gives 19 characters a line and 6 lines. That's the size for anything a person must read at a glance: choices, values, titles of what's playing.
- **Font0 x1 is half that size.** It holds 39 characters by 11 lines, and suits only what's glanced at rarely: version numbers, counters, diagnostics. Using it for a menu is a choice to pay for, not a default.
- **There's no good built-in middle size with French.** x1.5 is uneven, and the 10 px candidates all lack letters. A generated font with exactly the Latin letters needed, at 10 or 11 px capitals, would fill this gap at some flash cost. Nobody has built or tried one yet.

## Choosing

1. What is this text for: choosing, reading a value, reading a sentence, or diagnosing?
2. How many characters does the longest real value need, in both languages?
3. Can the screen show fewer things at a larger size? Paging is cheap here. Tiny type isn't.
4. Long free text (chat, song names): pick the size first, then scroll, wrap or cut with `utf8Head`. Never shrink it to fit.
