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
├── system/                    the firmware that runs on the badge (launcher + games)
├── games/clawd-roll/          the same game as a standalone sketch, no launcher
├── lib/
│   └── board_drivers/         shared Arduino library: ST77916 panel, CST816 touch, I2C
├── docs/
│   └── hardware.md            pin map, free GPIOs, expansion notes
├── tools/
│   ├── build.sh               compile / flash helper (arduino-cli)
│   ├── deploy_game.sh         build a sketch, write it into ota_0, boot it
│   ├── switch_slot.py         choose which slot the bootloader starts
│   ├── restore_stock.sh       restore the factory firmware
│   ├── serial_monitor.py      non-intrusive monitor (never touches DTR/RTS)
│   ├── convert_sprite.py      flat-colour art -> 2bpp indexed C header
│   ├── make_font.py           bake an 8x8 ASCII bitmap font
│   ├── make_icon.py           render the project icon
│   ├── idf_install_steps.bat  install ESP-IDF toolchain (Git Bash friendly)
│   └── idf_env.bat            activate ESP-IDF env and run a command
└── README.md
```

`arduino-cli`, the upstream Waveshare repo, ESP-IDF, build output and flash backups
are all gitignored — follow the setup below to recreate them locally.

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

## The system

The firmware that occupies `ota_0` is the project's own little system: it boots into
a launcher ring of games, and each game is an independent module behind the `Game`
interface. Adding one means writing a class and appending a pointer to `GAMES` in
`system/system.ino`.

```
system/
├── system.ino            setup/loop, screen dispatch, the game registry
├── gfx.{h,cpp}           framebuffer primitives, rotated 2bpp sprites, 8x8 text
├── input.{h,cpp}         tilt (with auto-trim), touch, BOOT button
├── arena.{h,cpp}         the shared backdrop: wall ring, floor dots
├── launcher.{h,cpp}      the carousel home screen
├── game.h                the Game interface
├── game_clawd.{h,cpp}    tilt-controlled rolling clawd
├── game_tap.{h,cpp}      touch-controlled clawd tap
├── clawd_sprite.h        the sprite + palette, shared by both games
├── assets_clawd.h        generated 2bpp sprite
├── assets_font.h         generated 8x8 ASCII font
└── partitions.csv        must match the stock layout — see Coexistence below
```

**Controls.** Tilt the board left or right to step the carousel — one lean moves one
place and then latches until the board is level again, so a held tilt does not
free-run through the list. Tap an icon to play it, or tap the middle to play the
selected one. Hold BOOT for a moment to come back from a game to the launcher; hold
it longer *on* the launcher to hand the board back to the stock system. A ring in the
bottom corner fills while you hold, so the gesture is discoverable.

```bash
./tools/deploy_game.sh system          # compile, flash into ota_0, boot it
./tools/deploy_game.sh games/clawd-roll   # or deploy the standalone game sketch
python tools/make_font.py              # regenerate the bitmap font
python tools/make_icon.py              # regenerate the project icon
```

Rendering bypasses LVGL completely: the firmware owns one 360×360 RGB565 framebuffer
in PSRAM and pushes bands of it to the panel. That keeps the frame loop cheap and
sidesteps the LVGL 8.x/9.x split between the two example trees. Each screen reports
which rows it changed (`Game::dirtyY0/dirtyY1`) so a game that moves one sprite does
not pay for a full-screen transfer every frame.

**Tilt input.** The axis perpendicular to the screen is found at boot (it is the one
carrying gravity while the board lies flat), so only the in-plane mapping is
hard-coded. There is deliberately *no* zeroing calibration: with gravity-sensed tilt,
level *is* zero, and subtracting a boot-time snapshot only bakes in whatever angle
the board happened to be held at — worse, the constant residual that leaves behind
pins a ball against a wall. Instead a slow auto-trim removes the accelerometer's
static offset, but only while the board is near level, so it never fights a player
who is deliberately leaning in.

## Games

### clawd-roll

<img src="games/clawd-roll/icon.png" width="180" alt="clawd-roll icon">

Tilt the board and clawd rolls; let up and it coasts; hit the arena wall and it
bounces. No score, no goal — it is a thing that feels good to wobble.

Also buildable as a standalone single-game firmware, which is useful when you want to
iterate on the physics without the launcher in the way:

```bash
./tools/deploy_game.sh games/clawd-roll
python tools/convert_sprite.py games/clawd-roll/clawd-source.png \
       games/clawd-roll/clawd_asset.h --name clawd --body E83B3B
