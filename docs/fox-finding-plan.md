# Fox Finding and place printing

## Status and intent

This document defines a planned, infrastructure-free person-finding mode for
SupaChat. No Fox Finding implementation or hardware result exists yet. All
radio-performance and bearing claims in this plan are hypotheses until the
measurement gates below pass on physical Cardputer ADV units.

Fox Finding helps one SupaChat user find another at a crowded outdoor venue
roughly 300 metres across. It must work on arrival at an unfamiliar venue without
internet, fixed relays, pre-positioned beacons, a site survey, GPS, or a compass.

The feature combines:

1. **Direct ESP-NOW signal strength** for proximity and a possible body-shadow
   bearing.
2. **Place printing**, Jay's name for comparing the Wi-Fi access points visible
   to each handheld and their signal strengths.
3. **The BMI270 IMU** for step detection, device posture, and short-term relative
   rotation. The Cardputer ADV has no magnetometer, so gyroscope yaw is allowed
   to drift and may never be presented as compass heading.

The minimum worthwhile product is a reliable warmer/colder proximity signal.
A left/right deviating dot is an enhancement that ships only if repeatable
body-shadow measurements support it.

## Design constraints

- No internet, server computation, relays, pre-placed beacons, site survey, or
  prior visit.
- No magnetometer, external antenna, GPS, or added Grove hardware in v1.
- Neither device derives geographic coordinates. Devices compare live radio
  observations directly.
- The initial range target is venue-scale best effort, not a guaranteed 300-metre
  radio range. Crowds, bodies, stalls, metal structures, antenna orientation, and
  the target's movement may reduce useful range substantially.
- The UI shows coarse guidance and measurement confidence rather than pretending
  the radio data is more precise than it is.

## Terminology

| Term | Meaning in SupaChat |
|---|---|
| Place print | A bounded set of visible 2.4 GHz access-point BSSIDs, RSSIs, and channels sampled at one place and time. |
| Direct RSSI | Signal strength attached to packets received directly from the target Cardputer. |
| Search sample | One timestamped direct-RSSI observation, IMU attitude sample, or place print used by an active session. |
| Calibration walk | A prompted, short, approximately straight walk used to associate device-forward motion with changing signals. |
| Body-shadow sweep | A slow turn with the seeker device upright against the sternum, used to test whether the wearer's body creates a repeatable RSSI depression. |
| Confidence | A quality grade derived from sample count, recency, variance, target motion, AP overlap, and agreement between channels. It is not statistical certainty unless later calibrated as such. |

## User experience

### Starting a search

Fox Finding appears on the second menu page. Opening it shows only eligible,
recently heard members of the current group. The seeker chooses a target and
confirms `FIND <NAME>?`.

The target receives a notice:

```text
PAPA IS LOOKING FOR YOU
OK START
```

After starting, the target screen says:

```text
FOX FINDING ACTIVE
STAND STILL IF SAFE
OK END
```

The target may optionally follow a five-step calibration prompt, but standing
still immediately must remain useful. The mode must work even when the target
does not understand or complete the walk prompt.

### Seeker guidance

The seeker is prompted to wear or hold the Cardputer upright against the chest.
The first experimental flow is:

1. Wait for direct target packets and exchange a place print.
2. Walk the on-screen number of straight steps while holding the device in a
   stable orientation.
3. If body-shadow bearing is enabled, turn slowly once through 360 degrees.
4. Follow a proximity pulse and, only when confidence permits, a left/right dot.

The primary screen favors truthful, coarse instructions over invented precision:

```text
FINDING JULIEN       07:42

      <    o    >
       GO LEFT

SIGNAL  WARMER
RANGE   NEAR / ROUGH
FIX     GOOD  2s

LEFT END       OK RESAMPLE
```

The range vocabulary is initially qualitative: `NO SIGNAL`, `FAR`, `CLOSER`,
`NEAR`, and `VERY NEAR`. Metres are prohibited until field calibration shows a
stable error bound across representative venues. When bearing confidence is too
low, the dot disappears and the screen explicitly says `TURN GUIDANCE UNSURE`.

Audio and haptics are simple and redundant with the screen:

- a slow pulse for far and faster pulse for near;
- two descending notes when the trend is colder;
- two rising notes when the trend is warmer;
- a distinct lost-fix tone after a grace period, never on every dropped packet;
- full compatibility with the existing volume and mute controls.

