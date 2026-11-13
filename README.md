# Cardputer ADV SupaChat

Persistent private mesh chat for Cardputer ADV handhelds using ESP-NOW for infrastructure-free peer-to-peer communication and optional WiFi backhaul through a Hetzner-hosted peer.

## Source provenance

A repository's currently checked-out branch or worktree is only local filesystem state and may be arbitrarily stale. It must never be treated as evidence of recent work, build provenance, authoritative source, feature scope, release intent, or authorization. Inspect dated commits across all relevant refs and SupaChat repositories for recency; use recorded ref/commit metadata for existing artifacts; and select an explicit ref and commit before choosing a clean build worktree.

> **Status (August 2026):** Text chat is working through the deployed Node/SQLite service and authenticated web portal at `https://supachat.net/`. The first walkie build adds raw-PCM voice clips and authenticated live PTT through Hetzner. Both child variants compile; physical two-device audio acceptance remains a release gate.

> **Authentication verification rule:** A created account or server-verified password is not proof that login works. Login may be called functional only after the actual released client completes Authentik authentication, returns through its redirect/deep link, exchanges the authorization result, creates a SupaChat session, maps to the intended identity, and receives the expected rooms. Otherwise, report the narrower verified fact and label end-to-end login unproven.

> **Test-readiness rule:** Do not distribute an APK for user testing until every independently testable authentication stage has passed against the release configuration. The physical-phone test is final acceptance, not the first full integration run. Startup and authentication must also fail with a visible, bounded error instead of spinning indefinitely.

## What this project is

SupaChat is intended to let a small group of family and friends communicate from dedicated Cardputer ADV handhelds:

- ESP-NOW keeps nearby devices talking without WiFi, cell service, or a central server.
- Known WiFi networks provide backhaul to a public Hetzner peer and web client.
- Messages persist across reboots, disconnections, and changing network paths.
- The initial fleet is two handhelds, with room to grow to roughly 20 devices.
- Text and voice share one conversation history; live push-to-talk uses the same identities and access controls.

The design goal is a whistle that carries farther—not a location-tracking system.

## Planned Fox Finding mode

SupaChat is planning an infrastructure-free person-finding mode for Cardputer
users at crowded venues. Fox Finding combines encrypted direct
ESP-NOW signal observations, **place printing** (live comparison of visible Wi-Fi
access points and their strengths), and short-term IMU motion. It deliberately
does not use GPS, fixed beacons, venue surveys, relays, a magnetometer, or a
server. The minimum shippable result is honest warmer/colder guidance; directional
guidance remains experimental until body-shadow bearing is proven on physical
Cardputer ADV units.

The complete design constraints, protocol proposal, measurement gates, firmware
architecture, failure behavior, and staged delivery plan are in
[`docs/fox-finding-plan.md`](docs/fox-finding-plan.md). Current reported firmware
issues and their acceptance criteria are tracked in
[`docs/firmware-fixes-backlog.md`](docs/firmware-fixes-backlog.md).

## HOME screen and apps

The device boots through the SupaChat splash to a HOME screen of apps, four to a page. Page 1 has SUPACHAT, MUSIC, BOOKS and R/C. Page 2 has the clock apps: TIMER, ALARM, SCREEN (screen time) and STOPWATCH. Page 3 has the settings: WI-FI, VOLUME, STATUS and LANGUAGE. Page 4 has CHARGING, LOCAL, CHANGELOG and SYSTEM. The arrows move between apps and on to the next page; going past the first or last page gives a bump and a falling tone. Enter or keys 1-4 open an app on the page, and key 0 opens SYSTEM. The rear button goes back one level; the top-left key does too (Fn + that key while typing).

MUSIC and BOOKS stream from Navidrome on the home LAN over the Subsonic API: plain HTTP, MP3 only, decoded with libhelix on a dedicated task (`firmware/src/media_player.cpp`, `firmware/src/music_library.cpp`). The Cardputer ADV has no PSRAM, so all audio buffers live in internal RAM and are released when playback stops. While media plays, the walkie socket closes and chat polls once a minute. Starting a walkie recording, charging mode or ESP-NOW local mode stops playback first.

**Sleep timer** (v0.60). On the now-playing screen, T sets 15, 30, 45 or 60 minutes, then off; the time left shows beside PLAYING (SLEEP, DODO in French) and on the big clock. Over its last 30 seconds the sound fades, then playback pauses with a book's place saved. A book started or resumed from 19:00 to 05:00 gets 30 minutes on its own. The runtime emulator's `book-sleep` scenario runs it.

