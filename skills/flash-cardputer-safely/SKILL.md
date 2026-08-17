---
name: flash-cardputer-safely
description: Identify, build, provision, and flash M5 Cardputer ADV devices without swapping identities or skipping emulator gates. Use when a Cardputer is connected, COM ports change, firmware must be uploaded, NVS must be provisioned, Albie or Julien/Juju must be selected, or the user asks to flash or catch up a device.
---

# Flash Cardputer Safely

## Hard gates

1. Run `$release-supachat-safely` first and record the authorized source ref and commit. Default to `origin/main`; compilation or a current feature checkout does not authorize flashing it.
2. Run `$emulate-cardputer-contracts` first.
3. List serial devices and identify the MAC. Do not select by remembered COM port alone.
4. Map only known identities:
   - Albie: `28:84:85:75:75:a0`, environment `cardputer-adv`.
   - Julien/Juju: `28:84:85:75:5e:fc`, environment `juju`.
5. Stop on an unknown or ambiguous MAC.
6. Compile the exact environment from the authorized clean worktree before upload.
7. Before upload, print the source ref, commit, included features, and explicit exclusions. Stop if the artifact includes an unapproved feature branch.
8. Upload only to the currently resolved port and require all esptool hashes to verify.
9. Provision NVS only from the private credential source; never print or commit secrets.
10. Never claim powered-runtime success from upload output.

Use `scripts/resolve-device.ps1` for identity resolution. Read [references/flash-protocol.md](references/flash-protocol.md) before changing partitions or provisioning.
