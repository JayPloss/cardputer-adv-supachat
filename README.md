# Cardputer ADV SupaChat

Persistent private mesh chat for Cardputer ADV handhelds using ESP-NOW for infrastructure-free peer-to-peer communication and optional WiFi backhaul through a Hetzner-hosted peer.

> **Status (August 2026):** Text chat is working through the deployed Node/SQLite service and authenticated web portal at `https://supachat.net/`. The first walkie build adds raw-PCM voice clips and authenticated live PTT through Hetzner. Both child variants compile; physical two-device audio acceptance remains a release gate.

## What this project is

SupaChat is intended to let a small group of family and friends communicate from dedicated Cardputer ADV handhelds:

- ESP-NOW keeps nearby devices talking without WiFi, cell service, or a central server.
- Known WiFi networks provide backhaul to a public Hetzner peer and web client.
- Messages persist across reboots, disconnections, and changing network paths.
- The initial fleet is two handhelds, with room to grow to roughly 20 devices.
- Text and voice share one conversation history; live push-to-talk uses the same identities and access controls.

The design goal is a whistle that carries farther—not a location-tracking system.

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

The current vertical slice supports private multi-room messaging:

- Papa uses a password-authenticated browser session.
- Albie's pre-registered device uses a revocable bearer token stored in NVS.
- Membership independently gates every room; requests without an explicit room are rejected.
- The server schema already supports `shared`, `room`, and `direct` conversation kinds.
- Messages use client-generated idempotency keys and are limited to 140 Unicode characters.
- The Cardputer synchronizes through authenticated HTTPS polling and keeps a separate 100-message SD snapshot per room.
- SQLite retains server history indefinitely for the MVP.
- Per-participant `server`, `delivered`, and `read` receipts plus last-seen presence are modeled separately.

The service can read message content in version 1. TLS protects messages in transit; end-to-end encryption is explicitly deferred. Revoking Albie prevents new device access without deleting his history.

## Messaging and duels

- New messages are limited to one per user per second at the server; idempotent retries remain safe.
- Web and Android support replies, reactions, edits/deletes, typing state, unread room counts, and notification deep-links.
- Cardputer renders replies/reactions/edits/deletes, sends `/r your reply` to reply to the latest incoming message, and sends `/like` to toggle a thumbs-up on it.
- A room member starts a duel by sending `duel Name`; the other member sends the reciprocal command to accept.
- Active Cardputer duels use `1` Protego, `2` Sectum Sempra, `3` Levicorpus, and `4` Langlock. Choices stay secret until both players lock a spell; first to two points wins.
- Completed results remain visible independently for both players until each acknowledges them. Web and Android use **Done**; Cardputer uses Enter.
- Challengers can cancel, opponents can decline, and unanswered challenges expire after 24 hours without blocking future duels.

## Network behavior

At boot, the MVP handheld:

1. Loads the Albie device token and all known SSID/password pairs from the private `supachat` NVS namespace.
2. Loads its recent message snapshot from microSD when a card is available.
3. Scans for the strongest provisioned WiFi network and connects only when one is visible.
4. Obtains Eastern time over NTP and authenticates to Hetzner over TLS.
5. Sends queued messages, long-polls for new messages and receipts, and retains the latest 100 locally.
6. Contains a 10 p.m.–6 a.m. Eastern blackout implementation with a manual one-sync override. Enforcement is currently suspended indefinitely by `kBlackoutEnabled = false`; composing and the keypress melody remain available if it is re-enabled later.

The firmware enables WiFi power saving and keeps network work on a separate FreeRTOS task so typing, display refresh, and melody playback stay responsive. Its Networks menu scans visible SSIDs, shows passwords during entry by design, joins the selected network, and saves up to 12 profiles in NVS. The provisioning tool can also import saved Windows profiles without printing or committing their keys.

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

Devices are pre-registered by an operator. The current tool writes Albie's hashed server credential counterpart and private device token separately, then provisions the device token and known networks directly into NVS. Full user/device administration and one-time interactive enrollment are later portal features.

Version 1 is expected to use TLS without end-to-end message encryption. A shared group-key design may be added later, but its browser key management and lost-device recovery costs must be resolved first.

No plaintext secrets belong in this repository. Runtime secrets must use environment variables; committed examples may describe variable names but must never contain live values.

## Walkie mode

The first working slice deliberately uses 8 kHz, 16-bit, mono PCM. This makes microphone, speaker, authorization, storage, and half-duplex behavior testable before introducing codec complexity. A held PTT session can run for 30 seconds; its first five seconds stream to microSD as a retained voice message. Without a card, a one-second RAM fallback remains available. Moving the full clip out of RAM reduced static use from 40% to about 21% of the ESP32-S3's 320 KB, preserving heap for concurrent HTTPS and WebSocket TLS.

The current path is:

```text
Cardputer mic -> 20 ms PCM blocks -> authenticated WSS -> Papa/Cardputer speaker
                                      |
                                      +-> five-second stored voice message

No usable WSS -> AES-256-GCM packet -> ESP-NOW broadcast -> nearby Cardputer
```

On the Cardputer, open **WALKIE** from the large menu and hold the large Space key to talk. Release to stop; Enter replays the locally retained clip. Left returns to the menu, and the rear button remains an optional back shortcut. The screen distinguishes `HETZNER`, `ESP-NOW NEARBY`, and `ESP-NOW WAITING`. Speaker volume and mute use the existing Volume menu.

In the Papa portal, **Hold to talk** opens the same authenticated WebSocket channel. **Record voice clip** creates a retained message that can be played by any authorized participant.

Transport rules:

- Hetzner is preferred whenever its authenticated WebSocket is connected.
- Only one participant owns the channel at a time; others receive a busy state.
- ESP-NOW is an encrypted fallback, not a simultaneous second feed.
- Nearby discovery metadata is broadcast every 30 seconds. Audio and text payloads require the provisioned family AES-256-GCM key.
- ESP-NOW peers must be on the same WiFi radio channel. Cross-channel discovery is future work.
- Speaker and microphone never run simultaneously because the Cardputer audio hardware requires explicit handoff.

Codec2, SD-backed longer memos, packet-loss concealment, and measured battery tuning are the next audio iterations—not prerequisites for validating this slice.

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

The firmware build should convert the MIDI into a compact, flash-friendly note-event table with precomputed frequencies. The handheld does not need a general-purpose MIDI parser or repeated floating-point note conversion at runtime.

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
