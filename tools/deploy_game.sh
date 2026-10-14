#!/usr/bin/env bash
# Build the game, drop it into the idle ota_0 slot, and boot it.
#
# The stock system in the `factory` partition is never touched: only the app region
# at 0x6c0000 and the 8 KB otadata selector are written. That makes this about 5 s
# instead of the ~70 s a full reflash costs, and means a broken build can always be
# escaped from with `tools/switch_slot.py stock`.
#
# Usage:  ./tools/deploy_game.sh [sketch-dir] [port]
#         (default sketch: games/clawd-roll, default port: COM3)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SKETCH="${1:-$ROOT/games/clawd-roll}"
PORT="${2:-${PORT:-COM3}}"
SKETCH_NAME="$(basename "$SKETCH")"
APP="$ROOT/build/$SKETCH_NAME/$SKETCH_NAME.ino.bin"

OTA0_OFFSET=0x6c0000

if [ -n "${ESPTOOL:-}" ]; then
    :
elif [ -x "$HOME/.espressif/python_env/idf5.5_py3.13_env/Scripts/esptool.exe" ]; then
    ESPTOOL="$HOME/.espressif/python_env/idf5.5_py3.13_env/Scripts/esptool.exe"
else
    ESPTOOL="$(command -v esptool || command -v esptool.py || true)"
fi
[ -n "$ESPTOOL" ] || { echo "ERROR: esptool not found; set ESPTOOL" >&2; exit 1; }

PYTHON="${PYTHON:-}"
if [ -z "$PYTHON" ]; then
    for c in "$HOME/.espressif/python_env/idf5.5_py3.13_env/Scripts/python.exe" python python3; do
        if command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; then PYTHON="$c"; break; fi
    done
fi

echo "=== 1/3 compile $SKETCH_NAME (partition scheme: custom) ==="
PARTITION_SCHEME=custom "$ROOT/tools/build.sh" compile "$SKETCH"

if [ ! -f "$APP" ]; then
    echo "ERROR: expected app image not found: $APP" >&2
    exit 1
fi

echo ""
echo "=== 2/3 flash app into ota_0 at $OTA0_OFFSET ==="
ls -lh "$APP"
"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 \
    --after no_reset --connect-attempts 1 \
    write_flash --flash_mode keep --flash_size keep --flash_freq keep \
    "$OTA0_OFFSET" "$APP"

echo ""
echo "=== 3/3 select ota_0 and boot ==="
"$PYTHON" "$ROOT/tools/switch_slot.py" game "$PORT"

echo ""
echo "Deployed. Attach a monitor with:"
echo "  python tools/serial_monitor.py $PORT 115200"
