#!/usr/bin/env python3
"""Bounded native USB-Serial/JTAG monitor, with no RTS reset pulses.

On this badge native HWCDC needs DTR asserted for host commands. DTR alone is
not the reset gesture; toggling RTS with DTR asserted can enter the ROM loader.
Set both lines before opening, enable DTR and keep RTS inactive throughout.

Usage: python tools/serial_monitor.py [COM3] [115200] [--seconds 20]
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
    # Enable native CDC host reception while leaving reset inactive.
    s.dtr = True
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
        # Close without a reset pulse.
        try:
            s.close()
        except Exception:
            pass
        print("\n--- monitor closed ---")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
