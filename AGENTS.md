# SupaChat workspace guardrails

## Device identification

- For every request to identify, look up, inspect, or act on an attached
  Cardputer, first load and follow
  `skills/identify-cardputer-device/SKILL.md`.
- The current repository's
  `skills/flash-cardputer-safely/scripts/resolve-device.ps1` is the authoritative
  device identity map. Never substitute the similarly named global skill script,
  a remembered COM port, another worktree, or an older copied map.
- Identification is a lightweight read-only operation. Do not run release,
  emulator, build, provisioning, or flash gates unless the user also requested
  the corresponding action.
- Report the resolved name, MAC, PlatformIO environment, and current COM port
  without ceremony. An unknown device is a lookup failure, not permission to
  guess.

- Opening a Cardputer's serial port on Linux (pyserial, `cat`, `stty`, a monitor) can reset it
  into download mode: the kernel raises DTR and RTS one after the other, and the USB-Serial/JTAG
  reads that as "reset with GPIO0 low". It then sits at `waiting for download`, off the network,
  until reset again. esptool 4.5.1's `--after hard_reset` and an RTC-watchdog reset did not bring
  it back (2026-11-07, Papa's device). Don't open the port on a running device just to read logs;
  check its state from the server first (`presence.last_seen_at` moves on every request), and if
  it is stuck, the owner presses reset or unplugs it. (Origin 2026-11-07: a read-only log check
  knocked Papa's device offline during a delivery diagnosis.)

## Emulators

- `emulator/` here is a browser mock of the UI plus contract tests of library behaviour. It does not
  run the firmware.
- `JayPloss/cardputer-emulator`, checked out beside this repo, is the runtime one: it compiles the
  real firmware against a device-sized heap, and it reproduced both real out-of-memory crashes.
  Run `make check` there before promoting anything to a device; the upload gate runs it when it is present.
- `firmware/include/os.h` is the layer keeping apps within their RAM: budgets, one secure connection
  at a time, containment, the boot phase. `docs/heap-budget.md` has the measurements behind it.

## Investigating a device report

- Start from the device's own evidence, all of it, before any theory or question: what the person
  saw, the card's files and their timestamps (place files, `supachat-charge.csv` sessions and
  versions), and the core dump (`tools/read-crash.sh <env>` while it is in download mode). Then make
  the runtime emulator reproduce what the device did; a pass there that contradicts the device means
  a stand-in is hiding the failure, which is the next thing to fix.
- Report only what the evidence shows. A guess is not offered as a cause, and a partial
  explanation is not called found.
- (Origin 2026-11-10: Julien's device showed a few books, then none after a reboot. I declared the
  missing episodes the cause, read code for theories, floated a card-speed guess his device had
  already disproved, and asked him what the screen showed, before reading the core dump.)

## Core libraries

- Every library and the platform in `firmware/platformio.ini` is pinned to an exact version: no `^`, `~`, `>=` or bare names. A device build never pulls in new library code on its own.
- Updating one is a deliberate migration: read its release notes, change our code to match, pass the emulators and the upload gate, then flash every device in the same round, so the family never runs on mixed libraries. Record it in the changelog.
- (Origin 2026-11-08: unpinned, M5GFX and M5Unified 0.3.0 came into the builds for Julien, Mama, Naomie and Andrew on Nov 7. 0.3.0 makes rotation 0 the upright position, so our `setRotation(1)` turned Julien's screen; Papa's build had stayed on 0.2.36.)

## Screens

- Before designing or changing a screen, menu, label, font size, key behaviour or UI sound, load `skills/design-reference-cardputer/SKILL.md`. Its `references/lessons.md` holds the mistakes already made, and takes new ones.

## Authentication claims

- Never describe a login issue as fixed, functional, working, or expected to work based only on account existence, password assignment, password-hash verification, server health, route health, unit tests, or a simulated identity header.
- Authentication is proven only when the same released client and login route used by the person completes the entire flow: credential entry, Authentik authentication, redirect or deep link, token/code exchange, SupaChat session creation, identity mapping, and retrieval of the expected rooms.
- Report partial evidence precisely. For example, `the stored password matches Authentik` is not `Android login works`.
- If the physical Android client has not completed the flow, state `Android login is unproven`. Do not predict success.
- A claim of successful end-to-end login must identify the tested client/build, username, resulting SupaChat identity, and returned room set. Never print or log passwords.

## Authentication test-readiness gate

- For Android authentication work and every APK release, load and follow `skills/gate-android-authentication/SKILL.md`; its checklist is a blocking release gate.

- Do not hand an APK to the user or ask the user to test login while any independently testable stage is merely assumed, partially configured, or known to be unverified. “Ready to test” means engineering preflight is complete, not “ready to discover the next integration failure.”
- Before user acceptance, exercise the whole authentication chain with the release configuration: authorization request, credential acceptance, registered Android callback, callback delivery to the installed package, preservation of PKCE state and verifier across backgrounding or process recreation, token exchange, `/api/native/session`, secure session persistence, identity mapping, and expected-room retrieval.
- Inspect the built APK rather than trusting source configuration. Confirm package name, intent filters, callback URI, embedded issuer/client configuration, and build provenance.
- Test failure paths and time bounds. No authentication or startup operation may leave an indefinite spinner; it must resolve or show an actionable error within a defined timeout.
- Automate or emulate every stage that does not intrinsically require the physical phone. Use the physical-phone run only as the last acceptance gate, with runtime logging attached when possible.
- If any stage cannot be exercised before handoff, identify that exact gap before asking for a test. Do not collapse several untested stages into “please try it.”
