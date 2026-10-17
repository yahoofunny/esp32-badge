# esp32-badge · CLAWD PET

**简体中文** | [English](README.en.md)

一只运行在圆屏 ESP32 上的电子吧唧桌宠：**养成、换装和小智语音聊天，放进同一个 CLAWD PET**。
硬件是 Waveshare **ESP32-S3-Touch-LCD-1.85B**，1.85 英寸 360×360 触摸屏，带六轴传感器、麦克风和扬声器接口。

- **同一只宠物**：蛋、七种成长形态、喂食、抚摸、拖动、休息和清洁；对话时保留形态与配饰，显示表情和中文字幕。
- **20 种像素配饰**：花朵、安全帽、耳机、巫师帽等叠加在原身体上，体型和比例保持一致；衣柜支持预览、取消和保存。
- **手指拖转主页**：沿应用圆环滑动，松手吸附，点击打开；主页没有左上角时钟。
- **GAMES 游戏文件夹**：四个小游戏收进独立圆环，操作与主页一致，退出游戏回到文件夹。
- **满屏相册**：左右滑动或点击左右两侧翻页，底部显示页码；长按约 **650ms** 保持原来的全屏 / 原显示状态切换。
- **24 个内置 Clawd 动图**：打字、思考、睡觉、扫地、戴耳机和迷你形态等；原样保留动画，离线可看，SD 卡照片继续追加。
- **HYPNO 惯性摆钟**：随设备左右晃动摆起，停止晃动后逐渐衰减；点击暂停/继续，底部减号、加号切换三档灵敏度。
- **SNAKE 贪吃蛇 / BRICKS 打砖块**：点击蛇头周围转向，触摸左右半屏控制挡板；均支持暂停、得分、当次开机最高分和重新开始。
- **MUSIC 本地播放器**：SD 卡 MP3、WAV 等音频，上一首/下一首、暂停/继续、进度和音量；空音乐目录可自动生成原创演示旋律。
- **Wi-Fi 传输**：设置中开启独立热点，手机或电脑通过网页批量上传音乐和照片，无需拔卡。
- **USB 连接提示与省电休眠**：电脑插拔及零电流读数的自动恢复已通过实机验证；CHARGE ONLY 熄屏休眠，按 BOOT 唤醒。充电状态仅显示在设置里。
- **触摸与绘制分开调度**：输入目标 **20ms**，本机实测约 **30ms**；省电只降低绘制频率，按下即唤醒并高亮，松手执行一次，反馈保留约 **150ms**。

实机验证：主页拖转、桌宠互动、语音联网对话和相册显示已通过；扫描间隔会随运行负载变化。
2026-10-17 已确认 GAMES 打开游戏及 BACK 返回、贪吃蛇底部暂停、打砖块左右半屏点击与按住均正常。

| 主页 | CLAWD PET 养成 |
|---|---|
| ![圆环应用主页，软件渲染](docs/images/launcher.png) | ![带像素配饰的养成页，软件渲染](docs/images/pet-care.png) |
| 衣柜 | 小智语音 |
| ![保持同一身体比例的衣柜，软件渲染](docs/images/pet-wardrobe.png) | ![宠物语音状态，软件渲染](docs/images/pet-voice.png) |

| GAMES 游戏文件夹 | SNAKE 贪吃蛇 | BRICKS 打砖块 |
|---|---|---|
| <img src="docs/images/games-folder.png" width="240" alt="GAMES 环绕菜单，四个游戏加 BACK，实际 C++ 软件渲染"> | <img src="docs/images/snake.png" width="240" alt="贪吃蛇示例对局，底部大范围暂停，实际 C++ 软件渲染"> | <img src="docs/images/bricks.png" width="240" alt="打砖块示例对局，左右半屏控制及底部暂停，实际 C++ 软件渲染"> |

*图示由共享 C++ 绘制代码生成，非 LCD 实拍；语音图不包含 LVGL 中文字幕覆盖层。*

<img src="docs/images/hypno.png" width="240" alt="HYPNO 金色怀表与同心圆，实际 C++ 软件渲染">
<img src="docs/images/music.png" width="240" alt="MUSIC 黑胶唱片界面，实际 C++ 软件渲染，曲目和时间为演示数据">
<img src="docs/images/photos.png" width="240" alt="PHOTOS 相册，设备实际 framebuffer 读回：透明背景与页码">

