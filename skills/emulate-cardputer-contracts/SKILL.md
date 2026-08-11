---
name: emulate-cardputer-contracts
description: Execute device-library contracts before changing or flashing ESP32-S3 Cardputer firmware. Use for TLS/certificate failures, HTTP transport codes, WebSocket setup, M5 microphone or speaker behavior, asynchronous DMA, keyboard modifier collisions, firmware regressions, or any claim that behavior was emulated.
---

# Emulate Cardputer Contracts

Treat UI simulation and device emulation as separate test surfaces. Never claim hardware emulation from state labels, screenshots, regex-only assertions, or successful compilation.

## Required workflow

1. Identify the exact installed library/core implementation used by the build.
2. Read the relevant implementation, not only public examples.
3. Port its observable contract into an executable host test.
4. Reproduce the reported failure and its exact device error code in that test.
5. Link the test to compiled constants or source so stale mocks cannot pass.
6. When external state matters, compare against the live endpoint or artifact.
7. Run `scripts/run-contract-gate.ps1` before compilation and again before upload.

For SupaChat, the gate must cover ESP32 SHA-256 pin parsing, the live `le954.ca` leaf pin, HTTP error mapping, M5's two-slot microphone DMA delay, and Cardputer modifier/navigation precedence. Read [references/contracts.md](references/contracts.md) when editing those paths.

Do not flash if a device-only failure has not first been represented by a failing emulator test.