Card files are found by `firmware/include/mp3_probe.h` (since `v0.59`): past any ID3v2 tag, cover art included, and any junk, onto two frames in a row; a Xing/Info or VBRI header gives a VBR file its real length and seek table. Streams drop a leading ID3v2 tag too. What can't play says why: `8-12 KHZ MP3: UNSUPPORTED` (MPEG 2.5: the pinned libhelix is built without it), `MP2, NOT MP3`, `NOT AN MP3`. `firmware/tools/mp3-fixtures.py` makes one file of each kind and `firmware/tools/mp3-probe-test.cpp` checks the probe against them on the PC; the runtime emulator's `card-formats` scenario plays them through the real firmware.

Card lists (books, card albums, chapters) are read with `readdir`, which allocates nothing per entry, and count only when two passes in a row agree (since `v0.59`). The core's `openNextFile` opened every entry, and one refused allocation (the app's budget, or the floor while Wi-Fi scans away from home) ended its loop as if the folder had ended: a short list offline, and on the next String move from that failed allocation, a crash. A list that can't be read whole says `CARD NOT READ: LOW RAM`. The runtime emulator's `books-flaky` scenario refuses every Nth allocation on the list task and checks the whole list or that message.

BOOKS is the Navidrome library named `Books`. Its place is saved on the server as a Subsonic bookmark every 30 seconds and on pause, and a book reopens on the chapter holding its bookmark.

Server address and login come from `firmware/include/music_secrets.h` (gitignored; see the `.example.h`). Without it, both apps show NOT SET UP and everything else works.

## Clock apps

Since `v0.56`. All four keep running whatever app is on the screen, and HOME's bottom line shows what is counting.

- **TIMER.** Type minutes and seconds (5, 0, 0 is 5:00; up to 99:59), or Left/Right for presets (1 to 60 minutes), Up/Down for a minute more or less. Enter starts and pauses, Del resets (or clears when stopped).
- **ALARM.** Up to four alarms, one a row. Each has a time and two switches, MON-FRI and SAT-SUN: both on rings every day, both off doesn't ring. Up/Down moves between rows and Left/Right between the time and the two switches; Enter flips a switch (on the time it steps to the first switch). Type the time as 24-hour digits on any column: they push in from the right from 00:00, as on a microwave (7 shows 00:07, then 3 00:73, then 0 07:30), and the line above the keys says what Enter would set. Nothing changes the alarm until the digits make a time and are confirmed by the fourth digit, Enter, or moving off the time; four digits that make no time turn red and keep the cursor there. While typing, Del takes back one digit; otherwise it deletes the alarm. The last row, + NEW ALARM, adds one at 7:00 on weekdays with Enter, or with the first digit of its time. The line above the keys says when the next one rings. Alarms are kept in NVS across restarts; the one alarm of v0.56 and v0.57 becomes the first row (a ONCE alarm comes over switched off). When one rings, Enter or the rear button stops it and any other key snoozes it for 5 minutes.
  Since v0.60 each alarm also has a SOUND column: Enter there opens the card's music (`music/` folders only, so it rings with no network), with BEEPS (NO SONG) first; the line above the keys names the song. A song plays from the start, again and again until the alarm is stopped, getting louder as the beeps do. The beeps sound for the few seconds the player needs to start (the radio goes off first, for its RAM), and take over if the file is gone or won't play.
- **SCREEN** (screen time). Arrows choose 15, 30, 45, 60, 90 or 120 minutes, Enter starts and pauses, Del stops. At 5 and 1 minutes left the header of whatever app is open turns into a warning for 10 seconds with a short chime; at zero it rings.
- **STOPWATCH.** Enter starts and stops, Del resets.

**The big clock** (v0.60). After 2 minutes without a key, once the clock is set, any screen but the walkie, the fox hunt and charging gives way to the time in 24-hour digits across the screen, the date, and the next alarm with how long until it rings (or its day, if more than a day away). From 19:00 to 05:00 the digits are orange and the screen dims to the charging screen's level. The first key or the rear button only brings back the screen underneath.

When something rings, the screen flashes red and a tone repeats until a key or the rear button stops it; the first second ignores keys so typing in progress can't stop it by accident. The tone starts at the volume setting (at least the middle step, so volume 0 doesn't silence an alarm) and climbs to full over 30 seconds. Ringing stops music or a book (its place is saved first) and brings the device out of charging mode. Three different tone patterns tell the alarm, timer and screen time apart.

What the alarm depends on:

