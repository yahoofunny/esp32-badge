#!/usr/bin/env python3
"""Set the badge's clock from this machine, over the serial port.

The RTC is battery-backed, so this is a once-in-a-while thing: it only has to happen when
the board has been without a battery, or after the RTC is first powered. No Wi-Fi is
involved on either side.

    python tools/set_time.py [port]
"""

import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM3"

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.3
# Same care as the monitor: DTR deasserted before opening, so the open itself cannot pull
# the chip into download mode.
s.dsrdtr = False
s.rtscts = False
s.dtr = False
s.rts = False
s.open()
time.sleep(0.2)

epoch = int(time.time())
s.write(f"T{epoch}\n".encode())
print(f"sent T{epoch} ({time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(epoch))})")

end = time.time() + 4
buf = b""
while time.time() < end:
    buf += s.read(4096)
s.close()
for line in buf.decode("utf-8", "replace").replace("\r", "").splitlines():
    if "[clock]" in line:
        print(" ", line)
