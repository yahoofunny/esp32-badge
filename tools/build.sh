#!/usr/bin/env bash
# Build / flash helper for the Waveshare ESP32-S3-Touch-LCD-1.85B under arduino-cli.
#
# Board options that matter for this board (everything else stays at default):
#   CDCOnBoot=cdc   -> the board's USB-C is wired to the chip's native USB-Serial/JTAG,
#                      so Serial only shows up on USB when CDC-on-boot is enabled
#   FlashSize=16M   -> onboard 16MB NOR flash
#   PSRAM=opi       -> onboard 8MB octal PSRAM
#   FlashMode=qio   -> QIO 80MHz (Waveshare's recommended setting)
#   PartitionScheme=app3M_fat9M_16MB -> 3MB app + 9.9MB FATFS, good headroom for games/assets
#
# Usage:
#   ./tools/build.sh compile <sketch-dir>
#   ./tools/build.sh upload  <sketch-dir> [port]
#   ./tools/build.sh both    <sketch-dir> [port]
#
# Overridable environment variables:
#   ARDUINO_CLI   path to arduino-cli     (default: tools/arduino-cli.exe, else $PATH)
#   SKETCHBOOK    Arduino libraries dir   (default: read from arduino-cli config)
#   PORT          serial port for upload  (default: COM3)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

FQBN="esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"

# Locate arduino-cli: explicit override, then the copy shipped in tools/, then PATH.
if [ -n "${ARDUINO_CLI:-}" ]; then
    CLI="$ARDUINO_CLI"
elif [ -x "$ROOT/tools/arduino-cli.exe" ]; then
    CLI="$ROOT/tools/arduino-cli.exe"
elif [ -x "$ROOT/tools/arduino-cli" ]; then
    CLI="$ROOT/tools/arduino-cli"
else
    CLI="$(command -v arduino-cli)"
fi

# Locate the sketchbook (where arduino-cli looks for installed libraries).
if [ -z "${SKETCHBOOK:-}" ]; then
    SKETCHBOOK="$("$CLI" config get directories.user | tr -d '\r')"
fi

PORT="${PORT:-COM3}"

usage() {
    echo "usage: $0 compile <sketch-dir>"
    echo "       $0 upload  <sketch-dir> [port]"
    echo "       $0 both    <sketch-dir> [port]"
    exit 1
}

[ $# -ge 2 ] || usage
ACTION="$1"
SKETCH="$2"
[ $# -ge 3 ] && PORT="$3"

echo "arduino-cli : $CLI"
echo "libraries   : $SKETCHBOOK/libraries"
echo ""

case "$ACTION" in
    compile)
        "$CLI" compile --fqbn "$FQBN" --libraries "$SKETCHBOOK/libraries" --warnings none "$SKETCH"
        ;;
    upload)
        "$CLI" upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"
        ;;
    both)
        "$CLI" compile --fqbn "$FQBN" --libraries "$SKETCHBOOK/libraries" --warnings none "$SKETCH"
        "$CLI" upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"
        ;;
    *)
        usage
        ;;
esac
