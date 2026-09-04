# SupaChat charging mode

## Status and purpose

This mode is implemented in the SupaChat `v0.49` source and emulator. Compilation,
device contracts, and exact-resolution UI checks pass; physical charging current,
radio suspension, logging, and exit/reconnection remain hardware acceptance gates.
It is not a standalone diagnostic firmware and it is not intended to solve the
full charging-hardware mystery.

Charging mode gives useful feedback while a Cardputer is plugged in and minimizes
the device's own power use so more available USB power reaches the battery. It
records and plots how the battery estimate and voltage change during the current
charging session.

A possible future standalone **Charge Detective** firmware may investigate
switch-on/off behavior, reset causes, controlled load sag, charger characteristics,
and deep-sleep measurements. That work is explicitly out of scope here.

## User experience

Add `CHARGING MODE` to the second menu page.

Selecting it opens a confirmation screen:

```text
CHARGING MODE

PAUSES CHAT + RADIO
SHOWS BATTERY HISTORY

OK START       LEFT CANCEL
```

Once started, the mode remains active until the user exits or restarts the device.
It is not inferred automatically from voltage and does not depend on unreliable
charger-status APIs. Entering the mode means “I have put this device aside to
charge,” regardless of whether firmware can prove a cable is present.

The main view is a fixed-scale charge-history plot:

```text
CHARGING          01:35
4021 mV    74%    +126 mV

4300 +--------------------
     |              ____
     |        _____/
     |  _____/
3200 +--------------------

RISING       27 SAMPLES
LEFT EXIT      OK DIM
```

Show:

- current filtered millivolts as the primary value;
- current SupaChat percentage estimate as a secondary reference;
- change in millivolts since the session began;
- elapsed session time;
- sample count;
- `RISING`, `FLAT`, `FALLING`, or `UNSTABLE` trend;
- a voltage trace with a fixed 3200-4300 mV vertical range;
- explicit `NO RISE YET` when the data does not support a charging trend;
- SD logging state when a card is present.

Do not show `FULL`, time remaining, charging current, cable-connected, or charge
rate in mAh. The Cardputer ADV does not directly expose enough information to make
those claims reliably.

## Controls

- `LEFT`: ask to exit charging mode, then restore normal SupaChat operation.
- `OK`: toggle between normal charging-screen brightness and a very dim view.
- Any other keyboard input is ignored and must not become a chat draft after exit.
- Rear button may mirror `LEFT` if that matches the final menu convention.

Exiting requires a clean subsystem restart and returns to the menu, not directly
to a composer with stale key state.

## What is disabled

Charging mode is an explicit low-activity application state. On entry:

- stop the HTTPS synchronization loop;
- close the walkie WebSocket;
- stop voice recording and playback;
- stop all speaker tones, message chirps, and keypress music;
- disconnect Wi-Fi and set Wi-Fi off;
- stop ESP-NOW transmissions and receive processing;
- disable Bluetooth if any library component enabled it;
- stop network scanning and connection retries;
- stop normal room/message rendering;
- stop presence broadcasts;
- stop automatic SD history writes and other unrelated filesystem activity;
- suppress message-arrival notification processing;
- reduce CPU frequency if the resolved ESP32/M5 stack allows it without breaking
  the display, keyboard, timer, or battery ADC;
- lower display brightness to a dedicated charging-mode level.

No task may continue waking frequently just to discover that charging mode is
active. Existing network/walkie tasks should block on an event or use a long bounded
wait until exit rather than spin every few milliseconds.

The following remain active:

- battery sampling and filtering;
- charging-session state and plot updates;
- the display at a reduced refresh cadence;
- only the input needed to dim or exit;
- optional append-only charging-session logging;
- a minimal clock/elapsed-time source.

## Entry and exit lifecycle

Implement charging mode as a real `ScreenMode::Charging` plus an application-level
`chargingModeActive` state consumed by every background subsystem.

### Entry

