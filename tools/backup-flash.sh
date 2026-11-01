#!/bin/bash
# Reads a Cardputer's whole 8 MB flash to <dest>/full-8mb.bin, one chunk at a
# time so an interrupted read resumes. The image holds NVS secrets: keep it out
# of every repo. The board must already be in download mode.
set -euo pipefail
dest=${1:?usage: backup-flash.sh <dest-dir> [port]}
port=${2:-/dev/ttyACM0}
esptool=~/.platformio/packages/tool-esptoolpy/esptool.py
python=$(uv tool dir)/platformio/bin/python
chunk=$((0x100000))

mkdir -p "$dest/chunks"
chmod 700 "$dest"
for n in 0 1 2 3 4 5 6 7; do
  file="$dest/chunks/$n.bin"
  if [ "$(stat -c %s "$file" 2>/dev/null || echo 0)" = "$chunk" ]; then continue; fi
  "$python" "$esptool" --chip esp32s3 --port "$port" --baud 921600 --before no_reset --after no_reset \
    read_flash $((n * chunk)) "$chunk" "$file.part" >/dev/null
  mv "$file.part" "$file"
  echo "chunk $n done"
done
cat "$dest"/chunks/{0..7}.bin > "$dest/full-8mb.bin"
sha256sum "$dest/full-8mb.bin"
