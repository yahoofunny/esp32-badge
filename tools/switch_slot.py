#!/usr/bin/env python3
"""Choose which firmware the bootloader starts.

Neither firmware is touched -- only the 8 KB `otadata` partition is rewritten, which
is what the bootloader consults. Switching takes about a second instead of the ~70 s a
full reflash costs.

    home   -> erase otadata. An invalid/erased otadata makes the bootloader fall back
              to the `factory` partition, which holds our own system.
    stock  -> write an `esp_ota_select_entry_t` selecting ota_0, which holds
              Waveshare's stock system (and the XiaoZhi app).

Note which slot means what. The two firmwares were deliberately swapped relative to
the layout the board shipped with -- see tools/migrate_layout.py for why: it is what
lets the stock system hand the board back by itself, with no host computer involved.

The entry format is ESP-IDF's esp_ota_select_entry_t (32 bytes):

    uint32_t ota_seq;
    uint8_t  seq_label[20];
    uint32_t ota_state;
    uint32_t crc;

with three details that are easy to get wrong, all verified against the IDF sources:

  * the checksum covers ONLY the 4-byte ota_seq, not the whole struct
    (bootloader_common_ota_select_crc in bootloader_common_loader.c)
  * esp_rom_crc32_le inverts its input and its output, so it is NOT zlib's crc32
  * ota_state must be VALID; anything the bootloader finds in PENDING_VERIFY or NEW
    is treated as unconfirmed and rolled back

Usage:
    tools/switch_slot.py home [port]
    tools/switch_slot.py stock [port]
    tools/switch_slot.py status [port]      # report what is on the device now
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

OTA_DATA_OFFSET = 0xD000
OTA_DATA_SIZE = 0x2000
SECTOR_SIZE = 0x1000

DEFAULT_PORT = "COM3"
BAUD = 921600


def esp_rom_crc32_le(crc: int, data: bytes) -> int:
    """Bit-exact port of esp_rom_crc32_le() (see
    components/esp_rom/patches/esp_rom_crc.c): the input is inverted, the reflected
    table loop runs, and the result is inverted again. Note this is *not* the same
    function as zlib's crc32 -- the initial value differs."""
    c = (~crc) & 0xFFFFFFFF
    for byte in data:
        c ^= byte
        for _ in range(8):
            c = (c >> 1) ^ (0xEDB88320 if c & 1 else 0)
    return (~c) & 0xFFFFFFFF


# esp_ota_img_states_t
ESP_OTA_IMG_NEW            = 0x0
ESP_OTA_IMG_PENDING_VERIFY = 0x1
ESP_OTA_IMG_VALID          = 0x2
ESP_OTA_IMG_INVALID        = 0x3
ESP_OTA_IMG_ABORTED        = 0x4


def otadata_entry(seq: int) -> bytes:
    """One esp_ota_select_entry_t.

    entry layout: uint32 ota_seq | uint8 seq_label[20] | uint32 ota_state | uint32 crc

    The checksum covers *only* the 4-byte ota_seq, per
    bootloader_common_ota_select_crc() in bootloader_common_loader.c:
        esp_rom_crc32_le(UINT32_MAX, (uint8_t *)&s->ota_seq, 4)

    ota_state must be VALID. The bootloader (with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    rewrites any entry it finds in PENDING_VERIFY to ABORTED before choosing, and
    turns NEW into PENDING_VERIFY -- either way the image would be treated as
    unconfirmed and rolled back on the next reset.
    """
    seq_bytes = struct.pack("<I", seq)
    body = seq_bytes + bytes(20) + struct.pack("<I", ESP_OTA_IMG_VALID)
    assert len(body) == 28, len(body)
    return body + struct.pack("<I", esp_rom_crc32_le(0xFFFFFFFF, seq_bytes))


def otadata_image_for_game() -> bytes:
    """Sector 0 carries a valid entry; sector 1 stays erased (its ota_seq of 0xFFFFFFFF
    makes it invalid, so sector 0 is the only candidate).

    The bootloader maps the entry to a slot with
        boot_index = (ota_seq - 1) % app_count
    where app_count counts OTA app partitions only. This table has exactly one
    (ota_0), so any sequence number at all resolves to ota_0.
    """
    sector0 = otadata_entry(1) + b"\xFF" * (SECTOR_SIZE - 32)
    return sector0 + b"\xFF" * SECTOR_SIZE