- **The clock comes from the internet.** The Cardputer ADV has no battery-backed clock chip; until a time server answers over Wi-Fi after turning on, the device only knows its build time. The alarm then shows CLOCK NOT SET and does not ring. After one sync it keeps time without Wi-Fi (the chip's own crystal) until it is turned off or restarts. Times are Eastern (`EST5EDT`, set in `ensureTime`).
- **It rings only while the device is on.** The firmware never uses light or deep sleep, so a dark screen or charging mode doesn't stop it; the power switch, a flat battery or a restart before Wi-Fi does. A missed alarm is not rung later.
- Timers, screen time and the stopwatch run on the chip's millisecond counter and don't need the clock, but they are lost if the device restarts.

The arithmetic lives in `firmware/include/clock_apps.h`, tested on the PC by `firmware/tools/clock-apps-test.cpp`. The runtime emulator's `clock-timer`, `clock-screen`, `clock-alarm`, `clock-alarm-typing` `clock-noclock`, `clock-idle` and `alarm-song` scenarios run the real firmware through each app.

### Putting it on the devices

Each device has its own PlatformIO environment (`cardputer-adv` is Albie, `juju`, `papa`, `mama`, `emmanuelle`, `naomie`, `andrew`). Follow `skills/flash-cardputer-safely`: identify the device by MAC first, then from `firmware/`:

```
pio run -e <environment> -t upload --upload-port <port>
```

On Linux the port is `/dev/ttyACM0`; on Windows use the `COM` port that `resolve-device.ps1` returns. The upload runs the emulator gate first and doesn't touch NVS, so Wi-Fi, volume and identity stay. A device needs Wi-Fi once after it is turned on before its alarm can ring.

## Charging mode

SupaChat `v0.49` includes a dedicated low-activity charging mode inside the normal
Cardputer firmware. While active it pauses chat synchronization, Wi-Fi, ESP-NOW,
walkie/audio, notifications, and unrelated SD activity; retains only battery
sampling, a fixed-scale charge-history plot, minimal controls, and optional
battery-event logging. Exiting restores the paused subsystems and forces a normal
message reconciliation.

The source, emulator, and exact-resolution UI gates pass. Physical measurement of
charging-mode current and real-device restoration remains required before calling
the runtime behavior proven.

This mode shows how voltage and the existing percentage estimate change during a
charging session without claiming direct cable, current, or completion sensing.
A separate laboratory-style “Charge Detective” firmware may investigate controlled
rest/load measurements later, but it is not the current feature. See
[`docs/charging-mode-plan.md`](docs/charging-mode-plan.md).

The on-device `STATUS` screen now has a second diagnostics page. It retains the
most recent sync failure type and parser detail together with HTTP status,
response length, free heap, room, initial/live phase, and error age. Response
bodies are deliberately neither displayed nor retained, so message content does
not leak into diagnostics.

## MVP

The first usable release is one complete, bidirectional path between **Albie Ploss's Cardputer ADV** and the **Hetzner-hosted SupaChat web portal**.

The MVP is done when:

1. Albie's Cardputer boots reliably into SupaChat.
2. It joins an allow-listed WiFi network without embedding credentials in the repository.
3. It authenticates to the Hetzner peer over TLS.
4. A message written on the Cardputer appears in the web portal.
5. A message written in the web portal appears on the Cardputer.
6. Both sides can display conversation history after reconnecting or rebooting.
7. Messages created while the Cardputer is offline synchronize after connectivity returns.
8. The portal clearly shows delivery/synchronization state without exposing private routing history.
9. One uninterrupted real-hardware acceptance run proves the entire workflow in both directions.

The portal must provide, at minimum:

- A readable chronological conversation view
- A composer for sending text messages
- Persistent message history
- Device/user identity labels
- Connection and last-sync state
- A layout usable from both desktop and mobile browsers
- Authentication appropriate for a private family system

ESP-NOW remains a foundational requirement and is already proven on this hardware by the stock firmware. A second handheld is not required to prove the first custom-firmware vertical slice. Reimplementing direct handheld-to-handheld synchronization in SupaChat becomes the next acceptance milestone when another physical node is available.

## Firmware development approach

SupaChat will use a new ESP32-S3 application written from scratch, following the same development pattern used successfully for the earlier Raspberry Pi and Cardputer field-control system.

- Build with PlatformIO and M5Cardputer/M5Unified libraries.
- Own the application architecture, state machines, UI, storage, audio, networking, and diagnostics.
- Do not patch, fork, or depend on the stock chat firmware.
- Treat the stock firmware as proof that ESP-NOW mesh chat works on the hardware and as a behavioral reference only.
- Preserve a recovery path before replacing the stock image: record device details and obtain an official factory image or documented restore procedure.
- Establish build, flash, serial-log, and recovery workflows before feature development.
- Develop in independently testable layers rather than one monolithic sketch.

The initial custom-firmware prize is WiFi: connect Albie's node to known networks, authenticate to Hetzner, synchronize persistent messages, and expose the conversation through the web portal. ESP-NOW is then added back beneath the same transport-independent synchronization engine.

The intended implementation layers are:

1. Board support, display, keyboard, speaker, SD, and diagnostics
2. Identity and persistent append-only message log
3. Allow-listed WiFi state machine
4. Authenticated HTTPS backhaul to Hetzner
5. Bidirectional web portal and offline reconciliation
6. ESP-NOW transport and multi-node mesh behavior
7. Voice and other personality features

## Implemented MVP design

The current vertical slice deliberately starts with the smallest complete texting path:

- Papa uses a password-authenticated browser session.
- Albie's pre-registered device uses a revocable bearer token stored in NVS.
- Both identities share the `family` conversation.
- The server schema already supports `shared`, `room`, and `direct` conversation kinds.
- Messages use client-generated idempotency keys and are limited to 140 Unicode characters.
- The Cardputer synchronizes through authenticated HTTPS long-polling and keeps the latest 100 messages locally.
- SQLite retains server history indefinitely for the MVP.
- Per-participant `server`, `delivered`, and `read` receipts plus last-seen presence are modeled separately.

The service can read message content in version 1. TLS protects messages in transit; end-to-end encryption is explicitly deferred. Revoking Albie prevents new device access without deleting his history.

## Network behavior

At boot, the MVP handheld:

1. Loads the Albie device token and all known SSID/password pairs from the private `supachat` NVS namespace.
2. Loads its recent message snapshot from microSD when a card is available.
3. Scans for the strongest provisioned WiFi network and connects only when one is visible.
4. Obtains Eastern time over NTP and authenticates to Hetzner over TLS.
5. Sends queued messages, long-polls for new messages and receipts, and retains the latest 100 locally.
6. Contains a 10 p.m.–6 a.m. Eastern blackout implementation with a manual one-sync override. Enforcement is currently suspended indefinitely by `kBlackoutEnabled = false`; composing and the keypress melody remain available if it is re-enabled later.

Since `v0.57` Wi-Fi power saving (modem sleep) is on only while the server answers and the walkie is closed: modem sleep leaves the radio deaf between the access point's beacons, and ESP-NOW packets arriving then are lost. Network work stays on a separate FreeRTOS task so typing, display refresh, and melody playback stay responsive. Its Networks menu scans visible SSIDs, shows passwords during entry by design, joins the selected network, and saves up to 12 profiles in NVS. The provisioning tool can also import saved Windows profiles without printing or committing their keys.

### Messages: the outbox, the marks, and ONLINE or NEARBY

Since `v0.57`. Every typed message goes into an outbox kept in NVS (`firmware/include/outbox.h`, up to 16), whatever room it belongs to, and leaves it only when the server answers that it stored it. A missing answer (no Wi-Fi, DNS, a blocked port, TLS, a 5xx) is retried with a growing wait from 5 seconds to 5 minutes. A refusal (400, 401, 403, 409) marks the message FAILED in red; it stays until the person selects it (Fn+Up) and presses Enter. A full outbox refuses the next message and says so, leaving the text in the composer. Unsent messages are never pushed off the 40-message list, and messages left unsent on the card by older firmware join the outbox when their room loads.

Each message of this device's carries a mark at the right of its last line: a small grey clock while it waits, a yellow tick once a nearby device has acknowledged its ESP-NOW copy, a grey tick on the server, two grey ticks delivered to another device, two cyan ticks read.

The header shows how messages travel right now: **ONLINE** (white) when the server answered in the last two minutes, **NEARBY** (yellow) when only ESP-NOW is left, so texts reach only devices in radio range on the same Wi-Fi channel, and **OFFLINE** when there is neither (no mesh key). French: EN LIGNE, PROXIMITE, HORS LIGNE.

Nearby copies are broadcast at once and repeated at 1, 2, 4, 8, 16 and 30 seconds until a nearby device answers with an acknowledgement (ESP-NOW type 11). Firmware before `v0.57` neither sends nor understands acknowledgements, and ignores type 11.

### Connecting to the server, and the STATUS screen

Connections check supachat.net's certificate chain against ISRG Root X1 (the chain is leaf, Let's Encrypt YE2, ISRG Root YE, cross-signed by X2, cross-signed by X1; every signature checked with mbedTLS 2.28 on the PC, 2026-11-07). If that fails, the leaf pin `kTlsFingerprint` is tried, so a device whose clock isn't set yet still connects. Until `v0.57` the pin was the only check, and Caddy renews the certificate about every 60 days: `v0.48` pinned the August certificate, and every connection it made after the renewal of 2026-10-09 failed.

