# What other Cardputer firmware does

Read from source on 2026-11-04. Add to this only from source or screenshots actually read, never from memory.

## Bruce (`pr3y/Bruce`)

- `boards/m5stack-cardputer/m5stack-cardputer.ini` defines its three sizes on this exact screen: `FP=1`, `FM=2`, `FG=3`, on a 6x8 base font. `FP` means small, `FM` medium, `FG` large.
- So its normal size is what we call `Font0` x2. In `src/core/display.cpp`, a message tries `FM` first and falls back to `FP` only when it doesn't fit, and option menus are sized from `FM`.

## Launcher (`bmorcelli/Launcher`)

- Its notes put binary size first, since every board flashes into a fixed partition. This is a reminder that added fonts cost flash, though our 3 MB app partition is at 46%.

Not read yet: the main menu code of either project, NEMO, and screenshots of any of them. Reading these is the next step in building out this reference.
