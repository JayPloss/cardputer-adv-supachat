# Cardputer ADV SupaChat

Persistent private mesh chat for Cardputer ADV handhelds using ESP-NOW for infrastructure-free peer-to-peer communication and optional WiFi backhaul through a Hetzner-hosted peer.

> **Status:** Planning and hardware validation. No production firmware has been implemented yet.

## What this project is

SupaChat is intended to let a small group of family and friends communicate from dedicated Cardputer ADV handhelds:

- ESP-NOW keeps nearby devices talking without WiFi, cell service, or a central server.
- Known WiFi networks provide backhaul to a public Hetzner peer and web client.
- Messages persist across reboots, disconnections, and changing network paths.
- The initial fleet is two handhelds, with room to grow to roughly 20 devices.
- Text chat comes first; voice memos and push-to-talk are later goals.

The design goal is a whistle that carries farther—not a location-tracking system.

## Core design

Transport is treated as a pipe, not a mode. Each author owns a signed, numbered, append-only message log. Peers synchronize by exchanging the highest sequence number they hold for each author and requesting anything newer.

The same synchronization operation can run over ESP-NOW, WiFi, a future LoRa transport, or even USB. Duplicate delivery is harmless because a message always lands in the same author-and-sequence slot.

The Hetzner machine is another peer rather than the source of truth. It differs from a handheld only because it is always online and exposes a web interface. If it goes down, handheld communication and local history continue normally and reconcile later.

## Network behavior

At boot, a handheld:

1. Loads or generates its cryptographic identity.
2. Mounts the microSD card and opens its local logs.
3. Starts ESP-NOW on a fixed rendezvous channel.
4. Periodically scans for an allow-listed WiFi network.
5. Connects to the Hetzner peer over a TLS WebSocket when known WiFi is available.
6. Returns naturally to direct ESP-NOW reachability after WiFi disassociation.

An ESP32-S3 associated with WiFi is pinned to the access point's channel and may be unable to hear ESP-NOW peers on the rendezvous channel. SupaChat deliberately accepts this limitation. While connected, traffic can take the longer route through Hetzner; away from known WiFi, the device remains on the rendezvous channel for direct peer-to-peer communication.

ESP-NOW remains initialized through WiFi transitions. The interface should describe who is reachable, not force a child to understand which radio path is active.

## Identity and message integrity

Each device generates an Ed25519 keypair on first boot and stores it in persistent settings:

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

## Storage

Message history lives on microSD as one append-only file per author:

```text
/log/<fingerprint>.dat
```

Each author also has a sequence-to-byte-offset index. Devices keep the highest known sequence per author and a recent-hash deduplication ring in memory.

Writes must be batched to reduce SD wear and avoid stalling future audio processing. Reads should be checksummed, and log rotation needs a deliberate design before long-term deployment.

## Transports

The synchronization engine will use a shared transport interface:

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

The planned hosted stack is deliberately small:

- Caddy for TLS and reverse proxying
- A Go or Node service implementing the same synchronization protocol as a handheld
- SQLite for initial storage
- A static single-page web client using WebSocket synchronization

Devices are pre-registered by an operator. Provisioning uses a one-time token displayed on the Cardputer and entered through the web interface. The hosted peer verifies every signed message before storing it.

Version 1 is expected to use TLS without end-to-end message encryption. A shared group-key design may be added later, but its browser key management and lost-device recovery costs must be resolved first.

No plaintext secrets belong in this repository. Runtime secrets must use environment variables; committed examples may describe variable names but must never contain live values.

## Voice roadmap

Voice is a stretch goal built on Codec2 and a streaming audio pipeline. The Cardputer ADV has 512 KB of internal RAM and no external PSRAM, so raw recordings must never be buffered in full.

The required pipeline is:

```text
microphone -> short PCM block -> Codec2 encoder -> SD or transmit queue
```

Voice will be developed in two steps:

1. **Voice memos:** encode continuously while a key is held, store to SD, then synchronize metadata and resumable chunks like any other message.
2. **Live push-to-talk:** batch short Codec2 frames into best-effort ESP-NOW packets with a small jitter buffer and no retransmission.

The intended user experience is one button. The device attempts live delivery first and silently falls back to a voice memo when no peer responds quickly enough. A release chirp communicates which path was used.

## Sound packs

User-customizable WAV files will be loaded from microSD without rebuilding or reflashing:

```text
/sounds/<author-fingerprint>/message.wav
/sounds/<author-fingerprint>/ptt-start.wav
/sounds/default/queued.wav
/sounds/default/delivered.wav
```

This is an early, inexpensive feature intended to make each handheld feel personal.

## Build phases

| Phase | Deliverable | Completion test |
|---|---|---|
| 0 — Measure | Throwaway ESP-NOW range and RSSI sketch | Real range and battery behavior are known in the places the devices will be used |
| 1 — Spine | Identity, signed logs, SD storage, ESP-NOW sync | Two handhelds exchange text and retain history after reboot |
| 2 — Backhaul | WiFi state machine, Hetzner peer, web client | An offline message appears on the web after WiFi returns |
| 3 — Chirps | Sound-pack loading | A child changes a sound without help or a firmware rebuild |
| 4 — Memos | Delayed voice | A five-second memo survives going out of range and returning |
| 5 — Live | ESP-NOW push-to-talk | Two-way voice works reliably in the apartment |
| 6 — Relay voice | Live audio through Hetzner | Voice works between separate WiFi locations |
| Later | LoRa, Android client, fixed relay nodes | Scoped after the core system is proven |

Phase 0 is mandatory. Datasheet range and battery estimates are not substitutes for measurements with the actual Cardputer ADV hardware in real locations.

## Immediate next steps

1. Verify M5Unified support for the Cardputer ADV variant.
2. Confirm the Arduino core and ESP-IDF version provide ESP-NOW v2's 1470-byte payloads.
3. Build the phase-0 range sketch and test RSSI, packet loss, and battery life.
4. Record the rendezvous channel and initial wire-format decisions.
5. Implement identity generation and persistence.
6. Implement append-only SD logs and indexes.
7. Synchronize text messages between two handhelds over ESP-NOW.

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
