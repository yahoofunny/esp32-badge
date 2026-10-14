#!/usr/bin/env python3
"""Install our launcher into the board's main slot, leaving the assistant alone.

The board ships with a two-firmware design, which this script preserves rather than
replaces:

    factory 0x020000 6.88MB    the main application  <- we put our launcher here
    ota_0   0x6c0000 4.00MB    xiaozhi 2.2.4         <- left exactly where it is
    assets  0xac0000 4.00MB    fonts + speech models <- untouched

`ota_0` is not spare space. It holds the XiaoZhi voice assistant as a complete
standalone firmware, and the main application switches to it the way it always has:
by writing otadata and rebooting. The assistant switches back by setting `factory` as
the boot partition ("已设置 factory 为启动分区 重启回到主程序" in its own log).

So the round trip needs neither a host computer nor a power cycle. The only thing
this script changes about the arrangement is *which* main application lives in
`factory`; the mechanism between the two slots is Waveshare's own.

    python tools/install_stack.py            # do it
    python tools/install_stack.py --dry-run  # report only

The assistant image is recovered from the byte-exact dump in backup/. Running
tools/restore_stock.sh afterwards puts the original firmware back in every slot.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BACKUP = os.path.join(ROOT, "backup", "1.85B-stock-16MB.bin")
APP = os.path.join(ROOT, "build", "system", "system.ino.bin")
PART = os.path.join(ROOT, "build", "system", "system.ino.partitions.bin")

# Offsets in the dump / on the device. The assistant is restored to the same offset
# it already occupies, so no large copy is involved -- only our own app moves in.
ASSISTANT_OFFSET = 0x6C0000
ASSISTANT_SIZE = 0x400000

FACTORY_OFFSET = 0x20000
FACTORY_SIZE = 0x692000

PARTITION_TABLE_OFFSET = 0x8000
OTA_DATA_OFFSET = 0xD000
OTA_DATA_SIZE = 0x2000

ESP_IMAGE_MAGIC = 0xE9
ESP_APP_DESC_MAGIC = 0xABCD5432

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


def esptool(exe, port, args, reset=True):
    cmd = [exe, "--chip", "esp32s3", "--port", port, "--baud", str(BAUD),
           "--after", "watchdog_reset" if reset else "no_reset",
           "--connect-attempts", "1", *args]
    print("+", " ".join(cmd[1:]))
    return subprocess.call(cmd)


def describe(image: bytes, where: str) -> str:
    if image[0] != ESP_IMAGE_MAGIC:
        sys.exit(f"{where}: not an ESP image (first byte 0x{image[0]:02X})")
    import struct
    magic, = struct.unpack("<I", image[0x20:0x24])
    if magic != ESP_APP_DESC_MAGIC:
        return "(no app description)"
    proj = image[0x20 + 48:0x20 + 80].split(b"\x00")[0].decode("utf-8", "replace")
    ver = image[0x20 + 16:0x20 + 48].split(b"\x00")[0].decode("utf-8", "replace")
    idf = image[0x20 + 112:0x20 + 144].split(b"\x00")[0].decode("utf-8", "replace")
    return f"project={proj} version={ver} idf={idf}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", default=DEFAULT_PORT)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    for path in (BACKUP, APP, PART):
        if not os.path.exists(path):
            sys.exit(f"missing required file: {path}")

    with open(BACKUP, "rb") as fh:
        fh.seek(ASSISTANT_OFFSET)
        assistant = fh.read(ASSISTANT_SIZE)

    with open(APP, "rb") as fh:
        our_app = fh.read()
    if our_app[0] != ESP_IMAGE_MAGIC:
        sys.exit("our app image does not start with the ESP image magic")

    with open(PART, "rb") as fh:
        partition_table = fh.read()

    print(f"assistant image : {ASSISTANT_SIZE / 1024 / 1024:.2f} MB  {describe(assistant, 'backup/ota_0')}")
    print(f"our app         : {len(our_app) / 1024:.0f} KB       {describe(our_app, 'build')}")
    print(f"partition table : {len(partition_table)} bytes")
    print()

    if len(our_app) > FACTORY_SIZE:
        sys.exit("our app does not fit in the factory partition")
    if not partition_table:
        sys.exit("empty partition table")

    print("will write:")
    print(f"  0x{PARTITION_TABLE_OFFSET:06X}  partition table (the stock layout)")
    print(f"  0x{FACTORY_OFFSET:06X}  our launcher        (factory slot)")
    print(f"  0x{ASSISTANT_OFFSET:06X}  XiaoZhi assistant   (ota_0 slot, restored from dump)")
    print(f"  0x{OTA_DATA_OFFSET:06X}  erase otadata -> boot factory = our launcher")
    print()
    print("The bootloader and the assets partition are not touched.")
    print()

    if args.dry_run:
        print("--dry-run: nothing written.")
        return 0

    tool = find_esptool()
    assistant_tmp = os.path.join(os.environ.get("TEMP", "/tmp"), "assistant_ota0.bin")
    with open(assistant_tmp, "wb") as fh:
        fh.write(assistant)

    steps = [
        (["write_flash", "--flash_mode", "keep", "--flash_size", "keep", "--flash_freq", "keep",
          hex(PARTITION_TABLE_OFFSET), PART], True),
        (["write_flash", "--flash_mode", "keep", "--flash_size", "keep", "--flash_freq", "keep",
          hex(FACTORY_OFFSET), APP], True),
        (["write_flash", "--flash_mode", "keep", "--flash_size", "keep", "--flash_freq", "keep",
          hex(ASSISTANT_OFFSET), assistant_tmp], True),
        # Must reset: a chip left in the ROM loader looks dead (black screen, no
        # response to the power button) while still enumerating over USB, and every
        # later esptool call would reset on connect and report a healthy boot.
        (["erase_region", hex(OTA_DATA_OFFSET), hex(OTA_DATA_SIZE)], True),
    ]

    for i, (a, reset) in enumerate(steps, 1):
        print(f"--- step {i}/{len(steps)} ---")
        rc = esptool(tool, args.port, a, reset=reset)
        if rc:
            print(f"step {i} failed with {rc}", file=sys.stderr)
            return rc

    print()
    print("Done. The launcher should be booting now.")
    print("Tap XIAOZHI (or hold BOOT on the launcher) to reach the assistant;")
    print("hold BOOT inside the assistant to come back.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