def find_esptool() -> str:
    override = os.environ.get("ESPTOOL")
    if override:
        return override
    home = os.path.expanduser("~")
    candidates = [
        os.path.join(home, ".espressif", "python_env", "idf5.5_py3.13_env", "Scripts", "esptool.exe"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    found = shutil.which("esptool") or shutil.which("esptool.py")
    if not found:
        sys.exit("esptool not found; set the ESPTOOL environment variable")
    return found


def run_esptool(esptool: str, port: str, args, reset: bool = True) -> int:
    after = "watchdog_reset" if reset else "no_reset"
    # A watchdog reset is used deliberately: on this board the USB-Serial/JTAG
    # peripheral treats an RTS-driven reset as a request to re-enter download mode,
    # while the watchdog path boots the application the way a power cycle would.
    cmd = [esptool, "--chip", "esp32s3", "--port", port, "--baud", str(BAUD),
           "--after", after, "--connect-attempts", "1", *args]
    print("+", " ".join(cmd[1:]))
    return subprocess.call(cmd)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("slot", choices=["home", "stock", "status"])
    ap.add_argument("port", nargs="?", default=DEFAULT_PORT)
    args = ap.parse_args()

    esptool = find_esptool()

    if args.slot == "home":
        print("Erasing otadata -> the bootloader falls back to the factory partition,")
        print("which holds our own system (launcher + games)")
        rc = run_esptool(esptool, args.port, ["erase_region", hex(OTA_DATA_OFFSET), hex(OTA_DATA_SIZE)])
        if rc:
            return rc
        print("\nDone. Our system should be booting now.")

    elif args.slot == "stock":
        image = otadata_image_for_game()
        tmp = os.path.join(tempfile.gettempdir(), "otadata_ota0.bin")
        with open(tmp, "wb") as fh:
            fh.write(image)
        print(f"Writing otadata selecting ota_0 -> Waveshare's stock system "
              f"({len(image)} bytes from {tmp})")
        rc = run_esptool(esptool, args.port,
                         ["write_flash", "--flash_mode", "keep", "--flash_size", "keep",
                          "--flash_freq", "keep", hex(OTA_DATA_OFFSET), tmp])
        if rc:
            return rc
        print("\nDone. The stock system should be booting now.")
        print("It pins the boot target back to factory on every boot, so a power cycle")
        print("from there returns to our system with no host computer involved.")

    else:  # status
        tmp = os.path.join(tempfile.gettempdir(), "otadata_read.bin")
        # Reading flash requires entering the ROM loader, so finish with a watchdog
        # reset to put the running application back the way we found it.
        rc = run_esptool(esptool, args.port,
                         ["read_flash", hex(OTA_DATA_OFFSET), hex(OTA_DATA_SIZE), tmp], reset=True)
        if rc:
            return rc
        data = open(tmp, "rb").read()
        for idx in range(2):
            entry = data[idx * SECTOR_SIZE : idx * SECTOR_SIZE + 32]
            seq, = struct.unpack("<I", entry[0:4])
            state, = struct.unpack("<I", entry[24:28])
            crc, = struct.unpack("<I", entry[28:32])
            # the checksum covers only the ota_seq field
            crc_ok = (crc == esp_rom_crc32_le(0xFFFFFFFF, entry[0:4]))
            valid = (seq != 0xFFFFFFFF
                     and state not in (ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED)
                     and crc_ok)
            label = {0: "NEW", 1: "PENDING_VERIFY", 2: "VALID",
                     3: "INVALID", 4: "ABORTED"}.get(state, "?")
            if seq == 0xFFFFFFFF and crc == 0xFFFFFFFF:
                print(f"sector {idx}: erased")
            else:
                print(f"sector {idx}: seq={seq} state={label} crc={'ok' if crc_ok else 'BAD'}"
                      f"  -> {'VALID' if valid else 'ignored'}")
        print()
        print("No valid entry -> bootloader falls back to factory   = our system")
        print("A valid entry selecting ota_0                        = the stock system")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
