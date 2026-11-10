---
name: identify-cardputer-device
description: Identify a connected SupaChat Cardputer by MAC address, name, PlatformIO environment, and current COM port. Use for every device lookup or when the user refers to an attached Cardputer; do not invoke release, build, emulator, provisioning, or flash gates for identification alone.
---

# Identify a SupaChat Cardputer

Device identification is a lightweight, read-only lookup.

## Required workflow

1. From the active SupaChat repository root, run:

   ```powershell
   & .\skills\flash-cardputer-safely\scripts\resolve-device.ps1
   ```

2. Report the returned `Name`, `Mac`, `Environment`, and `Port` directly.
3. Treat that repository-local resolver as authoritative for this checkout. Do
   not substitute a global skill copy, remembered COM port, old conversation,
   nearby worktree, or manually reconstructed identity table.
4. If the repository-local resolver and another copy disagree, use the
   repository-local result and report the stale-copy defect separately.
5. Stop only when the repository-local resolver reports an unknown or ambiguous
   device. Do not invent an identity.

## What an attached Cardputer means

- A Cardputer on USB is in download mode (black screen), put there to be flashed
  (`../flash-cardputer-safely/references/flash-protocol.md`). It is not running
  SupaChat, so it has no version to read: the answer to "is it on vX?" is that it
  is waiting for a flash. When the request is to get it onto a version, flash it.
- Look at the kernel log (`journalctl -k`), not one `lsusb`: entering download
  mode re-enumerates it several times, and a single look can land in a gap.
- After the flash, the owner resets or unplugs it. That is the known step, not a
  sign that something went wrong.
- (Origin 2026-11-10: I said nothing was plugged in, then that the version could
  only be read from its screen, then flashed it and called the expected black
  screen a possible problem. All of it was already in the flash protocol.)

## Scope boundary

Identification alone does **not** require release provenance, contract emulation,
compilation, runtime acceptance, provisioning, or flash authorization. Do not load
or run those workflows merely to answer which device is attached.

If the user also asks to build, provision, flash, or validate physical runtime,
first return the identity, then apply the separate skill for that requested action.

COM enumeration identifies the hardware but does not prove the installed firmware
version. Report a version only from visible runtime text, serial application output,
or provenance-matched artifact evidence.

