---
name: flash-cardputer-safely
description: Identify, build, provision, and flash M5 Cardputer ADV devices without swapping identities or skipping emulator gates. Use when a Cardputer is connected, COM ports change, firmware must be uploaded, NVS must be provisioned, Albie or Julien/Juju must be selected, or the user asks to flash or catch up a device.
---

# Flash Cardputer Safely

For identification without a build, provision, or flash request, use
`../identify-cardputer-device/SKILL.md` and stop there. The release and emulator
gates below apply to firmware mutation, not to a read-only identity lookup.

## Hard gates

1. Run `$emulate-cardputer-contracts` first.
2. List serial devices and identify the MAC. Do not select by remembered COM port alone.
3. Map only known identities:
   - Albie: `28:84:85:75:75:a0`, environment `cardputer-adv`.
   - Julien/Juju: `28:84:85:75:5e:fc`, environment `juju`.
4. Stop on an unknown or ambiguous MAC.
5. Compile the exact environment before upload.
6. Upload only to the currently resolved port and require all esptool hashes to verify.
7. Provision NVS only from the private credential source; never print or commit secrets.
8. Never claim powered-runtime success from upload output.

Use `scripts/resolve-device.ps1` for identity resolution. Read [references/flash-protocol.md](references/flash-protocol.md) before changing partitions or provisioning.