快速入口：**[编译与更新](#编译与更新)** · **[语音集成与操作](docs/clawd-pet-voice.md)** · **[硬件说明](docs/hardware.md)** · **[验证与预览](#验证与预览)** · **[固件分区](#固件分区)**

本仓库包含启动器、小游戏、共享宠物模型、ESP-IDF 语音集成、工具和硬件说明。
上游 SDK、本地闪存备份及编译产物不纳入仓库；获取方式见[环境准备](#环境准备)。

<a id="hardware"></a>

## 硬件

| 项目 | 规格 |
|---|---|
| 主控 | ESP32-S3R8，双核 LX7，240MHz |
| 内存 | 512KB SRAM + **8MB OPI PSRAM** |
| 闪存 | **16MB** NOR |
| 屏幕 | 1.85 英寸圆形 IPS，360×360，**ST77916**，QSPI，RGB565 |
| 触摸 | **CST816S**，I²C `0x15` |
| 惯性传感器 | **QMI8658** 六轴，I²C `0x6B`，±2–16g / ±32–4096dps |
| 音频 | ES8311 编解码器 `0x30` + ES7210 回声消除 `0x80`，双麦克风、扬声器焊盘 |
| 其他 | PCF85063 实时时钟 `0x51`，BQ27220 电量计 `0x55`，microSD（4 位 SDMMC） |
| 无线 | 2.4GHz Wi-Fi b/g/n + BLE 5 |
| 供电 | USB-C 或 MX1.25 接口的 3.7V 锂聚合物电池，板载充电 |

引脚表、空闲 GPIO 分析与扩展说明见 **[硬件文档](docs/hardware.md)**。

USB-C 直接连接芯片原生 USB-Serial/JTAG（`VID:PID = 303A:1001`），板上没有 USB 转 UART 桥接芯片。

<a id="repo-layout"></a>

## 仓库结构

```
esp32-badge/
├── system/                    吧唧启动器与小游戏固件
├── firmware/clawd_pet/        ESP-IDF 语音显示、输入适配及界面调度
├── assets/clawd-gifs/         相册与配饰参考使用的 24 个原始动画
├── games/clawd-roll/          不含启动器的独立游戏工程
├── lib/
│   ├── board_drivers/         ST77916、CST816 触摸及 I²C 的共享 Arduino 驱动
│   └── badge_audio_i2s/       本地音频解码器、256fs MCLK 及 I²S 释放修正
├── docs/
│   └── hardware.md            引脚、空闲 GPIO 与扩展说明
├── tools/
│   ├── build.sh               arduino-cli 编译与烧录辅助工具
│   ├── deploy_game.sh         编译、仅写入 factory 应用并启动
│   ├── prepare_clawd_voice.py 准备固定版本小智源码及共享宠物集成
│   ├── install_voice_pet.py   验证、备份及安装 ota_0 语音宠物
│   ├── switch_slot.py         选择启动分区
│   ├── restore_stock.sh       恢复原厂固件
│   ├── serial_monitor.py      原生 CDC 串口监视，启用 DTR、保持 RTS 无效
│   ├── convert_sprite.py     将纯色图转换为 2bpp 索引 C 头文件
│   ├── make_font.py           生成 8×8 ASCII 位图字体
│   ├── make_icon.py           生成项目图标
│   ├── idf_install_steps.bat  安装 ESP-IDF 工具链，兼容 Git Bash
│   └── idf_env.bat            激活 ESP-IDF 环境并执行命令
├── README.md                  中文说明
└── README.en.md               英文说明
```

`arduino-cli`、上游 Waveshare 仓库、ESP-IDF、编译产物和闪存备份均已加入 Git 忽略规则，可按下文在本地重新准备。

<a id="setup"></a>

## 环境准备

<a id="1-upstream-sources-not-in-git"></a>

### 1. 获取上游源码

```bash
git clone --depth 1 https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85B.git
```

上游仓库包含官方 Arduino 与 ESP-IDF 示例、发布的原厂固件和原理图。
ESP-IDF 示例包括 `05_esp-brookesia`（ESP-Brookesia 框架演示）和 `06_qmi8658_test`（惯性传感器测试）。

> **`05_esp-brookesia` 不是完整的出厂系统源码。** 它使用相同的 ESP-Brookesia Phone 框架和深色样式，
> 但只安装 `SquarelineDemo` 应用（`main/main.cpp`）。出厂二进制中的小智、视频播放器、Chipmunk2d 等应用
> 不在该源码树中，因此重新编译只会得到精简演示系统，不能得到逐字节一致的原厂系统。
> `Firmware/` 中的预编译二进制才是原厂系统的完整副本，因此需要保留备份。

<a id="2-arduino-path"></a>

### 2. Arduino 环境

```bash
# 从 https://github.com/arduino/arduino-cli/releases 下载 arduino-cli，放入 tools/
arduino-cli config init
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.2.0

# Waveshare 附带的库（LVGL 8.4、SensorLib、ES8311/ES7210 等）
cp -r ESP32-S3-Touch-LCD-1.85B/Examples/Arduino-V3.2.0/libraries/* \
      "$(arduino-cli config get directories.user)/libraries/"
```

需要代理时：`arduino-cli config set network.proxy http://127.0.0.1:7897`。

<a id="3-esp-idf-path-for-the-integrated-voice-pet"></a>

### 3. ESP-IDF 环境（集成语音宠物）

```bash
git clone -b v5.5.3 --depth 1 --shallow-submodules --recursive \
    https://github.com/espressif/esp-idf.git
cmd.exe //c "tools\idf_install_steps.bat"        # 安装 esp32s3 工具链
cmd.exe //c "tools\idf_env.bat idf.py --version" # 检查工具链是否可用
```

<a id="build-and-update"></a>

## 编译与更新

首次安装前先[备份完整闪存](#烧录前备份)。本项目将 Arduino 启动器放在 `factory`，
将集成语音宠物放在 `ota_0`，两者沿用设备已有的分区表。

在 Git Bash 中编译或更新启动器：

```bash
PARTITION_SCHEME=custom ./tools/build.sh compile system  # 仅编译
./tools/deploy_game.sh system COM3                     # 编译、写入 factory 应用并启动
```

此布局应使用仅更新应用的部署工具。通用的 `build.sh upload/both` 采用 Arduino 上传流程，
可能同时写入引导程序和分区表，不适合作为这套双固件布局的更新方式。

在仓库根目录使用 PowerShell 编译语音宠物：

```powershell
python tools/prepare_clawd_voice.py
$voiceBuild = Join-Path (Get-Location) "build/clawd-voice"
cmd /c tools\idf_env.bat idf.py -C reference/xiaozhi -B "$voiceBuild" set-target esp32s3 # 首次编译时执行
cmd /c tools\idf_env.bat idf.py -C reference/xiaozhi -B "$voiceBuild" build
python tools/install_voice_pet.py          # 验证产物，预演但不写入
python tools/install_voice_pet.py --flash  # 备份当前分区和配置，仅写 ota_0
```

语音集成使用小智 **v2.3.0**、ESP-IDF **5.5.3** 及固定版本依赖。
安装工具需要本地完整备份来验证分区表，并在写入前检查连接的设备。安装后从启动器打开 CLAWD PET。
不要直接运行 `idf.py flash`：自动生成的烧录参数指向 `factory`，并包含引导程序和分区表写入。
详细步骤见[语音宠物编译与操作](docs/clawd-pet-voice.md)。

`build.sh` 已设置板卡选项，下列非默认值关系到设备能否正常运行：

| 选项 | 值 | 原因 |
|---|---|---|
| `CDCOnBoot` | `cdc` | USB-C 接原生 USB-Serial/JTAG；未启用时 USB 上不会出现 `Serial` |
| `FlashSize` | `16M` | 板载闪存容量 |
| `PSRAM` | `opi` | 板载八线 PSRAM |
| `FlashMode` | `qio` | QIO 80MHz |
| `PartitionScheme` | `custom` | 使用 `system/partitions.csv`，匹配已有的 factory / ota_0 / assets 布局 |

`build.sh` 默认使用通用 Arduino 分区方案，因此上面的 `PARTITION_SCHEME=custom` 不能省略；
`deploy_game.sh` 会自动设置该选项。

<a id="verification"></a>

## 验证与预览

```bash
python tools/native_checks.py      # 在主机运行真实共享 C++ 逻辑，需要 g++
python tools/preview_pet.py        # 渲染共享宠物代码，还需要 Pillow
python tools/preview_launcher.py   # 使用 Python 预览圆环布局
python tools/preview_readme.py     # 从实际 C++ 绘图重新生成说明图片，需要 Pillow
python tools/preview_power.py      # 以充电测试数据渲染 POWER 页面
python tools/preview_hypno.py --animation # 实际 C++ 画面，可选生成摆动 GIF
python tools/preview_music.py      # 以固定曲目测试数据渲染音乐界面
python tools/preview_minigames.py  # 运行真实小游戏逻辑、生成截图并更新主页图
python tools/make_clawd_album.py --check # 检查内置 GIF 与原始资源一致
```

检查覆盖相册缩放与裁切边界、圆环拖动与吸附、旧存档迁移、养成、衣柜保存、两种 RGB565 字节序下的配饰裁切、
按钮按下与松手反馈、BQ27220 充电状态、HYPNO 传感器运动/灵敏度/暂停，以及毫秒计数回绕时的界面调度。
主机软件渲染用于补充实机检查，不能测量 LCD 传输或触摸控制器延迟。

启动器将 PSRAM 中的像素复制到可复用的 8 行内部 DMA 缓冲区，每条传输结束后才复用缓冲区，
避免大块传输失败造成底部红条残留。串口 `D` 包含已完成的传输计数；传输错误和超时会明确记录。

<a id="the-system"></a>

## 系统结构与功能

`factory` 中运行的是本项目自己的小型系统，启动后显示应用圆环。
每个应用通过 `Game` 接口独立实现；添加应用时在 `system/system.ino` 的 `GAMES` 中注册，
添加小游戏则注册到 `MINI_GAMES`。

```
system/
├── system.ino            初始化、主循环、页面分发和应用注册表
├── gfx.{h,cpp}           帧缓冲绘图、旋转 2bpp 精灵、8×8 文字
├── input.{h,cpp}         带自动微调的倾斜输入、触摸及 BOOT 按键
├── arena.{h,cpp}         场地边框与背景点阵
├── launcher.{h,cpp}      主页和文件夹共用的圆环及导航图标
├── game.h               Game 接口
├── game_clawd.{h,cpp}    倾斜控制的滚动 Clawd
├── game_tap.{h,cpp}      点击 Clawd 小游戏
├── game_snake.{h,cpp}    15×15 贪吃蛇、蛇头相对点击、转向队列和暂停
├── game_bricks.{h,cpp}   左右半屏控制的打砖块、20 块砖、三条命和暂停
├── game_pet.{h,cpp}      宠物养成、蛋、进化、手势及语音表现
├── outfits.{h,cpp}       叠加在原宠物身体上的 20 种配饰
├── game_photos.{h,cpp}   SD 卡相册及全屏裁切/完整显示切换
├── clawd_album.{h,cpp}   直接从闪存读取的 24 个原始 Clawd GIF
├── game_hypno.{h,cpp}    传感器驱动的怀表、暂停及运动灵敏度
├── game_music.{h,cpp}    SD 音乐播放器、控制及音频驱动管理
├── game_settings.{h,cpp} 音量与系统设置
├── power.{h,cpp}         BQ27220 电量计
├── clawd_sprite.h        共享精灵与调色板
├── assets_clawd.h        生成的 2bpp 精灵
├── assets_icons.h        生成的应用图标
├── assets_mon.h          生成的七种宠物身体
├── assets_font.h         生成的 8×8 ASCII 字体
└── partitions.csv        必须匹配设备现有布局，详见固件分区
```

主页有 CLAWD PET、PHOTOS、MUSIC、HYPNO、GAMES、SETTINGS 六个入口。
GAMES 内是包含 CLAWD ROLL、CLAWD TAP、SNAKE、BRICKS 和 BACK 的第二层圆环。
两层圆环共用拖转、点击和倾斜操作，并分别记住选中位置。各模块自行绘制图标；宠物养成与语音共用同一入口。
CLAWD PET 保留经典橙色主页图标，进入应用后，身体颜色随当前进化形态变化。

电池电压、百分比、带正负号的电流及 BatteryStatus 均来自 BQ27220 电量计。
SETTINGS → POWER 在收到 USB 主机数据包时单独显示 USB CONNECTED，同时按实际测量显示
CHARGING、DISCHARGING、FULL 或 CHARGE UNKNOWN。延迟更新的 DSG 标志不能覆盖新电流读数。
仅插线或显示 100% 不能证明正在充电或已充满；状态读取失败显示 STATUS UNKNOWN。
关闭电脑串口监视器后仍能检测 USB 连接。充电宝和普通充电器不发送主机数据包，
因此没有 USB 标签不能证明线没插上，电池电流检测与 USB 数据通信彼此独立。
零电流显示 CHARGE UNKNOWN，因为它无法确认充电芯片状态，不会显示成“没有充电”。
串口 `B` 报告 USB 状态、插拔计数及只读电量计寄存器；`S` 打开设置页供显示检查。

**电池容量必须匹配实物。** BQ27220 的默认容量为 3000mAh，不能据此认定实际装有 3000mAh 电池。
本机用户确认电池为 **500mAh / 1.85Wh，充电限制电压 4.2V**。串口 `C500` 显式配置该电池：
备份原参数后，只修改电量计 RAM 中的满电容量和设计容量，读回校验并重新初始化估算；
配置成功后将容量保存到本机 NVS，在启动器开机发现电量计回到默认容量时恢复。
未配置的设备不自动套用 500mAh，也不覆盖正常学习得到的满电容量。
此操作不烧写 OTP，不修改充电芯片、电流校准、电池化学曲线或电压/温度保护。
修正容量不等于立即完成电池校准；新的百分比仍是估算值，需要后续实际充满和正常使用核对。
本机已读回确认两项容量均为 500mAh，主控重启后仍保留；重新估算约 80%，正向电流约 +150mA。

**重插后零读数的自动恢复已通过实机验证（2026-10-17）。** 此前重插电脑 USB 后，电量计会持续报告
`0mA`、BatteryStatus `0x5029/0x5028`。不重启的对照证明，短暂降负载后读数可恢复为正电流，恢复正常负载后仍保持充电。
固件现在仅在电脑连接、读数有效、零电流且 SLEEP 标志持续 2 秒时，尝试一次最多 4 秒的降负载恢复。
屏幕可能短暂熄灭约 2～4 秒，随后回到原页面，无需按 BOOT；每次实际连接最多尝试一次，避免反复熄屏。
用户已确认重插后自动恢复并显示 CHARGING，后续串口连续采样约为 **+155mA**。
完整测试数据见 [充电诊断记录](docs/charging-diagnostics.md)。

异常的完整电气原因仍未确定，任意充电器兼容性与充满状态尚未完整验证。恢复失败仍显示 **CHARGE UNKNOWN**。
正电流表示电量计测到电池有净流入电流，不代替充电芯片 STAT 状态，也不会用 USB 连接直接判定充电。

**SETTINGS 设置**通过小型弧形菜单提供 VOLUME、SYSTEM、NET、USB、POWER 和 WIFI 页面。
VOLUME 与语音助手共用音量，保存在助手 `AudioCodec::Start()` 读取的 NVS 键中；
使用该键而非编解码器寄存器的原因见 `system/audio.h`，因为助手会在每次启动时重写寄存器。
SYSTEM 显示芯片、内存、编译及电池信息；NET 显示已保存的助手配置；USB 提供 SD 卡访问模式；
WIFI 开启本地媒体传输热点。集成语音宠物保留自己的助手联网配置。

**无线传音乐和照片：SETTINGS → WIFI → START HOTSPOT。**

1. 手机或电脑连接吧唧显示的 `BADGE-xxxx` 热点，输入屏幕上的密码。
2. 保持此热点连接，即使系统提示“没有互联网”；浏览器打开 `http://192.168.4.1`。
3. 选择音乐或照片，可批量上传，网页显示进度和 SD 卡文件列表。
4. 完成后在吧唧点 STOP / BACK，或长按 BOOT 返回主页，再打开 MUSIC / PHOTOS 即可重新扫描。

音乐支持 MP3、WAV、M4A/AAC、FLAC、OGG。音乐选择器显示所有文件，选中后再检查格式，
避免部分手机将 FLAC 错误置灰；从手机“文件”中选择即可。网页将静态 JPG/PNG/WebP 转为最长边
1280 像素的 JPG，兼容相册解码器；GIF 和 BMP 原样上传，GIF 保留动画。
单首音乐上限 **512 MiB**，单张照片上限 50 MiB，每个文件夹最多 64 个媒体文件。
上传以不超过 4 KiB 的缓冲区直接写入 SD 卡，音乐原样保存，不自动重新压缩或转码。
小网络包会合并到 4 KiB 再写卡，避免每收到一个 TCP 包就进行一次零碎写入；串口保存日志报告总耗时、
接收等待、写卡耗时和实际平均 KiB/s，不能将网页上传速度直接当作 Wi-Fi 硬件的极限。
按钮旁会显示选中文件大小、准备状态、已传字节、进度及错误，并提供取消按钮。
持续传输没有五分钟总时限；连续 45 秒无进度会停止等待，设备也会清理未完成文件。
请保持手机亮屏、传输网页在前台。手机息屏或切换应用可能挂起网页；当前版本不支持断点续传，
中断后需要重新选择并上传，未完成的临时文件不会进入歌单，意外断电遗留的临时文件在下次启动热点时清理。
同名文件自动添加数字后缀，
已有内容不覆盖；上传中断会删除临时数据，完整写入后才加入歌单或相册。
上传由独立 HTTP 任务分块写卡，界面仍可操作。退出会等待上传任务结束、卸载 SD 并关闭热点，
不修改小智的 Wi-Fi 配置。热点有独立的设备密码，保存在 `badge-transfer` 命名空间。

上传原生 FLAC 后，设备读取 STREAMINFO 并报告采样率、位深与声道数，不解码或修改文件。
当前解码器支持 8–24 位、单/双声道且帧块不超过 8192 个采样的 FLAC。
预测计算与双声道还原保留原始位深，最后转换为 16 位 PCM 送至音频输出；SD 卡上的 FLAC 不修改。
修复了高位深 LPC 计算溢出、单声道输出间隔以及 STREAMINFO 参数未传入解码器的问题；
输入缓冲区扩大至 256 KiB，并为 24 位双声道帧预留足够空间。解码停止时显示播放失败，避免停在 0:00 却仍显示正在播放。
FLAC 计时按照解码输出的采样数推进，避免可变码率和分段输出导致进度跳变。
更高采样率的实际播放性能仍需按文件测量，不能仅凭
扩展名或上传成功判定。音频播放性能用实际 I2S 输出帧数随时间的增长验证。

<img src="docs/images/wifi-transfer.png" width="240" alt="WIFI 传输页面，实际 C++ 渲染，热点与密码均为示例">
<img src="docs/images/wifi-transfer-web.png" width="240" alt="手机传输网页的浏览器测试截图，文件列表为测试数据">

传输检查：`python tools/native_checks.py` 覆盖真实上传处理函数的重名保护、分块写入、中断清理、
文件名与大小限制；`node tools/test_transfer_web.cjs` 验证网页批量上传、照片转换和 GIF 保留。
其中包括真实上传处理函数的 64 MiB 以上分块校验，以及浏览器中实际创建并上传 51 MiB
文件的回归检查，另覆盖按钮旁报错、取消、无进度超时及 FLAC 位深提示。
浏览器检查使用本机 Edge 和 Playwright（可用 `BADGE_NODE_MODULES` 指定依赖目录）。
`python tools/test_flac_decoder.py` 使用 FFmpeg 生成 16/24 位、单/双声道及不同编码方式的真实 FLAC，
编译生产解码器并逐个 PCM 采样与 FFmpeg 输出对比，包含 8192 采样帧和高位深预测计算。
实机已验证一首 54,393,782 字节的 24 位 / 48 kHz / 双声道 FLAC（总长 4:09，最大帧 20,473 字节）：
实际 I2S 输出约 48,000 帧/秒、写入错误为 0，用户确认连续播放且声音正常。
串口 `W` 打开并启动 Wi-Fi 传输，`D` 报告客户端数、接收字节与已保存数量。

**仅充电：SETTINGS → POWER → CHARGE ONLY。** 页面显示当前电量与充电状态，随后关闭背光与显示输出，让主控和无线硬件进入深度休眠；按 **BOOT** 唤醒。电池充电仍由板载充电芯片独立控制，没有提高充电电流或修改电池参数。休眠时 USB 串口会断开，烧录前先按 BOOT 唤醒。

USB 插入后会自动给主板供电；插线时 PWR 的关机请求无法切断 VBUS 供电。这是板卡电源路径行为，见 [板厂 FAQ](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.85B/FAQ)。仅充电模式用于减少运行耗电，实际充电速度仍受充电芯片、电池阶段和 USB 供电限制。

<img src="docs/images/power.png" width="240" alt="POWER 仅充电页面，实际 C++ 渲染，84% 为演示电量">
<img src="docs/images/power-no-current.png" width="240" alt="POWER 插线但零充电电流的页面，实际 C++ 渲染，10% 为演示电量">

左图为检测到电脑且电量计报告正向电流；右图为检测到电脑、零电流且充电状态未确认。两种状态均不会再显示 ON BATTERY。

**操作方式。** 沿应用圆环拖动，松手后吸附到最近入口；点图标或圆环中心即可打开。
倾斜操作仍可使用，触摸期间暂停响应倾斜。主页不显示时钟。
在游戏里长按 BOOT 返回 GAMES 文件夹，在文件夹里长按 BOOT 或点 BACK 返回主页；其他应用直接返回主页。
语音宠物中长按 BOOT 返回启动器固件。

**PHOTOS 相册。** 先列出 **24 个不同的原始 Clawd GIF**，再追加 SD 卡 `/Photos` 中的图片。
左滑下一张、右滑上一张，也可点击左右两侧翻页，底部显示当前页码和总数。
长按 650ms 保留原功能：切换居中裁切全屏和完整图片适配；长按后松手不会翻页，默认填满圆屏。
内置动图的全屏视图对可见主体居中并保持一致的身体大小，完整显示模式保留原始画布。
透明像素使用深色背景，正确合成前帧清除规则和逐帧调色板。GIF 缩放坐标会缓存，
播放期限使用真实时间，画面未变时不重复传输 LCD。
内置 GIF 无需 SD 卡，直接从闪存读取，不将整个相册放入内存，也不改动已有 SD 文件。
SD 图片支持基线 JPEG、BMP 和动画 GIF。
`tools/make_clawd_album.py` 去重后原样嵌入源文件字节，占用闪存 1,947,672 字节；`--check` 检查生成头文件是否仍匹配源文件。
串口 `P` 打开相册，`N` 下一张，`J` 上一张；`D` 报告真实动画帧率及解码/合成/缩放耗时，`F` 读取当前帧缓冲。
`python tools/verify_album_frames.py` 将全部 1,262 个动画源帧与独立 Pillow 解码结果逐像素比较。
该检查需要 g++、Pillow、NumPy 及本地 AnimatedGIF 源码，可用 `--gif-library /path/to/AnimatedGIF/src` 指定位置。
24 个相册条目也均已从实机采集并匹配源帧，背景、动画和人物尺寸修正已获实机确认。
三组实机动画抽样为 14.88–14.98fps，源帧间隔交替为 60/70ms；旧合成与调度实现为 9.53–10.07fps。
因此保留了原始动画，没有改成静态图。

**MUSIC 音乐。** 读取 SD 卡 `/Music` 中的 MP3、WAV、AAC/M4A、FLAC 和 Ogg。
底部三个按钮是上一首、暂停/继续和下一首；点击进度条跳转，顶部 `−` / `+` 调节音量，播放结束后循环切歌。
目录没有支持的音乐时，会生成原创的 8 秒 PCM WAV 演示曲 `/Music/Badge-Loop.wav`，不会替换用户已有歌曲。
长按 BOOT 约 900ms 返回主页。进入和退出时无需重启，通过释放及恢复 I²S 驱动切换音频使用权；
本地解码器分支使用 ES8311 的 256fs MCLK，并在删除 IDF 5 通道前先禁用通道。
串口 `M` 打开播放器，`V` 暂停/继续，`N` / `J` 切歌。
解码和 I²S 输出运行在核心 0 的独立任务中，避免 LCD 传输和触摸轮询饿死音频。
播放器与 SD 操作串行执行；退出时先等待音频任务结束，再删除解码器、卸载存储卡和恢复启动器音效。

PCM 回调明确将采样送入 I²S。串口 `D` 区分回调尝试次数和 I²S 实际接收的完整双声道帧，
并报告写入错误、工作任务耗时和剩余栈。`U40` 临时将共享编解码器音量设为 40%，不保存。
实机播放 44.1kHz / 128kbps 双声道 MP3 并同时运行音乐动画时，约输出 44,100 双声道帧/秒；
原先每轮界面循环只解码一次时约为 28,000 帧/秒。

**HYPNO 惯性怀表。** 左右晃动吧唧驱动金色怀表，复用输入循环已读取的 QMI8658 加速度，
以惯性摆和阻尼模拟运动，自由摆动周期约 3.4 秒。缓慢跟踪的重力基线适应不同握持姿态，
设备静止时不会自动周期摆动；同心圆随钟摆运动，摆停后也停止。
点击画面或中央暂停/播放按钮可冻结或继续；`−` / `+` 选择 SOFT、NORMAL、STRONG 三档灵敏度。
暂停期间仍采样运动，但不会积累恢复时的冲量；拖动会取消点击。
绘制上限为 30fps，暂停或静止时不反复传输屏幕。长按 BOOT 约 900ms 回主页。
每次进入从静止和 NORMAL 灵敏度开始，不改动宠物存档及电池数据。
串口 `Y` 打开 HYPNO，`D` 报告真实原始加速度、运动驱动量、角度和角速度。
软件预览向相同的运动及绘图代码输入模拟加速度，不是实物晃动录像。

```bash
./tools/deploy_game.sh system          # 更新 factory 启动器，保留 ota_0
./tools/deploy_game.sh games/clawd-roll   # 或部署独立游戏工程
python tools/make_font.py              # 重新生成位图字体
python tools/make_mon_sprites.py       # 重新生成共享宠物的七种形态
python tools/make_icon_sprites.py      # 重新生成三个启动器图标
python tools/make_icon.py              # 重新生成项目图标

python tools/preview_pet.py            # 烧录前检查宠物布局
python tools/preview_icons.py          # 检查圆环图标底盘上的应用图标
python tools/preview_launcher.py       # 检查圆环布局
python tools/check_preview_sync.py     # 运行真实 C++ 回归检查
```

Arduino 启动器直接绘制到 PSRAM 中的 360×360 RGB565 帧缓冲，并通过 `Game::dirtyY0/dirtyY1` 只传输变化行。
ESP-IDF 语音宠物通过 LVGL 9 显示适配层使用相同的 C++ 绘图代码，在 PSRAM 中保留绘制缓冲和稳定图像缓冲，
只复制及标记变化行。触摸采样独立于绘图运行，屏幕空闲时也保持扫描。

**倾斜输入。** 本板采用实测固定轴映射：Z 轴垂直屏幕，屏幕 X 为传感器 X 取反，屏幕 Y 为传感器 Y。
启动时记录平均值仅供诊断，不据此选择映射，也不按启动时角度归零。
重力倾斜检测中，水平本身就是零点；扣除启动姿态会把当时的握持角度固定成偏差，导致小球一直顶着墙。
实际使用缓慢自动微调，只在设备接近水平时消除加速度计静态偏置，不会抵消玩家主动倾斜。

<a id="games"></a>

## 小游戏与宠物

### clawd-roll

<img src="games/clawd-roll/icon.png" width="180" alt="clawd-roll 游戏图标">

倾斜设备让 Clawd 滚动，放平后依惯性滑行，撞到场地边缘会反弹并在碰撞点扩散圆环。
没有分数或通关目标，主要用于体验倾斜与碰撞的手感。

也可编译成独立单游戏固件，便于单独调整物理效果：

```bash
./tools/deploy_game.sh games/clawd-roll
python tools/convert_sprite.py games/clawd-roll/clawd-source.png \
       games/clawd-roll/clawd_asset.h --name clawd --body E83B3B
```

### clawd-tap

点击版小游戏：Clawd 在场地中随机出现，停留时间逐渐缩短，点击得分，错过三次结束。
它同时用于验证游戏接口；与 clawd-roll 一起覆盖触摸和倾斜两种输入，以及不同的绘制负载。

### SNAKE / BRICKS

主页点 **GAMES** 文件夹，里面仍是相同的环绕菜单，收纳 **CLAWD ROLL、CLAWD TAP、SNAKE、BRICKS**。
点 **BACK** 或长按 BOOT 约 0.9 秒回主页；游戏里长按 BOOT 约 0.9 秒返回文件夹，保留选中位置。
一次长按只返回一层，主页长按约 2.2 秒进入语音系统的功能保留。

![GAMES 文件夹圆环，实际 C++ 软件渲染](docs/images/games-folder.png)

两个新游戏完全离线，点击 **PLAY** 开始，底部暂停键暂停或继续，
结束后点 **RETRY** 重开。最高分保留到本次开机结束。

| SNAKE 贪吃蛇 | BRICKS 打砖块 |
|---|---|
| ![SNAKE 实际 C++ 软件渲染，示例对局](docs/images/snake.png) | ![BRICKS 实际 C++ 软件渲染，示例对局](docs/images/bricks.png) |

**SNAKE**：15×15 格子均位于圆屏可视区域。横着走时，点击蛇头上方或下方转为向上或向下；
竖着走时，点击蛇头左侧或右侧转为向左或向右。以蛇头的位置判断，不是按屏幕中心划分。
吃橙色食物得分并逐渐加速；
撞墙或撞到身体结束，填满棋盘获胜。禁止直接反向，快速连续转向会按移动步数依次执行，
每次点击只登记一次方向，蛇头眼睛立即提示下一个转向。底部整条区域按下即暂停或继续，
不要求对准文字，也不会因为松手前手指偏移而取消；短时间触点抖动不会反复切换。静止画面不重复传输。

**BRICKS**：触摸左半屏向左移动，触摸右半屏向右移动；点击立即移动一小步，按住超过约 180ms 连续移动。
无需瞄准小箭头，手指在同一半区内偏移不会取消，跨过中线可切换方向；对应场地边框高亮反馈。
底部中央的大触控区按下即暂停或继续；掉球后先用左右半屏调整挡板，再点底部 **PLAY** 发球。
反弹角度取决于球击中挡板的位置。共 20 块砖、三条命，每块 10 分，打完全部砖块获胜。
物理更新拆成不超过 8ms 的子步，避免快速小球穿过砖块，通常按约 30 帧/秒绘制。
主循环在同一毫秒内再次运行时按零时间推进，跳过屏幕刷新不会让游戏加速。

`python tools/preview_minigames.py` 编译并运行真实游戏逻辑，覆盖转向队列、吃食物、尾部与身体碰撞、
填满棋盘、反弹、掉命、完整打砖通关、蛇头相对点击、左右半屏点击及按住、整条底部暂停及防抖、绘制节奏及帧缓冲边界，
并生成上面的软件截图、六入口主页和游戏文件夹图。串口 `GAMES` 打开文件夹，`GSNAKE` / `GBRICKS` 打开对应游戏，`D` 查看状态。
`folder_nav` 检查实际菜单注册表、触控打开、逐层返回、选中位置恢复和 BOOT 长按锁存；
`launcher_drag` 同时检查六入口主页及五入口文件夹的拖动、点击和角度回绕。
`frame_timing` 回归检查覆盖短循环、长停顿及毫秒计数回绕，防止计时误差随空闲循环累积。

### CLAWD PET

原来的 clawd-pet 和 pixel-mon 已合并为一个应用，整合触摸、拖动、戳动、摇晃清洁、蛋、两条进化路线及七种形态。
Arduino 启动器和 ESP-IDF 语音宠物共用养成与绘图代码。不存在当前存档时，优先迁移旧 PIXEL MON 存档，
其次迁移早期宠物存档；旧存档保留，新进度使用 `clawdpet2/blob`。

点击 **SCRUB** 后轻轻来回摇动设备，清除 5 处污点；完成增加 55 点清洁度。
摇晃识别使用三轴变化，支持横向、纵向摇动；点击屏幕可结束，20 秒后自动退出并保留已完成部分的清洁度。
语音桌宠沿用 [Waveshare 官方 QMI8658 驱动](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85B/blob/main/Examples/ESP-IDF-V5.5.3/06_qmi8658_test/components/waveshare__qmi8658/qmi8658.c) 的 6DOF / 8g 配置，主机仍每 50ms 采样一次，避免原先仅加速度配置下持续读到零的情况。

可选的集成语音版基于小智开源客户端及本板已有硬件支持。屏幕上的宠物可以聆听、用表情和字幕回应，
并通过只读 MCP 工具提供当前需求。短按 BOOT 或点击宠物页顶部开始/结束对话，长按 BOOT 返回主页。
对话空闲时养成按钮仍可用，空闲页头不再显示“待命”。详见[语音宠物编译与操作](docs/clawd-pet-voice.md)。

**孵蛋。** 初始界面是程序绘制的蛋。能量和清洁度都高于 40 时才算温暖并推进孵化，
持续温暖约三分钟可孵化，每戳一次额外增加四点进度；点击时蛋会晃动并产生回应。
孵化前菜单只开放 PET 和 STYLE，重命名仍放在 STYLE 内。

**进化路线。** 两条路线根据持续养成状态在后台累积，而不是靠某个时刻点中按钮：

| 路线 | 累积来源 |
|---|---|
| joy（快乐） | 幸福值，以及玩耍、抚摸和戳动 |
| vigor（活力） | 能量与清洁度，以及睡觉和完成清洁 |

进化同时需要年龄和养成点：四项需求平均值高于 70 时，每分钟增加一点养成点。
门槛按短时使用设计，拿起吧唧玩十分钟也能看到成长，不必连续照顾数小时。

| 阶段 | 条件 | 结果 |
|---|---|---|
| EGG → BABY | 孵化达到 100% | **PIP** |
| BABY → CHILD | 5 分钟、养成点 6 | 按领先路线变为 **LEAF**（快乐）或 **AQUA**（活力） |
| CHILD → ADULT | 20 分钟、养成点 25 | 沿血统变为 **BLOOM** / **TIDE**；路线点数相同为 **SOLAR**；该阶段平均养成低于 45 为 **DUSK** |

进化提示会写明分支与原因，例如 `PIP is now BLOOM (joy)`；串口同时打印快乐点、活力点和平均养成值，便于核对结果。

**只有睡觉能恢复能量**，每分钟恢复九点，补满后自动醒来。休息会推动活力路线；
睡眠期间 FEED、PLAY、PET、SCRUB 禁用，REST 和 STYLE 仍可操作。

**图鉴与重新孵化。** 底部 `GROW 3/6  SEEN 2/7` 的前半项提示当前阻碍进化的条件；
养成点足够、只差年龄时，切换为 `AGE 4m`。`SEEN` 通过位掩码保存已见形态，换新蛋后仍保留。
宠物成年后，衣柜的 RENAME 次要操作变为 NEW EGG，可重新孵化以收集其他五种形态。

各阶段沿用 Clawd 轮廓与固定阶段尺寸，搭配形态颜色和一像素描边。
ORIGINAL 会添加对应头部标记：叶子、水滴、花、冠饰、光环或两团暗色小云。
`tools/make_mon_sprites.py` 重新生成身体位图；标记由代码绘制，避免每种形态额外增加约 1.5KB 位图。

**衣柜。** STYLE 提供 ORIGINAL 原始样式及 20 种像素配饰：花朵、安全帽、耳机、思考气泡、泡泡、睡帽、
眼镜、书、配送包、蝴蝶结、皇冠、贝雷帽、光环、星星、巫师帽、爱心、鸭舌帽、耳罩、彩虹、天线。
滑动预览或点击箭头切换，点 WEAR 保存，点 BACK 取消。配饰始终使用原有身体尺寸、眼睛和语音表情，不缩放或替换宠物。
GIF 参考保留在 `assets/clawd-gifs/`，轻量配饰由 `system/outfits.cpp` 直接绘制。
选择结果使用独立的 `clawdpet2/outfit` 键保存，不改变原有养成存档布局。

<a id="firmware-slots"></a>

## 固件分区

当前安装包含两个应用镜像。Arduino 启动器在 `factory` 中提供游戏、相册和设置；
集成 CLAWD PET 在 `ota_0` 中同时提供养成与小智语音。安装时先备份，
再**替换该分区原有的小智 2.2.4 应用**，共享字体和语音模型保留。

```
factory  app   0x020000  6.57MiB   Arduino 启动器、游戏、相册与设置
ota_0    app   0x6c0000  4.00MiB   CLAWD PET + 小智 v2.3.0 语音客户端
assets   data  0xac0000  4.00MiB   保留原有字体和语音模型
```

切换应用镜像时修改 8KiB `otadata` 启动选择区并重启；宠物养成和对话随后都在同一个宠物应用中完成。

| 操作 | 行为 |
|---|---|
| 启动器 → 打开 **CLAWD PET** | 检测已安装的语音宠物，选择 `ota_0` 并重启进入 |
| 语音宠物 → **长按 BOOT** | 保存养成进度，选择 `factory` 并重启回启动器 |

双向切换均由固件自己完成，无需电脑、重新烧录或断电。
擦除 `otadata` 会回退到 `factory`；选中语音宠物后会持续启动它，直到重新选择启动器。
未安装语音镜像时，CLAWD PET 入口运行本地养成版。

```bash
python tools/switch_slot.py home     # 选择 factory 启动器
python tools/switch_slot.py stock    # 旧命令名，选择 ota_0 中已安装的语音宠物
python tools/switch_slot.py status   # 读取启动选择区并重新启动应用
```

更新启动器使用 `deploy_game.sh system`，更新语音宠物使用 `install_voice_pet.py --flash`。
两者均保留引导程序、分区表和共享资源。

`install_stack.py` 是**旧版恢复工具**：安装启动器并从 `backup/` 恢复原始助手，覆盖自定义语音宠物，
不能用来更新语音宠物。`restore_stock.sh` 恢复整片 16MiB 镜像，包括原始主应用、助手、引导程序、分区表和保存的配置。

<a id="why-the-games-are-not-entries-in-the-stock-systems-launcher"></a>

### 为什么不直接加进原厂启动器

原厂启动器本应是自然的入口，但 Waveshare 公开发布的材料不足以直接修改它：

- 原厂系统只提供**预编译二进制**。公开仓库没有其完整源码；`05_esp-brookesia` 使用同一框架和样式，
  但只安装 `SquarelineDemo`。`brookesia_core` 也仅包含框架，没有设置、天气、音乐、视频或游戏应用。
- `assets` 分区是打包资源表，不是应用文件系统。实机读回内容是字体、`srmodels.bin` 语音模型和 `index.json`，
  该版本没有运行时应用加载或应用商店。

上游 ESP-Brookesia 有运行时应用机制（`runtime/brookesia_runtime_js`、`_lua`、`_wasm`、`_elf` 及
`app/brookesia_app_store`），因此从源码自行构建的系统可以提供游戏入口，但闭源的原厂应用仍需另行实现。
本项目采用自己的启动器，并把开源小智客户端集成到 CLAWD PET。

`system/partitions.csv` 和独立游戏的分区表均对应现有布局。
语音安装工具会把生成分区表与完整备份、连接的设备交叉核对。
仅更新应用的启动器部署流程假设现有布局已经正确安装；不兼容的分区表会让运行时查找访问错误区域。

修改 `otadata` 时需注意三个格式细节：校验和**只覆盖 4 字节 `ota_seq`**，并非整个结构体；
ESP-IDF 的 `esp_rom_crc32_le` 会对输入和输出都取反，与 zlib 的 crc32 不同；
`ota_state` 必须为 `VALID`，因为引导程序会把 `PENDING_VERIFY` 或 `NEW` 视为未确认并回滚。

<a id="back-up-before-you-flash"></a>

## 烧录前备份

修改原厂设备前，先保存整片闪存，以便逐字节恢复。保留最初的原厂备份，不要用后续自定义安装的备份覆盖。
备份包含设备配置，已加入 Git 忽略规则。

```bash
mkdir -p backup
esptool --chip esp32s3 --port COM3 --baud 921600 --after watchdog_reset \
    read_flash 0 0x1000000 backup/1.85B-stock-16MB.bin
```

需要时可使用以下命令恢复：

```bash
./tools/restore_stock.sh          # 优先使用 backup/，否则使用 Waveshare 发布镜像
```

<a id="gotchas-worth-knowing"></a>

## 常见问题与注意事项

**触摸控制器空闲时不响应 I²C。** `Touch_Init()` 会让 CST816S 保持自动休眠；
休眠后控制器不响应总线，因此每帧轮询会持续产生 `i2c.master: I2C transaction unexpected nack detected`，直到有人触摸屏幕。
上游用于控制该行为的函数没有正确写入计算结果：

```c
void CST816_AutoSleep(bool Sleep_State) {
  ...
  uint8_t Sleep_State_Set = (uint8_t)(!Sleep_State);
  Sleep_State_Set = 10;                              // 覆盖了前面计算的值
  I2C_Write_Touch(CST816_ADDR, CST816_REG_DisAutoSleep, &Sleep_State_Set, 1);
}
```

关闭自动休眠需要写入 `0x01`，但上述函数总是写入 `10`。
本地驱动的 `Touch_SetAutoSleep(false)` 写入正确值，并避免上游函数附带的控制器重置；`input::init()` 会调用它。

**原生 USB 可能使芯片滞留下载模式。** USB-C 直接连接 USB-Serial/JTAG；
本板原生 HWCDC 接收命令时需要拉起 DTR，单独操作 DTR 不会复位。
DTR 有效时切换 RTS 可能复位进入 ROM 下载器。串口监视应保持 RTS 无效，不要只为检查应用是否运行而执行会复位的 esptool 探测。

可按以下顺序尝试恢复：

1. **通过 esptool 执行看门狗复位**，通常无需接触设备即可解除滞留并启动应用：

   ```bash
   esptool --chip esp32s3 --port COM3 --baud 921600 \
       --after watchdog_reset --connect-attempts 1 read_mac
   ```

   看门狗复位路径与 RTS/EN 复位不同。应通过 `serial_monitor.py` 及启动日志确认应用已运行；
   单次 ROM 探测失败不能区分应用正在运行、设备断开或端口不可用。

2. **完整断电重启**：拔下 USB；若连接电池，长按 PWR 约 2 秒关机，再短按开机。
   完整冷启动会在 GPIO0 被拉高时重新采样启动配置引脚。

串口监视使用 `tools/serial_monitor.py`。它为原生 CDC 启用 DTR、始终不脉冲 RTS，连接和断开不会干扰正在运行的应用：

```bash
python tools/serial_monitor.py COM3 115200
python tools/serial_monitor.py COM3 --seconds 20     # 限时采集日志
```

**恢复原始备份时，不要让 esptool 改写镜像头。** 将原始闪存备份写入 `0x0` 时，
指定 `--flash_mode` / `--flash_size` / `--flash_freq` 会让 esptool 修改闪存参数字节（偏移 2，SPI 模式）。
原厂引导程序头标记为 **DIO**，运行时才升级到 QIO；强行指定 `--flash_mode qio` 会让引导程序读不到闪存，
设备反复看门狗重启（`rst:0x7 TG0WDT_SYS_RST`），控制台只剩 ROM 启动信息。

恢复时始终使用 `keep`：

```bash
esptool --chip esp32s3 --port COM3 --baud 921600 --after watchdog_reset write_flash \
    --flash_mode keep --flash_size keep --flash_freq keep --verify \
    0x0 backup/1.85B-stock-16MB.bin
```

此时 esptool 的 “Hash of data verified” 不能证明原始镜像未被修改，因为它校验的是改写后的数据。
`tools/restore_stock.sh` 已按保留原参数的方式处理。

**两套 LVGL 版本。** Arduino 示例附带 **LVGL 8.4.0**，ESP-IDF 示例和原厂系统使用 **LVGL 9.5.0**。
两者 API 不兼容，例如 `lv_scr_act()` 与 `lv_screen_active()`，显示和输入设备接口也已调整。
复用示例前先确认它使用的版本。

**通用示例上传可能替换现有布局。** 原厂系统是已编译的应用，不是可以任意安装 Arduino 草图的操作系统。
普通示例上传也可能重写引导程序和分区表。本项目仅更新应用的工具沿用已有分区；
完整原厂恢复则会替换两个自定义应用及其保存的配置。

**从 Git Bash 调用 ESP-IDF 脚本。** 有两个常见问题：

1. Git Bash 中直接执行 `cmd.exe /c script.bat` 会被 MSYS 把 `/c` 改写为路径，应使用双斜线：`cmd.exe //c script.bat`。
2. Git Bash 为子进程设置的 `MSYSTEM` 环境变量会使 `idf_tools.py` 和 `install.bat` 报错 “MSys/Mingw is not supported”。
   `tools/idf_install_steps.bat` 和 `tools/idf_env.bat` 会先清除该变量。

**只有 3 个空闲 GPIO**（7、8、18），其余引脚已被板载外设或闪存/PSRAM 总线占用。
需要更多 I/O 时可在引出的 I²C 接口上接扩展芯片，详见[硬件文档](docs/hardware.md)。

**逻辑电平为 3.3V。** 连接传统 5V Arduino 板前需要电平转换。

<a id="current-status-and-next-work"></a>

## 当前进度与后续工作

- [x] 六入口主页与 GAMES 文件夹圆环，支持拖转、吸附、倾斜和逐层返回
- [x] HYPNO 传感器惯性怀表，三档灵敏度、阻尼与暂停/继续
- [x] CLAWD ROLL 与 CLAWD TAP 小游戏
- [x] SNAKE 与 BRICKS 小游戏，支持触摸、暂停、重开和本次开机最高分
- [x] 统一 CLAWD PET 养成、孵蛋、七种进化形态及旧存档迁移
- [x] 集成小智语音、表情、中文字幕和共享宠物状态
- [x] 20 种不改变身体比例的配饰，选择可持久保存
- [x] SD 卡照片/GIF，全屏裁切与完整图片适配
- [x] 24 个内置 Clawd GIF，离线可用并与 SD 卡照片共存
- [x] 相册左右翻页、页码及原有长按切换显示模式
- [x] SD 本地音乐播放、暂停、切歌、进度跳转与音量控制
- [x] 独立触摸扫描、触摸唤醒及可见按钮反馈
- [ ] 更长时间的续航及语音/养成同时运行测试
- [ ] 更广泛的媒体文件和 SD 卡兼容性检查

## 协作者

- **[yahoofunny](https://github.com/yahoofunny)**：项目创意、需求与交互设计、实机测试和反馈。
- **Codex（OpenAI）**：AI 编程协作者，参与固件开发、问题排查、性能优化、回归验证与文档整理。

<a id="license"></a>

## 许可证

采用 **AGPL-3.0**。详见 [LICENSE](LICENSE)，仓库所含第三方材料及其条款见 [NOTICE.md](NOTICE.md)。

Clawd 美术素材来自 AGPL-3.0 项目，因此本项目按相应许可发布源码。
若改用原创素材，需要重新评估该素材带来的许可义务。板卡驱动采用兼容的 Apache-2.0；
宠物设计参考了 MIT 项目，但没有复制其代码。

<a id="links"></a>

## 相关链接

- [Waveshare 板卡文档](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B)
- [Waveshare 源码仓库](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85B)
- [ESP-Brookesia](https://github.com/espressif/esp-brookesia)：原厂系统使用的人机界面框架
- [LVGL](https://lvgl.io)

**与串口交互的操作最终必须让应用启动。** 本板如果停留在 ROM 下载器，会表现为黑屏、
电源键无响应，但 USB 仍能枚举；只断开电池也可能因 USB 持续供电而无法退出。
后续每次 esptool 连接又会复位并看见正常的 ROM 启动，所以工具可能一直报告成功，而屏幕仍然是黑的。
最终步骤始终使用 `--after watchdog_reset`，不要以 `--after no_reset` 结束。