### Ending and failure behavior

Either participant can end the session. It also ends after ten minutes or after
an unrecoverable protocol error. Temporary
radio loss enters `SEARCHING FOR SIGNAL` and preserves the last direction only
briefly with a visible `STALE` label. It must never silently reuse an old fix.

If the target does not start the mode, times out, or is busy in another session,
the seeker is told plainly. If Wi-Fi AP density is too low for place printing,
direct RSSI mode continues. If the bearing experiment fails, only proximity
guidance is exposed.

## Information channels

### Channel A: place printing

Each device performs a bounded 2.4 GHz Wi-Fi scan and records `{BSSID, RSSI,
channel}`. SSIDs are neither required nor exchanged. The target returns its live
place print after starting the requested mode.

The first matcher should be deliberately explainable:

1. Keep at most 24-30 strongest valid AP entries.
2. Match entries by BSSID.
3. Compute overlap count and weighted RSSI difference for matched entries.
4. Penalize missing strong entries more than missing weak entries.
5. Down-weight unstable entries observed with high variance over repeated scans.
6. Emit a normalized similarity and a confidence grade, not metres.

The approximate v1 entry budget is eight bytes before framing: six-byte BSSID,
signed RSSI byte, and channel byte. A single ESP-NOW v1 payload can hold about 30
entries only if framing, authentication, and protocol headers are kept within the
250-byte ceiling. The actual encrypted packet budget must be measured against
SupaChat's `EspNowPacket`; fragmentation is avoided in the first experiment by
reducing the entry cap if necessary.

A full multi-channel scan interrupts listening on the ESP-NOW channel. Place
prints therefore use sparse scans, not continuous scanning. Between scans, the
UI interpolates a trend from direct RSSI without claiming a new place-print fix.
The scheduler must return to the shared ESP-NOW channel promptly and expose scan
age in diagnostics.

The proposed short calibration walk does not subtract two noisy coordinate
estimates. The IMU identifies device-forward movement and steps; radio samples
ask whether particular AP signals became stronger or weaker along that movement.
Whether a five-step baseline contains enough information is entirely contingent
on the gradient-visibility experiment.

### Channel B: direct RSSI and body shadowing

During an active session, the target sends a small encrypted beacon at an
initial rate of 10 Hz. The seeker records receive time, sequence, RSSI, radio
channel, and current IMU-relative yaw.

The bearing hypothesis is that a chest-worn device sees a broad and repeatable
RSSI depression when the wearer's torso is between the target and the receiver.
One slow rotation can then identify a coarse rear sector. The initial algorithm
should use a circular, robustly smoothed RSSI curve:

- bin samples by relative yaw;
- reject bins with too few fresh packets;
- compare a broad low-signal sector with the opposite sector;
- require a configurable minimum contrast and repeatability across sweeps;
- derive only `LEFT`, `RIGHT`, `AHEAD`, or `UNSURE`;
- invalidate the solution when device tilt, target motion, packet loss, or curve
  ambiguity exceed limits.

The original design's estimated 15-20 dB depression, 120-degree sector, and
30-45-degree directional precision are unverified expectations. They are test
threshold candidates, not product claims.

Direct RSSI is also useful without bearing. A robust median/EWMA trend over fresh
packets can drive warmer/colder feedback. Absolute RSSI must not be naively mapped
to distance because antenna orientation, bodies, and multipath dominate it.

### Channel C: IMU

The accelerometer provides gravity, tilt rejection, approximate step events, and
a cue that the device is being held as instructed. The gyroscope provides smooth
short-term relative rotation. With no magnetometer, yaw has no absolute reference
and drifts.

If Channel B proves useful, a complementary estimator uses gyro integration for
fast motion and occasional body-shadow sector observations for slow absolute
correction. Corrections must be rate-limited and confidence-weighted so a noisy
radio sweep cannot snap the dot unpredictably.

The IMU is never allowed to label a cardinal direction. Resetting relative yaw,
changing device posture, or removing the device from the chest invalidates the
current bearing calibration.

## Protocol proposal

Fox Finding messages use the existing encrypted ESP-NOW envelope and current
device identities. They must not create a separate unencrypted packet format.
Every message includes protocol version, session ID, sender device ID, target
device ID, monotonically increasing sequence number, and a bounded timestamp or
age field suitable when wall-clock time is unavailable.

