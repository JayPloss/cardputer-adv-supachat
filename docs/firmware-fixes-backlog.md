# Cardputer firmware fixes backlog

Recorded September 3, 2026. These are reports and planned acceptance criteria,
not claims that the defects have been reproduced or fixed.

## 1. Boot-skip key leaks into chat input

**Report:** Pressing a key to leave the title screen can insert the same key into
the active channel composer.

**Current-source observation:** `showBootSplash()` returns as soon as
`Keyboard.isPressed()` becomes true. Setup then enters the normal loop while the
physical key may still be held. There is no explicit release/debounce boundary
between boot input and chat input. Current `v0.48` is therefore still plausibly
affected; this is not assumed to be limited to the older attached build.

**Planned behavior:** Any key or rear-button press may dismiss the splash, but the
dismissal gesture is consumed completely. No character, navigation action,
keypress-song note, menu toggle, or send action reaches the first interactive
screen. Input becomes eligible only after all dismissal controls are released for
a short stable interval.

**Implementation direction:** Add a reusable input quarantine/release latch after
the splash rather than clearing only a text buffer. Cover keyboard matrix state
and `BtnA`, and avoid a blocking wait that prevents background initialization.

**Acceptance:** Emulator contract presses and holds a printable key across the
boot-to-chat transition, then releases it; the draft stays empty and tone count
does not advance. Repeat with Space, Enter, an arrow-position key, and the rear
button. Verify on a physical Cardputer with both a tap and a long hold.

**v0.49 source status:** Implemented with an input-release quarantine after the
boot splash and across charging-mode entry/exit. The deterministic printable-key
case passes; physical keyboard and rear-button acceptance remains outstanding.

## 2. Papa needs a custom boot logo

**Report:** Papa's Cardputer should have a custom logo.

**Current-source observation:** A dedicated `papa` build environment and splash
selector already exist, but Papa currently uses the generic
`supachat-logo-source.png`. Other named identities have personalized source art.

**Planned behavior:** The Papa environment selects Papa-specific source art and a
generated 240x135 RGB565 header without changing other identities. The firmware
version remains readable at bottom-left and the asset conversion is reproducible.

**Needed input:** Papa's desired artwork or a visual brief. Until that is supplied,
the generic image must not be presented as a completed custom logo.

**Acceptance:** Regenerate assets using `firmware/tools/make-splash-header.py`,
verify RGB565/channel/byte order, render the exact 240x135 Papa splash, confirm no
overlap with the version label, run the UI/device gates, and finally verify on the
known Papa hardware before release.

## 3. Julien build v0.43 repeats an old notification after restart

**Report:** Julien's Cardputer on user-reported build `v0.43` notifies for the same
recent Papa message whenever the device is restarted.

**Current-source observation:** `v0.43` introduced history and notices. Current
`v0.48` contains a guard intended to make initial history hydration silent:
`messageNotificationPending` is set only when `initialSyncComplete` is already
true. Current source also hydrates room history from `after=0` on boot and posts
read receipts for incoming history. The old build may predate the guard, but the
behavior has not yet been reproduced on `v0.48` hardware.

**Likely failure class:** Startup hydration is being classified as a newly arrived
message, or the persisted high-water/seen state is not applied before the first
notification decision. Power-off wording should be interpreted as a notice heard
during the next boot unless runtime evidence shows a shutdown sound path.

**Planned behavior:** Loading persisted history or rehydrating the latest server
history after reboot is silent. A message produces at most one local arrival
notification per device. A genuinely new message received after initial sync may
notify once. Room switches and reconnects do not replay notices for already-seen
message IDs.

**Acceptance:** Add deterministic tests for cold boot with SD history, cold boot
without SD history, server rehydration from zero, reconnect after transport loss,
room switch away/back, duplicate idempotent sync payloads, and a genuinely new
post-hydration message. Then collect physical runtime evidence on Julien and a
second known device. Do not call the defect fixed from the current source guard
alone.

## Attached Papa device

The attached Cardputer is Papa: MAC `28:84:85:76:A4:94`, environment `papa`, on
`COM3` during this check. This identity is already recorded in the repository's
authoritative resolver at `skills/flash-cardputer-safely/scripts/resolve-device.ps1`.
The installed firmware version is not available from COM enumeration while the
device is in download mode; use the version shown on Papa's splash/changelog or a
separately provenance-matched firmware artifact as evidence.
