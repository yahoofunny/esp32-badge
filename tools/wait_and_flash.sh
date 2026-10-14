#!/usr/bin/env bash
# Flash as soon as the chip shows up in download mode.
#
# The board is currently running a temporary build that takes the USB port for mass
# storage on every boot, so the serial port esptool needs only exists while the chip is
# in the ROM's download mode -- which is what holding BOOT through a power-up gives.
# --before no_reset because the chip is already sitting in the loader; nothing should
# reset it out of there.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ESPTOOL="$HOME/.espressif/python_env/idf5.5_py3.13_env/Scripts/esptool.exe"
APP="$ROOT/build/system/system.ino.bin"
[ -f "$APP" ] || { echo "no app image; build first"; exit 1; }

echo "waiting for the chip in download mode (hold BOOT while plugging in)..."
for i in $(seq 1 900); do
    if "$ESPTOOL" --chip esp32s3 --port COM3 --baud 921600 --connect-attempts 1 \
         --before no_reset --after no_reset \
         write_flash --flash_mode keep --flash_size keep --flash_freq keep \
         0x20000 "$APP" > /tmp/waitflash.log 2>&1; then
        echo "flashed on attempt $i"
        # boot it, so the board comes up running the new firmware
        "$ESPTOOL" --chip esp32s3 --port COM3 --baud 921600 --connect-attempts 2 \
             --after watchdog_reset read_mac > /dev/null 2>&1
        echo "reset done; the board should be running the new build"
        exit 0
    fi
    sleep 1
done
echo "timed out"
exit 1
