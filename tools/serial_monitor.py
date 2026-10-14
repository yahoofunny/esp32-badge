#!/usr/bin/env python3
"""Safe serial monitor for boards whose USB-C goes straight to the chip's native
USB-Serial/JTAG (ESP32-S3/C3/C6/H2/P4 with `USB mode: USB-Serial/JTAG`).

Why this exists
---------------
On these boards the DTR/RTS lines are not wired to an external auto-reset
transistor circuit -- the chip's USB-Serial/JTAG peripheral interprets them, and
asserting DTR requests *download mode* on the next reset. Naive pyserial use is
dangerous in two ways:

  1. pyserial asserts DTR by default when a port is opened. Merely opening the
     port can therefore drive the chip toward download mode.
  2. Toggling RTS to "reset" the chip while DTR is still asserted resets it into
     the ROM bootloader instead of the application. The screen goes black and the
     device looks dead, but it is only sitting at "waiting for download".

Once the chip is in that state it can be sticky: plain resets keep landing back in
download mode, and the only reliable recovery is a cold power cycle (unplug USB,
and if a battery is attached, power-cycle with the PWR button).

This script deliberately never asserts DTR and never pulses RTS, so it can be
attached and detached freely while the application runs.

Usage
-----
    tools/serial_monitor.py [port] [baud]        # default COM3 115200
    tools/serial_monitor.py COM3 115200 --seconds 20

Quit with Ctrl-C.
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")


def main() -> int:
    ap = argparse.ArgumentParser(description="Non-intrusive serial monitor.")
    ap.add_argument("port", nargs="?", default="COM3")
    ap.add_argument("baud", nargs="?", type=int, default=115200)
    ap.add_argument(
        "--seconds",
        type=float,
        default=0,
        help="exit after N seconds instead of running until Ctrl-C",
    )
    args = ap.parse_args()

    s = serial.Serial()
    s.port = args.port
    s.baudrate = args.baud
    s.timeout = 0.5
    s.dsrdtr = False
    s.rtscts = False
    # Set both control lines to their idle state *before* opening, so the open
    # itself cannot pull the chip into download mode. False == deasserted.
    s.dtr = False
    s.rts = False

    try:
        s.open()
    except serial.SerialException as exc:
        return print(f"cannot open {args.port}: {exc}") or 1

    print(f"--- monitoring {args.port} @ {args.baud} (Ctrl-C to stop) ---")
    deadline = time.time() + args.seconds if args.seconds else None
    try:
        while deadline is None or time.time() < deadline:
            try:
                data = s.read(4096)
            except serial.SerialException:
                # The device went away underneath us -- typically the cable was
                # unplugged. That is a normal end to a monitoring session, not a
                # crash, so say so and stop rather than dumping a traceback.
                print("\n--- device disconnected (cable unplugged?) ---")
                try:
                    s.close()
                except Exception:
                    pass
                return 0
            if data:
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        # Leave the lines in the idle state; do not toggle anything on the way out.
        try:
            s.dtr = False
            s.rts = False
            s.close()
        except Exception:
            pass
        print("\n--- monitor closed ---")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
