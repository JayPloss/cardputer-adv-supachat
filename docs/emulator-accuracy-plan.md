# Urgent Cardputer Emulator Accuracy Plan

## Objective

Make emulator success mean that the same raw input, state transition, and draw
contract used by the Cardputer firmware was executed. Browser-only UI mocks and
tests that call logical actions directly must not qualify as device emulation.

## P0: physical input fidelity

- Maintain one canonical Cardputer ADV key-matrix fixture containing physical
  coordinates, first/shift characters, Fn, Opt, Ctrl, Alt, Enter, Backspace,
  Space, and rear Menu-button events.
- Port the installed M5Cardputer `Keyboard_Class::keysState()` behavior into an
  executable host module, including its two-pass modifier handling.
- Feed raw `KeysState` plus `keyList` fixtures into the same navigation decision
  function used by firmware. Tests must never call `up()`, `down()`, `left()`,
  or `right()` as a substitute for a physical key event.
- Exhaustively test every physical key under plain, Fn, Shift, Opt, Ctrl, and
  Alt on every screen class: text entry, menu/navigation, recording, playback,
  and modal screens.
- Block firmware upload if any raw-key fixture diverges from the installed M5
  library contract.

## P0: shared screen state machine

- Extract screen transitions from `main.cpp` into a platform-neutral C++ state
  machine used directly by both firmware and a native host test executable.
- Cover every screen and exit path: Chat, Menu pages, Rooms, Voice Messages,
  Walkie-Talkie, Volume, Language, Networks, Password, ESP-NOW Local, Status,
  and Changelog.
- Generate a complete transition matrix for every input accepted on every
  screen, including held/released keys and rear Menu-button behavior.
- Assert invariants: text-entry punctuation cannot navigate; navigation screens
  cannot type; Menu always exits a sub-screen; Enter does only the action shown
  in the footer; inactive controls do nothing.

## P1: firmware-native rendering

- Replace hand-maintained browser drawing as the source of truth with a native
  240×135 render harness that executes the firmware draw functions against a
  recording canvas or M5GFX PC backend.
- Record draw primitives and pixels for every reachable UI state, including
  selected/unselected rows, long French labels, errors, empty lists, maximum
  list sizes, recording, playback, local-only mode, and long changelog entries.
- Compare exact pixel output with reviewed golden images. Any changed pixel set
  requires an intentional golden update.
- Enforce the physical screen budget automatically: header 0–19, content
  20–105, composer from 106, and footer no lower than 123.

## P1: runtime-contract simulation

- Add deterministic fakes for Wi-Fi, HTTPS, JSON memory, WSS, ESP-NOW, SD,
  battery samples, microphone DMA, speaker queues, time, and NVS reboot state.
- Exercise failures and recovery, not just labels: malformed/large history,
  room-switch sync, Wi-Fi loss, ESP-NOW local-only reboot reset, voice upload,
  walkie PTT, SD absence, and charging trends during idle versus user activity.
- Link every fake to installed-library behavior or captured device traces. A
  regex assertion alone is not runtime emulation.

## P1: release gate

- Make the pre-upload gate run native input tests, full state-transition tests,
  runtime-contract tests, exact 240×135 render tests, and both English/French
  firmware builds.
- Produce a machine-readable coverage report listing every screen × input ×
  modifier combination. Upload fails if any reachable combination is untested.
- Keep a small physical acceptance suite for behaviors that cannot be proven on
  host; feed every discovered mismatch back into a failing emulator contract
  before another flash.

## Completion criteria

The emulator is trustworthy only when:

1. Browser controls enter through raw matrix events.
2. Firmware and host tests execute the same input/state-machine code.
3. Every reachable UI state has exact 240×135 render coverage.
4. Every screen/input/modifier combination appears in the coverage report.
5. The upload gate rejects missing coverage or firmware/emulator drift.
6. A physical acceptance pass finds no behavior absent from the emulator suite.
