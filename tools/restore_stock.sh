#!/usr/bin/env bash
# Restore the board to its stock factory firmware.
#
# Image sources, in order of preference:
#   1. --image <path>              an explicit image
#   2. backup/1.85B-stock-16MB.bin a byte-exact 16MB dump read off the device (not in git)
#   3. ESP32-S3-Touch-LCD-1.85B/Firmware/ESP32-S3-Touch-LCD-1.85B-Factory.bin
#                                  Waveshare's published factory image (in their repo)
#
# Usage:
#   ./tools/restore_stock.sh [port] [--image <path>]
#
# Environment:
#   ESPTOOL   path to esptool (default: search ESP-IDF python env, Arduino15, then PATH)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PORT="COM3"
IMAGE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --image)
            IMAGE="$2"; shift 2 ;;
        *)
            PORT="$1"; shift ;;
    esac
done

# --- locate esptool ---------------------------------------------------------
if [ -n "${ESPTOOL:-}" ]; then
    :
elif [ -x "$HOME/.espressif/python_env/idf5.5_py3.13_env/Scripts/esptool.exe" ]; then
    ESPTOOL="$HOME/.espressif/python_env/idf5.5_py3.13_env/Scripts/esptool.exe"
else
    ESPTOOL="$(command -v esptool || command -v esptool.py || true)"
fi

if [ -z "${ESPTOOL:-}" ]; then
    echo "ERROR: esptool not found. Set the ESPTOOL environment variable." >&2
    exit 1
fi

# --- pick the image --------------------------------------------------------
if [ -z "$IMAGE" ]; then
    if [ -f "$ROOT/backup/1.85B-stock-16MB.bin" ]; then
        IMAGE="$ROOT/backup/1.85B-stock-16MB.bin"
        echo "Using local device dump."
    elif [ -f "$ROOT/ESP32-S3-Touch-LCD-1.85B/Firmware/ESP32-S3-Touch-LCD-1.85B-Factory.bin" ]; then
        IMAGE="$ROOT/ESP32-S3-Touch-LCD-1.85B/Firmware/ESP32-S3-Touch-LCD-1.85B-Factory.bin"
        echo "Using Waveshare's published factory image."
    else
        echo "ERROR: no image found. Read a dump first with:" >&2
        echo "  esptool --port $PORT --baud 921600 read_flash 0 0x1000000 backup/1.85B-stock-16MB.bin" >&2
        exit 1
    fi
fi

[ -f "$IMAGE" ] || { echo "ERROR: image not found: $IMAGE" >&2; exit 1; }

ls -lh "$IMAGE"
echo ""
echo "Port: $PORT"
echo "This overwrites the ENTIRE flash. Press Ctrl-C within 5s to abort."
sleep 5

# Restore the image byte-for-byte.
#
# --flash_mode/--flash_size/--flash_freq MUST stay "keep". Passing real values
# makes esptool rewrite the flash-parameter byte in the image header (offset 2,
# SPI mode). The stock bootloader is built for DIO; forcing QIO there leaves it
# unable to read flash and the board watchdog-resets in a loop.
# --verify reads the written regions back and compares.
#
# --after watchdog_reset, not esptool's default RTS reset: on this board the
# USB-Serial/JTAG peripheral treats an RTS-driven reset as a request to re-enter
# download mode, and a board left in the ROM loader looks dead (black screen) while
# still enumerating over USB. The watchdog path boots the application properly.
"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 --after watchdog_reset \
    write_flash --flash_mode keep --flash_size keep --flash_freq keep --verify \
    0x0 "$IMAGE"

echo ""
echo "Done. The board should now boot the stock firmware."
