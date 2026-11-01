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

Budgets: chat 44 K, each media app 72 K, and a 16 K floor no app can eat (the UI can). An app over budget gets a failed allocation, before the real call.

Tracking is a fixed table of block to owner, 8 KB of static memory, filled by `--wrap` hooks on `malloc`, `calloc`, `realloc`, `free` and libc's `_free_r`. The hooks return exactly the pointer the real allocator gave and never touch the block. An earlier version prefixed each block and boot-looped the real device, because libc's own `free` reached the heap past the hook with a shifted pointer. Calls before the scheduler runs, in an interrupt or on an unbound task pass straight through.

Error bounds, honestly:

- A free past the hooks (ROM code) leaves a stale entry, overstating its owner until an allocation lands on that address again.
- `reconcile` clears an owner's entries: a media app's when it stops, chat's every 5 minutes. Live blocks stop counting too, so just after a reconcile chat's figure is low, by at most its RAM (about 15 K).
- A full table leaves new blocks uncounted but working; SYSTEM shows how many, and how many allocations were refused.
- Anything calling `heap_caps_malloc` directly isn't seen: the Wi-Fi driver, lwIP's pools, M5GFX's buffers, and mbedTLS. TLS is held back by the broker instead, which needs 80 KB free and a 20 KB block.

## Containment

`bad_alloc` is caught on every firmware task: chat's two tasks start over, the player and library report failure and carry on, the UI drops to HOME. The fault shows on HOME for 10 seconds and on the SYSTEM screen. Not contained, and left to the core dump path: panics such as a bad pointer, a stack overflow or the watchdog, and an exception thrown inside a prebuilt library that doesn't unwind.
