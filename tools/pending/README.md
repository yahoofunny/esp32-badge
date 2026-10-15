# Work in progress, not in the build

Files here are **not** compiled: arduino-cli only builds `system/`, so anything parked here
is inert. That is the point -- a half-finished app left inside the sketch folder either
breaks the build or, worse, compiles and looks like it works.

## game_music.{h,cpp}

The music player for `/Music`. Written, does not compile yet.

State: `#include "Audio.h"` resolves but `class Audio` does not become visible -- no
"file not found", so the header is being found and something inside it is not reaching the
class. Next things to try, in order:

1. `arduino-cli compile --verbose` and look at the include paths it actually passes, to see
   which `Audio.h` is winning.
2. The manual install is `ESP32-audioI2S-master` in the sketchbook; the registry version is
   named differently. Try installing from the registry and letting it manage the folder
   name.
3. Failing that, include the header by its library-qualified form, or drop the library and
   drive the decoder through ESP-IDF's `esp_audio_codec`.

Design notes that are worth keeping whatever the wrapper turns out to be:

* **It is a mode, entered by rebooting.** The library installs its own I2S on the same pins
  this system's sound effects use, and the Arduino `I2SClass` has no `end()` -- there is no
  way to hand the peripheral over. In player mode `setup()` never starts the sound effects'
  I2S at all. An NVS flag means "boot into the player once" and is cleared as the player
  starts, so a crash costs one ordinary boot rather than a loop.
* `Audio` is constructed on the heap, not as a global. A global constructor runs before
  `setup()`, and exactly that was enough to make the LCD's first SPI transfer fail when the
  image decoders were linked that way -- see commit c9c593b.
* The library has a destructor (`~Audio()`), so if it does release the I2S cleanly then the
  reboot on exit could be dropped. Untested.

## upload.{h,cpp} + badge_put.py — 串口传文件（写完了，但传不过去）

目的：不拔卡、不断开 badge，从主机把图片/音乐放进 `/Photos` `/Music`。
协议：主机发 `W<字节数> <路径>`，badge 回 `[file] ready`，字节流随后，badge 再回
`[file] wrote <路径> <字节数> crc32=0x…`；CRC 用 zlib 那一版（不是 IDF 那个反转进出的）。
badge 侧从主循环分块喂，不阻塞渲染；只允许写两个内容文件夹。

**实测卡在哪（2026-10-15）**：
1. **必须先搞清楚 DTR**：这块板子的硬件 CDC（`USBMode=hwcdc`）**只发不收**，
   除非主机把 **DTR 拉高**。拉高之后小命令立刻通了（授时命令能写进 RTC）。
   而真正会触发下载模式的是 **RTS 的抖动**，不是 DTR —— 所以工具的正确写法是
   "拉高 DTR、绝不碰 RTS"，`tools/set_time.py` / `serial_monitor.py` 里那句
   "DTR 一律不拉" 的注释是**错的**，代价是命令永远进不去。
2. **大块数据过不去**：命令（约 30 字节）能进，但 100 字节的负载就一个字节都进不去，
   badge 侧 `Serial.available()` 一直是 0（固件准时在 10 秒后报 `timeout`）。
   加了发送节奏也没用 —— 症状指向这块芯片硬件 CDC 的接收缓冲很小、突发即丢。
   **下一步**：把协议改成"逐行 base64 + 每行一个确认"，完全走已经验证可用的
   "一行一行"通道，用握手代替缓冲。
