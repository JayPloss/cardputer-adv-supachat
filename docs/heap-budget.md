# Heap budget

From the runtime emulator (`cardputer-emulator`), with its stated estimates. It isn't measured on a device yet: STATUS or the SYSTEM screen on a real Cardputer gives the free heap to compare.

| What | Bytes | Source |
|---|---|---|
| Device heap | 304,040 | `_heap_start` in the ELF to the end of SRAM, plus 32 KB region |
| System: Wi-Fi driver, lwIP, system tasks, speaker, FATFS | 93,800 | estimates plus 14 KB calibrated against two real crashes |
| Screen canvas | 64,800 | 240 x 135 x 16 bit |
| Task stacks always there: chat 16 K, walkie 12 K, loop 8 K | 36,000 | the code |
| Chat at rest, 40 messages in RAM | 15,000 | scenario `boot-online` |
| **Free at rest on HOME** | **99,000** | scenario `boot-online` |
| One secure connection at its peak | 71,000 | socket 2 K, two 16 K records, context 3 K, traffic 8 K, handshake 24 K |
| Media tasks' stacks, while a media app is open | 20,000 | the code |
| Player on the card: decoder 24 K, input 6 K, PCM 14 K | 44,000 | scenario `card-book` |

What follows from those: one secure connection fits at rest but not beside a media app (99 - 20 - 44 leaves 35 K, under the 71 K peak). So:

- One secure connection at a time. Chat waits whenever a media app is open.
- The walkie socket, which holds that slot, exists only on the walkie screen.
- The media tasks exist only while a media app is open or something plays.
- While something plays, chat's RAM shrinks to its last 5 messages and comes back from the card afterwards.

## What is enforced, and what isn't

Nothing limits an individual app's allocations. An earlier version charged every `malloc` to its app through a linker `--wrap` with a prefix on each block. That boot-looped the real device: libc's own `free` reached the real heap directly, past the wrapper, with a prefixed pointer. So there are no per-app budgets, and the upload check refuses any `--wrap` of the allocator.

What remains acts before heavy work starts, from the free heap as reported: a secure connection needs 80 KB free and a 20 KB block; the player waits if too little is free. The 16 KB floor for the screen and keyboard no longer exists as a limit. mbedTLS's allocator is left alone.

## Containment

`bad_alloc` is caught on every firmware task: chat's two tasks start over, the player and library report failure and carry on, the UI drops to HOME. The fault shows on HOME for 10 seconds and on the SYSTEM screen. Not contained, and left to the core dump path: panics such as a bad pointer, a stack overflow or the watchdog, and an exception thrown inside a prebuilt library that doesn't unwind.
