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
and the schematic. The ESP-IDF examples include `05_esp-brookesia` (the stock
system's source) and `06_qmi8658_test` (IMU check).

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

**Two different LVGL versions.** The Arduino examples ship **LVGL 8.4.0**; the
ESP-IDF examples and the stock system use **LVGL 9.5.0**. The APIs are not
compatible (`lv_scr_act()` vs `lv_screen_active()`, the display/indev API was
reworked). Check which version any example you copy was written for.

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
