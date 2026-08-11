---
name: collect-cardputer-runtime-feedback
description: Obtain valid physical-runtime feedback from M5 Cardputer ADV devices after builds or flashes. Use when interpreting black screens, COM visibility, boot behavior, sync status, keyboard input, tones, microphone playback, Wi-Fi, or any result that requires the device to run outside download mode.
---

# Collect Cardputer Runtime Feedback

Treat these facts as authoritative:

- The Cardputer is visible over USB/COM only in download mode in this workflow.
- Its display is black in download mode.
- Therefore COM visibility and a black screen provide no application-runtime evidence.

After every flash:

1. Confirm hashes only.
2. Explicitly ask the user to unplug USB and cycle the side power.
3. Ask for exact visible status text and one narrowly defined interaction.
4. Separate observations: boot, Wi-Fi, HTTPS sync, WSS, keyboard, tones, recording, local replay, and remote playback.
5. Do not reinterpret or contradict the user's physical observation.
6. Add any new device-library failure to `$emulate-cardputer-contracts` before the next flash.

Use the checklist in [references/feedback-checklist.md](references/feedback-checklist.md).
