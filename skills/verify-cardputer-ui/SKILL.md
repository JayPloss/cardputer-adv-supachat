---
name: verify-cardputer-ui
description: Render and visually verify Cardputer firmware interfaces at their exact display resolution. Use for 240x135 layouts, menus, chat screens, status labels, clipped text, navigation affordances, boot art, visual regressions, or emulator screenshots. Do not use this skill as evidence for ESP32, M5, network, audio, or TLS behavior.
---

# Verify Cardputer UI

Keep this workflow independent from device-contract emulation.

1. Render at the physical 240×135 resolution with pixel scaling disabled.
2. Mirror firmware coordinates, truncation budgets, colors, and screen states.
3. Exercise Chat, Menu, Walkie, Volume, Networks, password entry, Status, and boot art.
4. Capture each screen, including error and active states.
5. Run the UI visual-integrity measurements at narrow and desktop host viewports.
6. Inspect captures for clipping, overlap, illegible labels, and controls outside the frame.
7. Report UI results only. Never infer hardware input, audio, TLS, Wi-Fi, or DMA success.

For SupaChat, run `server/test/emulator-visual.mjs` and the visual-integrity scripts. See [references/screen-budget.md](references/screen-budget.md).
