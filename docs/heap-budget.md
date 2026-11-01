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
| Reserved floor for the screen and keyboard | 16,384 | `os::kFloorBytes` |

What follows from those: one secure connection fits at rest but not beside a media app (99 - 20 - 44 leaves 35 K, under the 71 K peak plus the floor). So:

- One secure connection at a time. Chat waits whenever a media app is open.
- The walkie socket, which holds that slot, exists only on the walkie screen.
- The media tasks exist only while a media app is open or something plays.
- While something plays, chat's RAM shrinks to its last 5 messages and comes back from the card afterwards.

Budgets: chat 38 K (its measured peak is 36 K, during the first sync), each media app 62 K.

## What enforcement can't reach

The ledger sees what goes through `malloc`, `calloc`, `realloc` and `new`, by `--wrap` at link time, including from the prebuilt libraries that call `malloc`. It can't see anything calling `heap_caps_malloc` directly (the Wi-Fi driver, lwIP's pools, M5GFX's buffers) or ROM code. mbedTLS goes through `mbedtls_platform_set_calloc_free`, which stops it eating the floor. The broker admits a connection by free heap at the start, not by reserving it, so something else can still lose out during a handshake, and is then contained.

## Containment

`bad_alloc` is caught on every firmware task: chat's two tasks start over, the player and library report failure and carry on, the UI drops to HOME. The fault shows on HOME for 10 seconds and on the SYSTEM screen. Not contained, and left to the core dump path: panics such as a bad pointer, a stack overflow or the watchdog, and an exception thrown inside a prebuilt library that doesn't unwind.