Proposed message types:

| Type | Direction | Purpose |
|---|---|---|
| `FIND_REQUEST` | seeker to target | Ask for a time-limited cooperative session. |
| `FIND_START` | target to seeker | Confirm that the target started Fox Finding. |
| `FIND_BEACON` | target to seeker | High-rate sequence used for direct RSSI observations. |
| `PLACEPRINT_REQUEST` | seeker to target | Request a fresh bounded scan. |
| `PLACEPRINT_RESULT` | target to seeker | Return entries, scan duration, age, truncation flag, and entry count. |
| `MOTION_STATE` | target to seeker | Coarse `STILL`, `MOVING`, or `UNKNOWN`; no path or step history. |
| `FIND_END` | either direction | End reason and final acknowledgement. |

Requests are idempotent. A target runs at most one active search session.
Unknown message versions are ignored safely. Session IDs and sequence windows
prevent a delayed packet from reviving an expired session. Rate limits prevent a
malfunctioning unit from monopolizing the radio. Scan data is held in RAM during
normal operation; diagnostic builds may export measurements for the physical
test program described below.

## Firmware architecture

Implement Fox Finding as an independent state machine rather than adding flags to
chat rendering or the networking loop.

```text
IDLE
  -> REQUESTING -> WAITING_FOR_START
  -> TARGET_PROMPT -> TARGET_ACTIVE
  -> ACQUIRING -> CALIBRATION_WALK -> OPTIONAL_BODY_SWEEP
  -> GUIDING <-> RESAMPLING
  -> SIGNAL_LOST -> GUIDING
  -> ENDING -> IDLE
```

Suggested components:

- `FoxSession`: participant IDs, expiry, sequence windows, and state.
- `FoxRadioScheduler`: beacon timing, scan windows, channel restoration, and chat
  coexistence.
- `PlacePrintSampler`: bounded scan collection and normalized records.
- `PlacePrintMatcher`: similarity, gradient features, uncertainty inputs.
- `DirectRssiTracker`: robust filtering, freshness, warmer/colder trend.
- `BodyShadowEstimator`: circular bins, contrast, ambiguity, bearing correction.
- `FoxImuTracker`: posture, step events, relative yaw, drift/reset signals.
- `FoxGuidance`: fuses channel outputs into coarse, confidence-gated UI commands.
- `FoxDiagnostics`: counters and compact export for controlled field trials.

All radio and sensor work is non-blocking. Keyboard capture, message receipt, PTT
safety, and display refresh retain their existing scheduling guarantees. Fox mode
may reduce normal sync frequency but must not corrupt queued messages or switch
rooms implicitly. PTT and Fox Finding cannot both own the high-rate radio/audio
interaction; v1 should show a busy explanation instead of attempting concurrency.

## Resource and power budgets

Initial engineering budgets, to be revised from measurements:

- Maximum 30 APs, reduced if the encrypted packet cannot fit without fragments.
- Maximum 360 one-degree bearing bins is unnecessary; start with 24 bins of 15
  degrees and bounded aggregates.
- No unbounded sample vectors; use rings and running statistics.
- Beacon at 10 Hz only during an active session, lowered after acquisition if
  field tests allow.
- Place-print scan no more often than needed for a useful trend; start at one
  complete scan on acquisition and explicit/slow resampling.
- Ten-minute hard session expiry.
- Record heap minimum, scan interruption time, packet receive rate, and battery
  draw during every field trial.

## Diagnostics and evidence

Developer diagnostics should make every conclusion auditable. A controlled test
build may export CSV to microSD:

```text
monotonic_ms,session_phase,relative_yaw_deg,tilt_deg,step_count,
direct_rssi,packet_seq,packet_age_ms,scan_id,bssid_hash,ap_rssi,channel
```

BSSIDs may be session-salted hashes in shared logs unless raw values are needed
for a local experiment. Logs identify firmware ref/commit, device identity,
radio/core versions, test geometry, posture, crowd conditions, and whether either
participant moved. UI screenshots alone are not evidence for
radio, IMU, or range behavior.

## Experimental gates

### Gate 0: toolchain and packet contract

