# SupaChat Cardputer emulator

This 240×135 browser emulator mirrors the firmware's critical chat, menu, walkie, Space-key press/release, timeout, synchronization-error, and clip-upload states. Its deterministic test also models the asynchronous microphone DMA boundary, verifies audible local replay from completed non-zero buffers, exercises build-clock TLS fallback when NTP is unavailable, and checks the corresponding firmware source invariants.

Run `python -m http.server 8877` from the repository root and open `http://127.0.0.1:8877/emulator/`. The browser surface is only the UI simulator. Run `node emulator/test-flow.mjs` for state/source checks and `node emulator/device-contract.mjs` for executable ESP32/M5/Cardputer library contracts plus the live `supachat.net` certificate-pin check. Firmware must not be flashed unless both pass.
