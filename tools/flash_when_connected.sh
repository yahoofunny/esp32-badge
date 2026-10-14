#!/usr/bin/env bash
# Flash as soon as the board is on the cable again.
#
# Unlike wait_and_flash.sh this does not need BOOT held: the board is running the normal
# firmware, so the port is up and esptool's own reset works. It loops the whole deploy so
# a port that is busy or absent is simply retried.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for i in $(seq 1 600); do
    if "$ROOT/tools/deploy_game.sh" "$ROOT/system" COM3 > /tmp/waitdeploy.log 2>&1; then
        echo "deployed on attempt $i"
        exit 0
    fi
    sleep 2
done
echo "timed out waiting for the board"
exit 1
