# esp32-badge

Developing a pocket **electronic badge** (电子吧唧) and a set of small games on the
Waveshare **ESP32-S3-Touch-LCD-1.85B** — a round 1.85" 360×360 touch display with a
6-axis IMU.

This repo holds the setup scripts, hardware notes, and (soon) the game and badge
firmware. It does **not** vendor the upstream SDKs — see [Setup](#setup).

## Hardware

| | |
|---|---|
| MCU | ESP32-S3R8, dual-core LX7 @ 240MHz |
| Memory | 512KB SRAM + **8MB OPI PSRAM** |
| Flash | **16MB** NOR |
| Display | 1.85" round IPS, 360×360, **ST77916**, QSPI, RGB565 |
| Touch | **CST816S**, I²C `0x15` |
| IMU | **QMI8658** 6-axis, I²C `0x6B`, ±2–16g / ±32–4096dps |
| Audio | ES8311 codec `0x30` + ES7210 echo-cancel `0x80`, dual mics, speaker pads |
| Other | PCF85063 RTC `0x51`, BQ27220 fuel gauge `0x55`, microSD (4-bit SDMMC) |
| Radio | WiFi 2.4GHz b/g/n + BLE 5 |
| Power | USB-C or MX1.25 3.7V LiPo (onboard charging) |

Pin map, free-GPIO analysis and expansion notes: **[docs/hardware.md](docs/hardware.md)**

The USB-C port is wired straight to the chip's native USB-Serial/JTAG
(`VID:PID = 303A:1001`) — there is no USB-to-UART bridge chip on the board.

## Repo layout

```
esp32-badge/
├── docs/
│   └── hardware.md            pin map, free GPIOs, expansion notes
├── tools/
│   ├── build.sh               compile / flash helper (arduino-cli)
│   ├── restore_stock.sh       restore factory firmware
│   ├── idf_install_steps.bat  install ESP-IDF toolchain (Git Bash friendly)
│   └── idf_env.bat            activate ESP-IDF env and run a command
└── README.md
```

`arduino-cli`, the upstream Waveshare repo, ESP-IDF and flash backups are all
gitignored — follow the setup below to recreate them locally.

## Setup

### 1. Upstream sources (not in git)

```bash
git clone --depth 1 https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85B.git
```

Contains the official Arduino + ESP-IDF examples, the published factory firmware,
and the schematic. The ESP-IDF examples include `05_esp-brookesia` (an ESP-Brookesia
framework demo) and `06_qmi8658_test` (IMU check).

> **`05_esp-brookesia` is not the full shipping system.** It builds the same
> ESP-Brookesia *Phone* framework with the same dark stylesheet, but it only installs
> the `SquarelineDemo` app (`main/main.cpp`). The apps the shipped binary actually has
> — XiaoZhi, Video Player, Chipmunk2d, and the rest — are **not** in this source tree,
> so rebuilding it gives you a reduced system, not a byte-equivalent one. The
> prebuilt binary in `Firmware/` is the only faithful copy of the stock system, which
> is why backing it up matters.

### 2. Arduino path

```bash
# arduino-cli: https://github.com/arduino/arduino-cli/releases  -> drop into tools/
arduino-cli config init
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.2.0

# Waveshare's bundled libraries (lvgl 8.4, SensorLib, ES8311/ES7210, ...)
cp -r ESP32-S3-Touch-LCD-1.85B/Examples/Arduino-V3.2.0/libraries/* \
      "$(arduino-cli config get directories.user)/libraries/"
```

If you are behind a proxy: `arduino-cli config set network.proxy http://127.0.0.1:7897`

### 3. ESP-IDF path (needed to modify the stock system)

```bash
git clone -b v5.5.3 --depth 1 --shallow-submodules --recursive \
    https://github.com/espressif/esp-idf.git
cmd.exe //c "tools\idf_install_steps.bat"        # installs the esp32s3 toolchain
cmd.exe //c "tools\idf_env.bat idf.py --version" # sanity check
```

## Build and flash

```bash
./tools/build.sh compile <sketch-dir>
./tools/build.sh upload  <sketch-dir> [port]     # default port COM3
./tools/build.sh both    <sketch-dir> [port]
```

Board options are baked into `build.sh`. The non-default ones matter:

| Option | Value | Why |
|---|---|---|
| `CDCOnBoot` | `cdc` | USB-C goes to the chip's native USB-Serial/JTAG; without this `Serial` never appears over USB |
| `FlashSize` | `16M` | onboard flash |
| `PSRAM` | `opi` | onboard octal PSRAM |
| `FlashMode` | `qio` | QIO 80MHz |
| `PartitionScheme` | `app3M_fat9M_16MB` | 3MB app + 9.9MB FATFS for assets |

## Back up before you flash

Dump the whole flash first — it is the only way back to a byte-exact stock state:

```bash
esptool --port COM3 --baud 921600 read_flash 0 0x1000000 backup/1.85B-stock-16MB.bin
```

Then restore any time with:

```bash
./tools/restore_stock.sh          # uses backup/ if present, else Waveshare's published image
```

## Gotchas worth knowing

**The native USB port can strand the chip in download mode.** The USB-C connector
goes straight to the chip's USB-Serial/JTAG peripheral — there is no USB-to-UART
bridge and no auto-reset transistor circuit. On the chip, asserting **DTR requests
download mode on the next reset**, and pyserial asserts DTR by default when a port
is opened. So a naive serial script can leave the board sitting at
`boot:0x2 (DOWNLOAD(USB/UART0)) / waiting for download` — black screen, unresponsive,
and plain resets keep landing back in download mode.

Recovery, in order of convenience:

1. **Watchdog reset via esptool** — usually enough to break the sticky state and boot
   the application, with no physical access needed:

   ```bash
   esptool --chip esp32s3 --port COM3 --baud 921600 \
       --after watchdog_reset --connect-attempts 1 read_mac
   ```

   A watchdog reset takes a different path than the RTS/EN reset, so it does not
   re-enter download mode. Verify success by attempting a connect with **no reset** —
   *if it times out, the application is running* (a live app never answers the ROM's
   sync handshake):

   ```bash
   esptool --chip esp32s3 --port COM3 --before no_reset --after no_reset read_mac
   # "Write timeout" / connection failure here means SUCCESS
   ```

   Careful: a plain `esptool ... read_mac` uses `--before default_reset`, which puts
   the chip back into download mode. Only the `no_reset` form is a safe probe.

2. **Cold power cycle** — always works: unplug USB (and if a battery is attached,
   long-press PWR 2s to power off, then short-press to power on). A cold boot samples
   the strapping pins correctly with GPIO0 pulled high.

Use `tools/serial_monitor.py` instead of ad-hoc pyserial code — it never asserts DTR
and never pulses RTS, so it attaches and detaches without disturbing a running app:

```bash
python tools/serial_monitor.py COM3 115200
python tools/serial_monitor.py COM3 --seconds 20     # bounded capture
```

**Never let esptool rewrite the image header when restoring a dump.** When writing
a raw flash dump at `0x0`, esptool patches the flash-parameter byte (offset 2, SPI
mode) if you pass `--flash_mode` / `--flash_size` / `--flash_freq`. The stock
bootloader's header is marked **DIO** — it upgrades itself to QIO at runtime — so
forcing `--flash_mode qio` there leaves the bootloader unable to read flash and the
board watchdog-resets in a loop (`rst:0x7 TG0WDT_SYS_RST`), with nothing on the
console but the ROM banner.

Always restore with `keep`:

```bash
esptool --chip esp32s3 --port COM3 --baud 921600 write_flash \
    --flash_mode keep --flash_size keep --flash_freq keep --verify \
    0x0 backup/1.85B-stock-16MB.bin
```

esptool's "Hash of data verified" is not a safety net here — it checks the data
*after* patching, so it passes either way. `tools/restore_stock.sh` already does
this correctly.

**Two different LVGL versions.** The Arduino examples ship **LVGL 8.4.0**; the
ESP-IDF examples and the stock system use **LVGL 9.5.0**. The APIs are not
compatible (`lv_scr_act()` vs `lv_screen_active()`, the display/indev API was
reworked). Check which version any example you copy was written for.

**Flashing replaces the whole system, not an app.** The stock firmware is a single
firmware image, not an OS with installed apps. Flash a demo sketch and the stock UI
is gone — there is no "exit" gesture, and a power cycle boots straight back into the
demo. To get the stock system back, reflash it (`tools/restore_stock.sh`).

**Driving ESP-IDF scripts from Git Bash.** Two traps:

1. `cmd.exe /c script.bat` does not work from Git Bash — MSYS rewrites `/c` into a
   path. Use the doubled form: `cmd.exe //c script.bat`.
2. `idf_tools.py` and `install.bat` abort with *"MSys/Mingw is not supported"*
   whenever `MSYSTEM` is in the environment, which Git Bash sets for every child
   process. `tools/idf_install_steps.bat` and `tools/idf_env.bat` clear it first.

**Only 3 free GPIOs** (7, 8, 18) — everything else is taken by onboard peripherals
or the flash/PSRAM bus. Need more IO? Use an I²C expander on the exposed I²C
header. Details in [docs/hardware.md](docs/hardware.md).

**3.3V logic.** Classic 5V Arduino boards need level shifting before connecting.

## Roadmap

- [ ] Decide the first mini-game and the badge's content model
- [ ] Project skeleton with a launcher that switches between games
- [ ] IMU input layer — tilt / shake / flip gestures
- [ ] Badge assets: animated frames from SD card or the SPIFFS assets partition
- [ ] Optional: rebuild and extend the stock Brookesia system with ESP-IDF

## Links

- [Waveshare docs](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B)
- [Waveshare repo](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85B)
- [esp-brookesia](https://github.com/espressif/esp-brookesia) — the HMI framework the stock system is built on
- [LVGL](https://lvgl.io)
