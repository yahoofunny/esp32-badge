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
#   ARDUINO_CLI       path to arduino-cli     (default: tools/arduino-cli.exe, else $PATH)
#   SKETCHBOOK        Arduino libraries dir   (default: read from arduino-cli config)
#   PORT              serial port for upload  (default: COM3)
#   PARTITION_SCHEME  Arduino partition scheme (default: app3M_fat9M_16MB)
#                     Use `custom` for sketches that ship a partitions.csv, e.g. the
#                     game firmware, whose table must match the stock layout so it
#                     can live in the idle ota_0 slot.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PARTITION_SCHEME="${PARTITION_SCHEME:-app3M_fat9M_16MB}"

FQBN="esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=${PARTITION_SCHEME},DebugLevel=none,PSRAM=opi,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default,ZigbeeMode=default"

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

# Locate the sketchbook (where arduino-cli looks for installed libraries). The
# repo's own shared libraries under lib/ are added alongside it, so sketches can
# use them without anything being installed into the sketchbook first.
#
# The path must be in Windows form: arduino-cli is a native binary and does not
# resolve the /c/... paths Git Bash hands out, at least not inside a
# comma-separated --libraries list.
if [ -z "${SKETCHBOOK:-}" ]; then
    SKETCHBOOK="$("$CLI" config get directories.user | tr -d '\r')"
fi
ROOT_WIN="$(cygpath -w "$ROOT" 2>/dev/null || echo "$ROOT")"
LIB_PATH="$SKETCHBOOK/libraries,$ROOT_WIN\\lib"

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

# Keep build output inside the repo so the generated partition table and the app
# .bin can be inspected before anything is flashed. --output-dir is deliberately
# not used: pointing it at the build path makes arduino-cli try to copy each
# artifact onto itself and fail.
SKETCH_NAME="$(basename "$SKETCH")"
BUILD_DIR="$ROOT/build/$SKETCH_NAME"

COMPILE_ARGS=(--fqbn "$FQBN" --libraries "$LIB_PATH" --warnings none
              --build-path "$BUILD_DIR")

echo "arduino-cli : $CLI"
echo "libraries   : $LIB_PATH"
echo "partition   : $PARTITION_SCHEME"
echo "build dir   : $BUILD_DIR"
echo ""

case "$ACTION" in
    compile)
        "$CLI" compile "${COMPILE_ARGS[@]}" "$SKETCH"
        ;;
    upload)
        "$CLI" upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD_DIR" "$SKETCH"
        ;;
    both)
        "$CLI" compile "${COMPILE_ARGS[@]}" "$SKETCH"
        "$CLI" upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD_DIR" "$SKETCH"
        ;;
    *)
        usage
        ;;
esac
