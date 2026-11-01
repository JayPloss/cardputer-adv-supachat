#!/bin/bash
# Shows why a Cardputer last crashed, from the core dump in its flash. The board
# must be in download mode. Run from the repo root, with the matching build in place.
set -euo pipefail
env=${1:-cardputer-adv}
out=$(mktemp)
"$(uv tool dir)/platformio/bin/python" ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port "${2:-/dev/ttyACM0}" \
  --before no_reset --after no_reset read_flash 0x3F0000 0x10000 "$out" >/dev/null
strings -n 8 "$out" | grep -m3 -E 'called at|abort|assert' || true
python3 - "$out" <<'P' | ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line -pfC -e "firmware/.pio/build/$env/firmware.elf" | grep -v '??' | cut -c1-120 | uniq | head -40
import struct, sys
data = open(sys.argv[1], 'rb').read()
for offset in range(0, struct.unpack('<I', data[:4])[0] - 4, 4):
    value = (struct.unpack('<I', data[offset:offset + 4])[0] & 0x3FFFFFFF) | 0x40000000
    if 0x42000000 <= value < 0x42200000:
        print(hex(value))
P
rm -f "$out"
