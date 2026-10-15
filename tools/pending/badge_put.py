#!/usr/bin/env python3
"""Put a file on the badge over the serial port.

The card is the badge's content pipe, and pulling it out interrupts the badge. The
serial line is already the host's channel to this thing -- the clock goes over it -- so
files go over it too: this announces the size and the path, waits for the badge to say
it is ready, streams the bytes, and then compares the CRC32 the badge reports with its
own, so the copy is verified without touching the card.

    python tools/badge_put.py <local-file> </Photos/name.ext> [port]

The badge only writes inside /Photos and /Music -- the two folders are the interface.
"""
import argparse
import sys
import time
import zlib

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

CHUNK = 4096


def read_until(port, needles, timeout):
    """Read lines until one starts with one of `needles`; returns (line, lines seen)."""
    end = time.time() + timeout
    seen = []
    buf = b""
    while time.time() < end:
        buf += port.read(4096)
        while b"\n" in buf:
            line, _, buf = buf.partition(b"\n")
            text = line.decode("utf-8", "replace").strip()
            if text:
                seen.append(text)
                for needle in needles:
                    if text.startswith(needle):
                        return text, seen
    return None, seen


def main():
    ap = argparse.ArgumentParser(description="Send a file to the badge over serial.")
    ap.add_argument("file")
    ap.add_argument("dest", help="where it goes on the badge, e.g. /Photos/x.gif")
    ap.add_argument("port", nargs="?", default="COM3")
    args = ap.parse_args()

    if not (args.dest.startswith("/Photos/") or args.dest.startswith("/Music/")):
        sys.exit("dest must be under /Photos/ or /Music/")

    data = open(args.file, "rb").read()
    want = zlib.crc32(data) & 0xFFFFFFFF

    s = serial.Serial()
    s.port = args.port
    s.baudrate = 115200
    s.timeout = 0.2
    # DTR asserted, RTS left alone. The hardware CDC on this chip only *receives* while
    # DTR is high -- with it low, output flows but commands are never seen (measured:
    # the clock command gets no answer). The download-mode trap needs an RTS pulse, not
    # DTR, so as long as nothing toggles RTS the chip stays in its application.
    s.dsrdtr = False
    s.rtscts = False
    s.dtr = True
    s.rts = False
    s.open()
    time.sleep(0.2)

    print("sending %s -> %s (%d bytes)" % (args.file, args.dest, len(data)))
    s.write(("W%d %s\n" % (len(data), args.dest)).encode())

    line, seen = read_until(s, ("[file] ready", "[file] err"), 5.0)
    if line is None:
        sys.exit("no answer from the badge (running? right port?): %s" % seen[-3:])
    if line.startswith("[file] err"):
        sys.exit("badge refused: %s" % line)
    print("  badge is ready")

    sent = 0
    for i in range(0, len(data), CHUNK):
        chunk = data[i:i + CHUNK]
        s.write(chunk)
        sent += len(chunk)
        if sent % (256 * 1024) < CHUNK:
            print("  %d%%" % (100 * sent // len(data)))

    line, seen = read_until(s, ("[file] wrote", "[file] err"), 60.0 + len(data) / 2000.0)
    if line is None:
        sys.exit("no completion from the badge: %s" % seen[-3:])
    if line.startswith("[file] err"):
        sys.exit("badge failed: %s" % line)

    crc = None
    for token in line.split():
        if token.startswith("crc32="):
            crc = int(token[6:], 16)

    s.dtr = False
    s.rts = False
    s.close()

    print("  %s" % line)
    if crc is None:
        sys.exit("the badge did not report a CRC; nothing to verify against")
    if crc != want:
        sys.exit("CRC mismatch: badge 0x%08X, host 0x%08X" % (crc, want))
    print("  CRC match (0x%08X) -- the file is on the card" % want)


if __name__ == "__main__":
    main()
