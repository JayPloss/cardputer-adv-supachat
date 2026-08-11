# Walkie acceptance

Walkie mode is released only after one uninterrupted pass records every check below. Builds and automated tests establish software readiness; they do not substitute for listening to the physical devices.

## Automated gate

```powershell
py -m platformio run --project-dir firmware -e cardputer-adv
py -m platformio run --project-dir firmware -e juju
node --test server/test/server.test.mjs
$env:SUPACHAT_DEVICE_TOKEN = '<private device token>'
node server/test/production-smoke.mjs
Remove-Item Env:SUPACHAT_DEVICE_TOKEN
```

Both firmware builds must report `SUCCESS`, every Node test must pass, and the production probe must report `tls=true ptt=true` without printing the token.

## Provision and flash

Confirm identity by MAC before writing:

- Albie: `28:84:85:75:75:a0`
- Juju: `28:84:85:75:5e:fc`

```powershell
pwsh -File tools/provision-albie.ps1 -Port COM4 -DeviceKey albie
pwsh -File tools/provision-albie.ps1 -Port COM5 -DeviceKey juju
py -m platformio run --project-dir firmware -e cardputer-adv -t upload --upload-port COM4
py -m platformio run --project-dir firmware -e juju -t upload --upload-port COM5
```

COM numbers are examples. Never infer identity from the port number alone. Require `Hash of data verified.` for every written region. Native USB may remain absent after upload reset; physically power-cycle the Cardputer instead of toggling DTR/RTS from a serial monitor.

## Hosted PTT

1. Power-cycle both devices and wait for `SYNCED` and `HETZNER`.
2. On Albie, open Menu → Walkie. Hold Space, speak for three seconds, then release.
3. Verify Juju hears intelligible audio once, without echo, duplication, clipping, or a stuck channel.
4. Repeat Juju → Albie, Papa → both devices, and both devices → Papa.
5. Press Enter on each Cardputer and verify its retained clip replays.
6. Play each retained message in the portal and verify speaker and duration.
7. Attempt simultaneous PTT. Exactly one participant must own the channel; the other must see busy.
8. Disconnect the active speaker mid-PTT. The channel must return to ready within 31 seconds.
9. Verify Mute is silent and Low, Medium, High, and Max increase audibly.

## Nearby encrypted fallback

1. Put both devices on the same 802.11 radio channel, then make Hetzner unavailable without erasing NVS.
2. Wait up to 65 seconds for `ESP-NOW NEARBY` on both screens.
3. Send one text each direction and verify it reconciles only once after internet returns.
4. Test three-second PTT in each direction; each transmission must play exactly once.
5. Verify a device with a different family key cannot decrypt content.
6. Replay a captured encrypted packet and verify the receiver drops it.
7. Restore WiFi and verify queued content reconciles once and Hetzner again becomes preferred.

## Portal layout

At 390×844 and 1440×900, verify login, history, composer, voice controls, presence, and PTT have no overflow or overlap. PTT must release on pointer-up, cancellation, or window blur.

Record the date, devices, firmware commit, network conditions, latency, intelligible range, and battery delta. Any failed item keeps walkie mode pre-release.