```

### clawd-tap

Touch counterpart: clawd pops up somewhere in the arena, waits a shrinking moment,
and vanishes. Tap it to score, three time-outs and it is over. It exists as much to
exercise the game interface as to be played — between it and clawd-roll both input
paths and both ends of the drawing cost are covered.

## Coexistence: our system and the stock system

Both firmwares are flashed and neither is overwritten. Switching rewrites only the
8 KB `otadata` selector, so it takes about a second rather than the ~70 s a full
reflash costs:

```
factory  app  0x020000  1.00MB   our system (launcher + games)
ota_0    app  0x120000  9.62MB   the stock Waveshare system, including the XiaoZhi AI app
assets   data 0xac0000  4.00MB   stock media partition, left where it was
```

**The slots are the opposite way round from how the board shipped**, and that is the
whole trick. `tools/migrate_layout.py` performs the swap; here is why it works.

The stock firmware calls `esp_ota_set_boot_partition(factory)` on every boot, and for
a factory partition ESP-IDF just *erases otadata*:

```c
// components/app_update/esp_ota_ops.c
// if set boot partition to factory bin ,just format ota info partition
if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    return esp_partition_erase_range(find_partition, 0, find_partition->size);
}
```

With the stock system in `factory` that behaviour was a trap: it pinned the boot
target there, so leaving our system meant never getting back without a computer.
Moving *our* firmware into `factory` turns the same behaviour into the way home.

So the round trip needs no host at all:

| | |
|---|---|
| launcher → tap **XIAOZHI** (or hold BOOT on the launcher) | writes otadata selecting ota_0, restarts → stock system |
| power cycle in the stock system | it has already re-pinned the boot target to factory → back to our launcher |

The launcher is what boots by default, because erased otadata falls back to `factory`.

```bash
python tools/switch_slot.py home     # boot our system (erases otadata)
python tools/switch_slot.py stock    # boot the stock system (selects ota_0)
python tools/switch_slot.py status   # report what is currently selected
python tools/migrate_layout.py       # re-apply the layout swap; --dry-run to preview
```

`assets` deliberately keeps the offset the stock layout gave it, so the swap never has
to copy those 4 MB. The bootloader is not rewritten either — it reads the table from
flash at runtime.

Reverting is `./tools/restore_stock.sh`, which writes the original 16 MB dump back
verbatim and with it the original layout.

### Why the games are not entries in the stock system's launcher

That would be the natural place for them, and it is not possible with what Waveshare
ships:

- The stock system is a **prebuilt binary**. Nothing in the published repository is
  its source: `05_esp-brookesia` builds the same ESP-Brookesia *framework* with the
  same stylesheet, but installs only the `SquarelineDemo` app, and the
  `brookesia_core` component contains framework code only — no settings, weather,
  music, video or game apps at all.
- The `assets` partition is a packed resource table, not an app filesystem. Reading
  it back off the device shows fonts, `srmodels.bin` (speech models) and an
  `index.json` — there is no runtime app loading and no app store in this build.

Upstream ESP-Brookesia does have a runtime-app mechanism (`runtime/brookesia_runtime_js`,
`_lua`, `_wasm`, `_elf`, plus `app/brookesia_app_store`), so a system built from
source *could* list the game in a launcher. That is the path to take if a launcher is
wanted — but it means building our own system, which would not contain the stock
apps such as XiaoZhi.

This works because `games/clawd-roll/partitions.csv` is byte-identical to the table
already on the device — the offsets were read back from the stock bootloader's own
log, and the build output is parsed to confirm it before anything is flashed. A
mismatch would send runtime partition lookups to the wrong regions.

The `otadata` entry format has three traps worth knowing if you touch the code:
the checksum covers **only the 4-byte `ota_seq`**, not the whole struct; ESP-IDF's
`esp_rom_crc32_le` inverts both its input and its output (it is *not* zlib's crc32);
and `ota_state` must be `VALID`, because anything the bootloader finds in
`PENDING_VERIFY` or `NEW` is treated as unconfirmed and rolled back.

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

**The touch controller stops answering I2C when it is idle.** `Touch_Init()` leaves
the CST816S in auto-sleep, and a sleeping controller does not respond on the bus — so
code that polls it every frame produces a continuous stream of
`i2c.master: I2C transaction unexpected nack detected` until the screen is touched.
The vendor function that is supposed to control this cannot:

```c
void CST816_AutoSleep(bool Sleep_State) {
  ...
  uint8_t Sleep_State_Set = (uint8_t)(!Sleep_State);
  Sleep_State_Set = 10;                              // overwrites the computed value
  I2C_Write_Touch(CST816_ADDR, CST816_REG_DisAutoSleep, &Sleep_State_Set, 1);
}
```

The register needs `0x01` to disable auto-sleep; it always receives `10`. The
library's `Touch_SetAutoSleep(false)` writes the correct value without the controller
reset that the vendor function also performs, and `input::init()` calls it.

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

**Anything that talks to the serial port must finish by booting the app.** On this
board a chip left in the ROM download loader looks completely dead — black screen, no
response to the power button — while still enumerating over USB, and a power cycle is
not an escape because USB keeps it powered. Worse, it is invisible from the host side:
every subsequent `esptool` call resets on connect and sees a perfectly healthy boot,
so the tooling keeps reporting success while the screen stays dark. Always end with
`--after watchdog_reset`, never leave a `--after no_reset` step as the final one.
