#!/usr/bin/env python3
"""Rearrange the flash so the two firmwares can hand the board back and forth.

Before:  factory = stock system (has the XiaoZhi app),  ota_0 = our system
After:   factory = our system,                          ota_0 = stock system

The swap is what makes the round trip work without a host computer. The stock
firmware calls esp_ota_set_boot_partition(factory) on every boot, and for a factory
partition ESP-IDF just erases otadata (components/app_update/esp_ota_ops.c). So once
`factory` holds *our* firmware, the stock system's own habit of pinning the boot
target there becomes the way home: use XiaoZhi, power cycle, and the launcher is back.

And in the other direction, our system writes an otadata entry selecting ota_0, which
is where the stock system now lives.

The stock application image is recovered from the byte-exact dump in backup/, and its
assets partition is deliberately left at the offset it already occupies, so nothing
large has to be copied.

    python tools/migrate_layout.py            # do it
    python tools/migrate_layout.py --dry-run  # report only

Restoring the original arrangement is always possible with tools/restore_stock.sh,
which writes the whole 16 MB dump back verbatim.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BACKUP = os.path.join(ROOT, "backup", "1.85B-stock-16MB.bin")
APP = os.path.join(ROOT, "build", "system", "system.ino.bin")
PART = os.path.join(ROOT, "build", "system", "system.ino.partitions.bin")

# Original layout, as read back from the stock bootloader's own log.
STOCK_FACTORY_OFFSET = 0x20000
STOCK_FACTORY_SIZE = 0x692000

# New layout, mirrored from system/partitions.csv.
NEW_FACTORY_OFFSET = 0x20000      # our system
NEW_OTA0_OFFSET = 0x120000        # stock system
OTA_DATA_OFFSET = 0xD000
OTA_DATA_SIZE = 0x2000
PARTITION_TABLE_OFFSET = 0x8000

ESP_IMAGE_MAGIC = 0xE9

DEFAULT_PORT = "COM3"
BAUD = 921600


def find_esptool() -> str:
    if os.environ.get("ESPTOOL"):
        return os.environ["ESPTOOL"]
    cand = os.path.join(os.path.expanduser("~"), ".espressif", "python_env",
                        "idf5.5_py3.13_env", "Scripts", "esptool.exe")
    if os.path.exists(cand):
        return cand
    import shutil
    found = shutil.which("esptool") or shutil.which("esptool.py")
    if not found:
        sys.exit("esptool not found; set the ESPTOOL environment variable")
    return found


def esptool(esptool_path, port, args, reset=True):
    cmd = [esptool_path, "--chip", "esp32s3", "--port", port, "--baud", str(BAUD),
           "--after", "watchdog_reset" if reset else "no_reset",
           "--connect-attempts", "1", *args]
    print("+", " ".join(cmd[1:]))
    return subprocess.call(cmd)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", default=DEFAULT_PORT)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    for path in (BACKUP, APP, PART):
        if not os.path.exists(path):
            sys.exit(f"missing required file: {path}")

    # --- recover the stock application out of the dump ----------------------
    with open(BACKUP, "rb") as fh:
        fh.seek(STOCK_FACTORY_OFFSET)
        stock_app = fh.read(STOCK_FACTORY_SIZE)

    if stock_app[0] != ESP_IMAGE_MAGIC:
        sys.exit(f"dump does not contain an app image at 0x{STOCK_FACTORY_OFFSET:X} "
                 f"(first byte 0x{stock_app[0]:02X})")
    if len(stock_app) != STOCK_FACTORY_SIZE:
        sys.exit("dump is shorter than the factory partition")

    # The image ends where the padding starts; report the real size so the numbers
    # below are honest rather than "6.9 MB" for a 6.4 MB app.
    end = len(stock_app)
    while end > 0 and stock_app[end - 1] == 0xFF:
        end -= 1
    print(f"stock app   : {end / 1024 / 1024:.2f} MB of image "
          f"({len(stock_app) / 1024 / 1024:.2f} MB partition)")

    with open(APP, "rb") as fh:
        our_app = fh.read()
    if our_app[0] != ESP_IMAGE_MAGIC:
        sys.exit("our app image does not start with the ESP image magic")
    print(f"our app     : {len(our_app) / 1024:.0f} KB")

    with open(PART, "rb") as fh:
        partition_table = fh.read()
    print(f"partition tbl: {len(partition_table)} bytes")

    if len(our_app) > (NEW_OTA0_OFFSET - NEW_FACTORY_OFFSET):
        sys.exit("our app does not fit in the 1 MB factory partition")
    if end > 0x9A0000:
        sys.exit("stock app does not fit in the ota_0 partition")

    print()
    print("will write:")
    print(f"  0x{PARTITION_TABLE_OFFSET:06X}  partition table (new layout)")
    print(f"  0x{NEW_FACTORY_OFFSET:06X}  our system")
    print(f"  0x{NEW_OTA0_OFFSET:06X}  stock system (recovered from the dump)")
    print(f"  0x{OTA_DATA_OFFSET:06X}  erase otadata -> boot factory = our system")
    print()
    print("assets stays at 0xAC0000, untouched. The bootloader is not rewritten.")
    print()

    if args.dry_run:
        print("--dry-run: nothing written.")
        return 0

    print("This replaces the partition table and moves the stock app.")
    print("tools/restore_stock.sh rewrites the full 16 MB dump if you want it back.")
    print()

    tool = find_esptool()
    stock_tmp = os.path.join(os.environ.get("TEMP", "/tmp"), "stock_app_relocated.bin")
    with open(stock_tmp, "wb") as fh:
        fh.write(stock_app)

    steps = [
        (["write_flash", "--flash_mode", "keep", "--flash_size", "keep", "--flash_freq", "keep",
          hex(PARTITION_TABLE_OFFSET), PART], True),
        (["write_flash", "--flash_mode", "keep", "--flash_size", "keep", "--flash_freq", "keep",
          hex(NEW_FACTORY_OFFSET), APP], True),
        (["write_flash", "--flash_mode", "keep", "--flash_size", "keep", "--flash_freq", "keep",
          hex(NEW_OTA0_OFFSET), stock_tmp], True),
        (["erase_region", hex(OTA_DATA_OFFSET), hex(OTA_DATA_SIZE)], False),
    ]

    for i, (args_, reset) in enumerate(steps, 1):
        print(f"--- step {i}/{len(steps)} ---")
        # Only the last step may leave the ROM; the others keep it in the loader so
        # the sequence is not interrupted by an app that is not fully written yet.
        rc = esptool(tool, args.port, args_, reset=reset)
        if rc:
            print(f"step {i} failed with {rc}", file=sys.stderr)
            return rc

    print()
    print("Layout migrated. The board should now be booting our launcher from factory.")
    print("Tap XIAOZHI (or hold BOOT on the launcher) to reach the stock system;")
    print("power cycle from there to come back.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