1. Confirm the request.
2. Quarantine the confirmation key until all keys/buttons are released.
3. Stop or close voice, walkie, and pending audio safely.
4. Pause network tasks and prevent reconnect attempts.
5. Disable ESP-NOW and Wi-Fi in the correct stack order.
6. Flush any already-pending SupaChat history write once, if safe, then prohibit
   unrelated SD operations.
7. Capture the session baseline sample and monotonic start time.
8. Clear the in-memory charging plot and add the first sample.
9. Reduce CPU/display activity and render the charging screen.

### Exit

1. Stop charging-session logging and close its file.
2. Restore CPU frequency and display settings.
3. Reinitialize the radio stack rather than assuming paused state survived.
4. Resume Wi-Fi discovery, ESP-NOW, sync, presence, and walkie tasks.
5. Force a normal room resynchronization so messages missed while charging arrive.
6. Quarantine the exit key until release.
7. Return to the menu with any pre-existing draft preserved and no entry/exit key
   added to it.

If subsystem restoration fails, show the specific failed stage and offer `RETRY`
or `RESTART`; do not leave the user on an indefinite spinner.

## Sampling and plot model

The plot is intended to show change over the charging session, not laboratory-grade
state of charge.

### Sampling cadence

- Take one raw battery reading every second using the existing firmware path.
- Continue the existing low-pass filter for the live numeric display.
- Commit one plot/log point every five minutes.
- Also commit an event point when a meaningful trend/state transition occurs.
- Render only when a value, state, minute, control, or new plot point changes.

A five-minute plot cadence gives 12 points per hour and keeps noise/readability
manageable. Store enough points for at least a 12-hour session. If a fixed array is
used, 144 five-minute points covers 12 hours; after it fills, either scroll the
window or decimate older adjacent points without changing the voltage axis.

### Plot point

```cpp
struct ChargePlotPoint {
  uint32_t elapsed_s;
  uint16_t filtered_mv;
  uint8_t estimated_pct;
  uint8_t state;
};
```

The plotted value is the filtered operational voltage. It is not a controlled rest
measurement because the display and application remain awake. Label documentation
and exported logs accordingly.

### Trend classification

Classify from a rolling window of committed plot points:

- `RISING`: sustained increase beyond a configurable noise threshold;
- `FLAT`: window span remains inside the noise threshold;
- `FALLING`: sustained decrease beyond the threshold;
- `UNSTABLE`: direction changes or spread is too large for the other labels;
- `WAITING`: not enough points yet.

Use hysteresis so the label does not oscillate. Tune thresholds from physical logs.
The trend is descriptive and must not be renamed `CHARGING DETECTED` without better
hardware evidence.

## Battery-event logging

Keep a bounded in-memory event ring for the active session. When an SD card is
available, append events to `/supachat-charge.csv` after sampling—not continuously
on every one-second ADC read.

Suggested schema:

```text
schema_version,device_id,firmware_version,session_id,elapsed_s,
event,raw_mv,filtered_mv,estimated_pct,trend,detail
```

Initial events:

| Event | Meaning |
|---|---|
| `MODE_ENTER` | User started charging mode. |
| `SAMPLE` | Scheduled five-minute plot point. |
| `TREND_RISING` | Rolling classification entered rising. |
| `TREND_FLAT` | Rolling classification entered flat. |
| `TREND_FALLING` | Rolling classification entered falling. |
| `TREND_UNSTABLE` | Rolling classification became noisy/ambiguous. |
| `PERCENT_CHANGE` | Displayed estimate crossed an integer percentage. |
| `LOW_VOLTAGE` | Voltage crossed a provisional low threshold. |
| `SD_FAILURE` | Session logging failed; mode continued in memory. |
| `SD_RECOVERED` | A later bounded retry restored logging. |
| `MODE_EXIT` | User exited charging mode normally. |

Do not write to NVS on every sample or percentage change. Reuse the existing
five-minute battery-estimate persistence cadence or write once on clean exit.