Before feature code, confirm the installed Arduino/ESP-IDF stack exposes receive
RSSI through the callback actually used by this project. The current PlatformIO
declaration is `espressif32@6.7.0`; inspect the resolved core and ESP-NOW headers,
not version assumptions. Measure the encrypted payload available after SupaChat's
envelope. Prove scan completion returns to the expected channel and that chat
packets resume.

### Gate 1: body-shadow bearing

- Two known Cardputers, open space, approximately 30 metres apart.
- Target transmits at 10 Hz; seeker wears the device upright on the sternum.
- Perform several slow 360-degree turns in both directions and repeat at multiple
  distances and orientations.
- Record RSSI and gyro-relative yaw.

Advance only if a depressed sector is repeatable within and between sweeps. The
initial candidate threshold is 15 dB of contrast. Failure removes directional UI
from v1; it does not block proximity mode.

### Gate 2: gyroscope yaw drift

- Radio disabled; reset relative yaw against a fixed visible object.
- Walk five steps, stop, and turn through repeated circles.
- Measure drift and short-term repeatability across devices.

The original one-minute useful hold is a hypothesis. Use the measured error curve
to set bearing lifetime and correction cadence.

### Gate 3: place-print gradient visibility

- Walk a marked straight line in an open parking lot and in a representative
  crowd while logging all visible APs.
- Repeat in both directions and at several baseline lengths.
- Plot per-AP RSSI against step count and quantify within-walk variance.

Determine the minimum baseline and scan count needed to distinguish a trend. If
five steps are insufficient, change the interaction honestly; do not tune against
one lucky AP.

### Gate 4: proximity classifier

Collect direct RSSI and place-print similarity at known coarse separations under
line-of-sight, body obstruction, crowd, stalls, and target-motion conditions.
Choose qualitative buckets with measured confusion rates. Metres remain disabled
unless errors are acceptable across conditions, not merely on average.

### Gate 5: two-device integrated trial

At a venue-like site, start with devices together, separate them without fixed
infrastructure, start a search, and complete the find. Repeat with a stationary
target, non-walking target, moving target, sparse AP environment, lost packets,
an unanswered request, expiry, and both roles reversed.

The feature is not complete until a real two-device acceptance run demonstrates
that the prompts work and stale guidance is never mistaken for a live fix.

## Delivery phases

1. **Measurement firmware:** direct beacon/RSSI logger, IMU logger, scan logger,
   exact build provenance, and no consumer bearing claim.
2. **Proximity prototype:** requested sessions and warmer/colder direct RSSI with
   honest confidence and timeout behavior.
3. **Place-print prototype:** bounded exchange, similarity, sparse-scan scheduler,
   and gradient experiments.
4. **Bearing prototype:** only after Gate 1, fuse body-shadow sector and gyro into
   coarse left/right guidance.
5. **Product hardening:** bilingual strings, power measurement, coexistence with
   chat/local-only mode, failure recovery, emulator UI contracts,
   and exact 240x135 visual verification.
6. **Physical acceptance:** repeatable two-device venue trial before describing
   Fox Finding as working or including it in a release.

Each phase should land behind an experimental build flag until its preceding
measurement gate passes. Compilation, emulator screenshots, and synthetic RSSI
traces are useful development evidence but never substitute for physical radio
acceptance.

## Explicitly deferred ideas

- Wi-Fi FTM/802.11mc ranging, because switching one handheld into an access-point
  role conflicts with simple ESP-NOW continuity and still suffers multipath.
- A Grove magnetometer, external directional antenna, GPS, fixed anchors, relays,
  site surveys, venue maps, and server-side computation.
- Nearest-neighbour matching against a persistent place-print library. V1 compares
  the two participants' live prints; it does not build a location database.
- A rotating compass arrow or cardinal directions.

## Open decisions requiring evidence

- Whether direct receive RSSI is available cleanly in the resolved core or needs
  a promiscuous-mode metadata path.
- Whether 10 Hz produces useful samples without unacceptable power/radio cost.
- Whether body shadowing is repeatable on the Cardputer ADV's actual antenna and
  lanyard posture.
- Whether four to five metres of walking exposes any stable AP gradient in crowds.
- How quickly full scans interrupt encrypted ESP-NOW traffic in the actual stack.
- Whether the target's coarse motion state improves confidence enough to justify
  transmitting it.
- Which qualitative range labels are easiest to follow in a noisy venue.
