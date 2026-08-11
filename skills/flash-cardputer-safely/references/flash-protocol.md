# Flash protocol

- USB/COM visibility means the device is in download mode for this workflow.
- Download mode has a black display.
- Application upload writes bootloader, partition table, boot app, and application image. NVS is separate at `0x9000`.
- Provision with `tools/provision-albie.ps1 -Port <port> -DeviceKey albie|juju|papa` only when required.
- Preserve user data and identities; never erase the whole flash unless explicitly authorized.