The log is for understanding how readings change over time. It does not prove
energy transferred, charge current, battery capacity, or charger status.

## Display behavior and power

The charging screen is useful only if its own draw is restrained.

- Default to a low but readable brightness.
- `OK` dims the backlight to zero or the lowest physically useful level.
- In dim state, continue collecting five-minute samples without repainting unless
  needed for the next wake/view interaction.
- Consider blanking the display after a configurable idle period while retaining
  a one-key wake that is fully consumed.
- Keep the UI refresh at no more than one frame per second for live numbers and
  once per committed point when dimmed.
- Fill unused display regions black.
- Never play a tone merely because the trend label changes.

Deep sleep is not required for the first integrated version because normal wake,
keyboard, task, and radio restoration behavior would become substantially more
complex. First make the explicit low-activity mode correct and measure its current.

## Interaction with messages

Charging mode intentionally takes the device offline. Messages remain on the
server or other peers and reconcile after exit through the existing idempotent sync
path.

- Do not display stale chat while in charging mode.
- Do not notify for messages until normal sync resumes.
- Initial post-charge hydration must be silent for already-seen messages and notify
  only according to the repaired notification/high-water rules.
- Queued outbound messages created before charging mode remain queued and are not
  discarded.
- Entering charge mode must be blocked or explicitly confirmed while recording or
  holding PTT so audio state is not truncated silently.

## Failure behavior

- SD absent/full/corrupt: continue with the in-memory plot and show `LOG MEMORY` or
  `SD ERROR`.
- ADC reading outside plausible range: retain the last good display value, mark a
  gap in the plot, and log `BAD_SAMPLE`.
- Millis wrap: compute elapsed intervals with unsigned subtraction.
- Unexpected radio wake/reconnect: log the invariant violation, shut it down again,
  and surface `RADIO ERROR` rather than silently spending power.
- Restart during charging mode: default to normal SupaChat startup unless later
  evidence supports persisting/re-entering the mode. Never trap the device in a
  charging screen after a crash.
- No voltage rise: continue plotting and say `NO RISE YET`; do not infer that the
  cable, charger, switch, or battery is faulty.

## Emulator and host tests

Add deterministic coverage for:

- menu entry, confirmation, cancellation, dim, and exit;
- entry/exit key quarantine so no character leaks into chat;
- all network/audio/ESP-NOW work becoming disabled while active;
- network tasks waiting rather than busy-spinning;
- five-minute point cadence and 12-hour ring behavior;
- fixed plot axis and clipping of out-of-range samples;
- trend classification and hysteresis for rising, flat, falling, and noisy traces;
- SD absence and append failure;
- percentage changes being logged without excessive durable writes;
- exit restoring each subsystem and forcing room resynchronization;
- reboot starting normally rather than automatically re-entering charging mode;
- exact 240x135 layout at 0, 1, several, and full-ring sample counts.

Synthetic traces validate software behavior only. They do not prove that the
physical device is charging.

## Physical acceptance

On a known Cardputer, with exact source/build provenance:

1. Enter charging mode and confirm Wi-Fi, ESP-NOW, walkie, audio, and notification
   activity stop.
2. Measure charging-mode current at normal and dim brightness with the inline USB
   meter.
3. Leave it connected for several hours and confirm the graph advances every five
   minutes without freezing or corrupting.
4. Remove the SD card and repeat; the plot must continue in memory.
5. Exit and confirm Wi-Fi/ESP-NOW reconnect, rooms resynchronize, queued messages
   remain, and controls work.
6. Hold keys across entry and exit; no key may leak into a chat draft.
7. Compare CSV points with the on-device graph and displayed millivolts.
8. Power-cycle during the mode and confirm normal startup recovery.

The feature is complete when it provides a stable low-activity charging view,
records an understandable session trace, and restores SupaChat reliably. It does
not need to settle the switch-on/off charging mystery; that belongs to a future
Charge Detective firmware.
