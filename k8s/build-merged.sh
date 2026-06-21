#!/usr/bin/env bash
#
# Build the firmware and merge bootloader + partition table + boot_app0 + app
# into a single image flashable at offset 0x0 (for serial flashing over USB,
# e.g. on the Talos node when OTA isn't available).
#
#   ./build-merged.sh                 # build + merge the default env
#   ENV=nodemcu-32s ./build-merged.sh # override the PlatformIO env
#
# Output: .pio/build/<env>/hatch-merged.bin
#
set -euo pipefail
cd "$(dirname "$0")"

ENV="${ENV:-nodemcu-32s}"
BUILD=".pio/build/${ENV}"
PIO_HOME="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
PIOPY="$PIO_HOME/penv/bin/python"

# 1. Build the firmware.
pio run -e "$ENV"

# 2. Locate the tools PlatformIO installed (newest version wins).
ESPTOOL="$(ls "$PIO_HOME"/packages/tool-esptoolpy*/esptool.py 2>/dev/null | sort -V | tail -1)"

BOOT_APP0="$PIO_HOME/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
if [ ! -f "$BOOT_APP0" ]; then
  BOOT_APP0="$(ls "$PIO_HOME"/packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin 2>/dev/null | sort -V | tail -1)"
fi

for f in "$PIOPY" "$ESPTOOL" "$BOOT_APP0" \
         "$BUILD/bootloader.bin" "$BUILD/partitions.bin" "$BUILD/firmware.bin"; do
  [ -f "$f" ] || { echo "error: required file not found: $f" >&2; exit 1; }
done

# 3. Merge into a single image, flashable with: write_flash -z 0x0 hatch-merged.bin
"$PIOPY" "$ESPTOOL" --chip esp32 merge_bin \
  -o "$BUILD/hatch-merged.bin" \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000  "$BUILD/bootloader.bin" \
  0x8000  "$BUILD/partitions.bin" \
  0xe000  "$BOOT_APP0" \
  0x10000 "$BUILD/firmware.bin"

echo
echo "Merged image ready: $BUILD/hatch-merged.bin"
ls -lh "$BUILD/hatch-merged.bin"