STATUS (page 1 of 3) shows, large: the mode (ONLINE, NEARBY ONLY, OFFLINE), then the step that failed: NO WI-FI, DNS FAILED, PORT 443 BLOCKED, TLS FAILED, CERT REJECTED, LOGIN REFUSED (401), SERVER HTTP n, or SERVER OK. Under it: the Wi-Fi network and signal, the detail (mbedTLS's own words, the address tried, the HTTP status), how long ago, whether the clock is set, and how many messages wait or failed. Pages 2 and 3 are the device and last-sync pages from before.

### Typing in French

Since `v0.58`. When the interface is French, chat types the Canadian French CSA layout (the one in xkb's `ca(multix)`), read from where each key sits, since the keycaps are English. The table is `firmware/include/french_keyboard.h`; `firmware/tools/french-keyboard-test.cpp` proves every character the kids need (`? ! . , ; : ' " ( ) - _ @ # /`, é è ê à â ç ù û ô î ï ë « ») and every printable ASCII character can be typed. The main moves from the keycaps: Shift+6 is `?`, `/` is é, `'` is è, `\` is à, `]` is ç, the top-left key is `/`, Shift+, and Shift+. are `'` and `"`. `[` is a dead key for ^ (then a letter: ê), Shift+`[` for ¨ and Alt+`[` for the grave accent; Space after it types the accent alone. Alt is CSA's AltGr: Alt+Z and Alt+X are « », Alt+U is ù (CSA's own ù key is missing on the Cardputer), Alt+7, 8, 9, 0 are { } [ ]. Tab in chat opens TOUCHES, a page of every combination not printed on its keycap, generated from the same table. Wi-Fi passwords and music search type the keycaps in both languages. Font0 has no capital À Â È Ê Ë Î Ï Ô Ù Û, so the screen draws those as the plain capital; the message keeps the accent.

### SupaChat's menu

SupaChat's menu holds what belongs to chat: back to chat, rooms, sync, voice messages, walkie-talkie, emoji recipes and fox finding. Up and Down move the selection, Enter opens it, and Left returns to chat. The device's settings are apps on HOME.

### On-device changelog

The CHANGELOG app on HOME shows each release. Up and Down scroll releases by firmware build number, starting with the installed build. The canonical release notes live in `firmware/include/changelog.h`; emulator contracts verify every rendered build and line against that source.

### ESP-NOW local-only mode

The LOCAL app on HOME turns on ESP-NOW local mode. Turning it on deliberately disconnects Wi-Fi, stops HTTPS/WSS work, pins the radio to the shared fallback channel, and leaves encrypted nearby text and audio active. Turning it off resumes normal Wi-Fi discovery. This operator choice is intentionally held only in RAM and always resets to off after reboot.

Custom firmware now broadcasts nearby presence and sends encrypted text or 8 kHz audio over ESP-NOW when the Hetzner walkie socket is unavailable. Two-device RF acceptance is still required before calling this path production-ready.

## Roadmap identity and message integrity

The original architecture calls for each device to generate an Ed25519 keypair on first boot and store it in persistent settings. This is not implemented in the MVP bearer-token protocol:

- The public key is the durable identity.
- A short fingerprint makes that identity readable by people.
- Display names are mutable labels, not identities.
- MAC addresses are routing hints only.

Each message envelope contains:

```text
author     32-byte public key
seq        per-author monotonic sequence number
prev_hash  hash of the author's previous message
timestamp  author's clock in milliseconds
type       message type identifier
body       type-dependent payload
signature  Ed25519 signature over the fields above
```

The hash chain makes missing or rewritten history detectable. Unknown message types must be stored and forwarded unchanged so newer features can propagate through older devices.

Planned message types include text, presence, acknowledgements, voice metadata, voice chunks, sound-pack references, and structured system messages.

## Roadmap storage

The MVP stores a compact tab-separated snapshot of the latest 100 messages on microSD. The planned long-term design moves to one append-only file per author:

```text
/log/<fingerprint>.dat
```

Each author also has a sequence-to-byte-offset index. Devices keep the highest known sequence per author and a recent-hash deduplication ring in memory.

Writes must be batched to reduce SD wear and avoid stalling future audio processing. Reads should be checksummed, and log rotation needs a deliberate design before long-term deployment.

## Roadmap transports

Text synchronization currently uses HTTPS long-polling, live voice uses authenticated WebSockets, and nearby fallback uses ESP-NOW. The synchronization engine is intended to grow toward a shared transport interface:

```text
send(peer_hint, bytes) -> queued | rejected
on_receive(callback)
available() -> bool
mtu() -> int
```

Initial transports:

| Transport | Expected MTU | Purpose |
|---|---:|---|
| ESP-NOW v2 | 1470 bytes | Local discovery, flooding, and targeted sync |
| ESP-NOW v1 fallback | 250 bytes | Compatibility with older Arduino/ESP-IDF stacks |
| WebSocket over TLS | About 64 KB | Hetzner backhaul and web synchronization |
| LoRa | About 200 bytes | Possible future extension |

ESP-NOW discovery and flooding use broadcast; targeted synchronization uses unicast. Flooding is limited to three hops and deduplicated by message hash. Broadcast has no radio-level encryption, so the application must not rely on ESP-NOW's built-in encryption for privacy.

## Hetzner peer and web client

The deployed hosted stack is deliberately small:

- Caddy for TLS and reverse proxying
- A native Node service using built-in HTTP and SQLite support, isolated by systemd
- SQLite for initial storage
- A responsive static web client using server-sent events, with polling fallback for presence

Cardputer devices are pre-registered by an operator and keep separate bearer credentials. Web and Android members enroll through one-time Authentik invitations targeted to a specific room. Administrators can create rooms, manage membership, review safety reports, and process account-deletion requests from the web portal.

Planned admin feature: edit each member's room/group-specific display alias in the web portal, while preserving one stable account identity across groups.

Planned emoji feature: recognize an initial, documented set of punctuation shortcuts (for example `:)`, `;)`, and `<3`) as canonical emoji while composing messages. Android/web clients will render the corresponding Unicode emoji; Cardputer firmware will use compact built-in bitmap glyphs where practical and preserve a readable punctuation fallback for unknown or unsupported emoji. Keep the shortcut table and wire representation extensible so the supported emoji set can grow over time without breaking older clients.

Public Play-policy pages are served at `/privacy`, `/terms`, and `/delete-account`. Interactive web and Android users must accept the current community standards before posting; managed Cardputer device credentials retain their existing protocol. Message reporting, user blocking, and deletion requests are stored in SQLite and exposed to the administrator safety queue.

Version 1 is expected to use TLS without end-to-end message encryption. A shared group-key design may be added later, but its browser key management and lost-device recovery costs must be resolved first.

No plaintext secrets belong in this repository. Runtime secrets must use environment variables; committed examples may describe variable names but must never contain live values.

## Walkie mode

The first working slice deliberately uses 8 kHz, 16-bit, mono PCM. This makes microphone, speaker, authorization, storage, and half-duplex behavior testable before introducing codec complexity. A held PTT session can run for 30 seconds; its first five seconds stream to microSD as a retained voice message. Without a card, a one-second RAM fallback remains available. Moving the full clip out of RAM reduced static use from 40% to about 21% of the ESP32-S3's 320 KB, preserving heap for concurrent HTTPS and WebSocket TLS.

The current path is:

```text
Cardputer mic -> 20 ms PCM blocks -> authenticated WSS -> Papa/Cardputer speaker
                                      |
                                      +-> five-second stored voice message

No usable WSS -> level control -> IMA ADPCM, 30 ms a packet -> AES-256-GCM -> ESP-NOW broadcast, each packet sent twice
               -> nearby Cardputer: 90 ms jitter buffer, lost frames covered -> speaker, 60 ms blocks
```

Since `v0.57` (`firmware/include/voice_codec.h`, tested by `firmware/tools/messaging-test.cpp`). Before, nearby audio was raw PCM in 71-sample pieces, 150 broadcast packets a second with no acknowledgement, each played the moment it landed with about 18 ms queued at the speaker: every lost packet and every main-loop stall longer than 18 ms was a hole, which is the rough, choppy sound. Now one packet carries 30 ms (128 bytes of ADPCM, 26 dB SNR on a test signal), each goes out again with the next, the receiver waits 90 ms before playing, puts frames back in order, and covers a missing frame by repeating the last one at half level, twice, then silence. The first talker keeps the channel until they stop. Walkie-socket audio (and raw audio from older firmware) waits for a 240 ms cushion. The microphone gain went from 48 to M5's default 16, and a level control (DC blocker, slow gain up to 6x, fast gain down, limiter at 30000) follows it.

What software can't change: the Cardputer's small chip antenna and the hand around it set the range; ESP-NOW broadcasts go out at 1 Mbit/s, the most robust 2.4 GHz rate, without retries; devices on different Wi-Fi channels (one joined to a network, one not) can't hear each other at all; the speaker is small and the audio 8 kHz. ESP-NOW's long-range mode (250 or 500 kbit/s) could reach further, but only between devices that all have it on, and nothing here has measured it; it is not enabled.

On the Cardputer, open **WALKIE** from the large menu and hold the large Space key to talk. Release to stop; Enter replays the locally retained clip. Left returns to the menu, and the rear button remains an optional back shortcut. The screen distinguishes `HETZNER`, `ESP-NOW NEARBY`, and `ESP-NOW WAITING`. Speaker volume and mute use the existing Volume menu.

In the Papa portal, **Hold to talk** opens the same authenticated WebSocket channel. **Record voice clip** creates a retained message that can be played by any authorized participant.

Transport rules:

- Hetzner is preferred whenever its authenticated WebSocket is connected.
- Only one participant owns the channel at a time; others receive a busy state.
- ESP-NOW is an encrypted fallback, not a simultaneous second feed.
- Nearby discovery metadata is broadcast every 30 seconds. Audio and text payloads require the provisioned family AES-256-GCM key.
- ESP-NOW peers must be on the same WiFi radio channel. Cross-channel discovery is future work.
- Speaker and microphone never run simultaneously because the Cardputer audio hardware requires explicit handoff.

Nothing here has been heard on a device yet: the codec, jitter buffer, concealment and level control run only in the PC test, and ESP-NOW isn't modelled by the runtime emulator.

The repeatable software, provisioning, hosted-PTT, nearby-radio, audio, and viewport gates are recorded in [`docs/walkie-acceptance.md`](docs/walkie-acceptance.md).

## Sound packs

User-customizable WAV files will be loaded from microSD without rebuilding or reflashing:

```text
/sounds/<author-fingerprint>/message.wav
/sounds/<author-fingerprint>/ptt-start.wav
/sounds/default/queued.wav
/sounds/default/delivered.wav
```

This is an early, inexpensive feature intended to make each handheld feel personal.

### Sequential keypress song

Chat mode will also use a bundled MIDI melody as a playful typing sound. Each accepted text keypress advances to the next audible note event in the song and plays that note through the Cardputer speaker. Repeated typing therefore reveals the melody in sequence rather than triggering unrelated beeps.

The source asset is stored at:

```text
firmware/assets/music/keypress-song.mid
```

Expected behavior:

- Only keypresses that insert or delete chat text advance the melody.
- Navigation, power, mode-switching, and push-to-talk controls do not consume notes.
- Chords are treated as one musical step and played together when the hardware audio path permits it.
- Enforce at least 40 ms between speaker events and use short, non-blocking tones; rapid events inside that window are silently skipped rather than queued.
- A rate-limited keypress does not advance the melody, so skipped audio never skips part of the song.
- Note duration comes from the MIDI but is capped so typing never feels blocked. Start with the factory-proven 20 ms key-tone duration and tune on hardware.
- A new keypress may interrupt the previous note or chord.
- Reaching the end wraps back to the first musical step.
- Leaving and returning to chat mode keeps the current position for the active session.
- Reboot behavior should be configurable; version 1 may restart from the beginning.
- The feature can be muted independently of message and delivery chirps.

The firmware build converts the source MIDI's 53-note verse and the complete 78-note Hooktheory chorus into one compact, flash-friendly table with precomputed frequencies. The source MIDI ends during the chorus, so the converter detects that partial chorus opening and replaces its truncated tail with the complete transcription. The handheld does not need a general-purpose MIDI parser or repeated floating-point note conversion at runtime.

Keyboard capture must run on every `M5Cardputer.update()` loop. Display rendering should be independently throttled to roughly 20 fps so rapid typing and audio feedback cannot stall or flicker the chat UI.

### Boot splash

The Cardputer starts with the supplied retro arcade logo, converted reproducibly to a 240×135 RGB565 image by `firmware/tools/make-splash-header.py`. A four-second, 16-step chiptune phrase and slim progress sweep borrow MiLFFINDER's proven jaunty boot pattern while keeping SupaChat's path to WiFi shorter. The splash pixels live in program flash rather than a runtime framebuffer.

The bottom-right boot label comes from the firmware identity rather than the shared image. The default `cardputer-adv` environment builds Albie's node; `pio run -e juju` builds the second Juju node with its own display name and device ID.

TLS uses a pinned `supachat.net` leaf-certificate fingerprint because the Cardputer's embedded CA path does not currently validate Caddy's Let’s Encrypt chain. The pin must be refreshed whenever Caddy rotates the certificate.

## Prior Cardputer lessons

Relevant, hardware-tested guidance is maintained in the sibling `dev-lore` repository at `cardputer-pi-inky-lessons.md`. The hosted side also draws on `dev-lore/private-authenticated-web-portal-playbook.md`. SupaChat should carry these lessons forward:

- Use PlatformIO with a dedicated `cardputer-adv` environment for reproducible builds.
- Initialize the ADV microSD bus explicitly as SCK 40, MISO 39, MOSI 14, and CS 12. Default ESP32-S3 SPI pins reuse GPIO11 and disable the TCA8418 keyboard interrupt.
- Treat `Hash of data verified.` from Esptool as required flash verification, not optional output.
- Track RAM and flash percentages throughout development; store large immutable tables in program flash rather than RAM.
- Precompute MIDI note frequencies instead of calculating them on every keypress.
- Keep key capture, display rendering, audio output, transport synchronization, and storage flushing as separate scheduling concerns.
- Model WiFi and backhaul transitions as event-driven state machines with bounded retries, not synchronized delays.
- Expose distinct connection evidence—SSID visibility, association, address assignment, TLS/WebSocket authentication, and sync state—instead of one ambiguous connected flag.
- Preserve the working ESP-NOW path until the replacement network path has actually been observed as usable.
- Store provisioned credentials in a dedicated NVS namespace and never commit them.
- Do not ask for repeated physical tests without adding new diagnostics or independently verifying the surrounding systems first.
- Call a feature complete only after one uninterrupted, real-hardware acceptance run proves the full user-facing workflow.
- Keep hosted secrets and runtime state outside the disposable application tree.
- Expose an intentionally unauthenticated `/healthz` endpoint while keeping device and web synchronization authenticated.
- Harden the Hetzner host before rollout, expose only the required ports, and verify deployments against the running service rather than trusting copied files or script output.
- Choose the lightest deployment tier that makes the live service correct; do not rebuild the entire hosted stack for a static web-client change.

Cardputer Advanced USB programming and normal application runtime were mutually exclusive in the prior project. Provisioning and test plans must assume credentials may need to be flashed into NVS while the device is off rather than streamed to a running application.

## Build phases

| Phase | Deliverable | Completion test |
|---|---|---|
| 0 — Foundation | Stock recovery path plus hardware, speaker, storage, WiFi, and ESP-NOW validation sketches | Albie's node has measured behavior and repeatable build, flash, serial-log, and recovery workflows |
| 1 — MVP spine | Registered identity, recent local history, WiFi state machine, Hetzner peer, web portal | Albie's Cardputer and the portal exchange text both ways and retain history after reboot |
| 2 — Direct mesh | ESP-NOW discovery, flooding, and synchronization | Two handhelds exchange text without infrastructure and reconcile later through Hetzner |
| 3 — Chirps | Sound-pack loading | A child changes a sound without help or a firmware rebuild |
| 4 — Memos | Delayed voice | A five-second memo survives going out of range and returning |
| 5 — Live | ESP-NOW push-to-talk | Two-way voice works reliably in the apartment |
| 6 — Relay voice | Live audio through Hetzner | Voice works between separate WiFi locations |
| Later | LoRa, Android client, fixed relay nodes | Scoped after the core system is proven |

Phase 0 is mandatory. Datasheet range and battery estimates are not substitutes for measurements with the actual Cardputer ADV hardware in real locations.

## Immediate next steps

1. Confirm the on-device TCA8418 self-test and perform a full keyboard, melody, arrow, OK, and rear-button acceptance pass.
2. Complete one uninterrupted Papa-to-Albie and Albie-to-Papa production exchange with delivery/read receipts.
3. Prove reboot persistence and offline queue reconciliation on the physical Cardputer.
4. Run the hosted and nearby walkie audio acceptance checklist on both physical devices.
5. Measure an ordinary day of battery use; tune sync cadence only from real evidence.
6. Flash and provision Juju with the same family mesh key, then validate encrypted nearby text and PTT with WiFi unavailable.
7. Measure latency, packet loss, intelligibility, and battery life; only then choose the Codec2 mode and jitter-buffer budget.

## Principal risks

- Cardputer ADV library support may lag behind the original Cardputer.
- Older Arduino cores reduce the ESP-NOW payload limit to 250 bytes.
- Continuous ESP-NOW listening may consume too much battery.
- SD write patterns may cause stalls or premature wear.
- Radio transmit bursts may leak into the audio path.
- WiFi association temporarily reduces direct ESP-NOW reachability.

These are measurement and engineering risks, not reasons to weaken the offline-first architecture.

## Privacy principles

- Relays must not log who they heard or when; transient routing state stays in memory.
- Hosted storage must contain only what synchronization requires.
- The children using the devices should understand what the system records and does not record.
- Privacy claims in the interface and documentation must remain literally true.

## Planned repository layout

```text
cardputer-adv-supachat/
  README.md
  docs/
    architecture.md
    protocol.md
    decisions/
  firmware/
  server/
  web/
```

Files and directories use lowercase hyphenated names. Commits should be lowercase, imperative, and limited to one logical change. Documentation should preserve the reasons behind decisions, not merely describe the resulting code.

## License

No license has been selected yet. Until one is added, copyright remains with the repository owner.
