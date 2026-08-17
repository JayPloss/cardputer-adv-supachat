# SupaChat device contracts

- ESP32 Arduino 2.0.16 `WiFiClientSecure::verify` parses exactly 32 fingerprint bytes and hashes the peer certificate with SHA-256. A 20-byte SHA-1 pin produces the firmware-mapped `-5` failure.
- The compiled pin must equal the live leaf certificate served for `supachat.net`, including after Caddy renewal.
- M5 microphone recording is asynchronous. Follow the working M5 three-slot ring and consume two slots behind the active DMA target.
- Shifted printable keys take precedence over physical arrow roles. In particular, Shift+`/` must emit `?`, not navigate right.
- ESP-NOW is currently disabled. Do not model it as an available fallback.

