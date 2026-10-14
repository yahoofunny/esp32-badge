# ESP32-S3-Touch-LCD-1.85B 硬件笔记

Waveshare 1.85 寸圆屏板子的引脚占用、可扩展性和踩坑记录。所有数据来自实测设备、
官方仓库的 BSP 头文件（`bsp/esp32_s3_touch_lcd_1_85B.h`）以及原理图。

## 实测设备信息

```
Chip     : ESP32-S3 (QFN56) revision v0.2
Features : WiFi, BLE, Embedded PSRAM 8MB (AP_3v3)
Crystal  : 40MHz
USB mode : USB-Serial/JTAG
MAC      : 44:bd:8d:78:0d:ac
```

USB VID:PID = `303A:1001` —— 303A 是 Espressif 的厂商 ID，1001 是芯片内置的
USB JTAG/serial debug unit。板子上**没有** USB 转串口芯片，Type-C 直接接在芯片的
GPIO19/20 上。

枚举出两个接口：`MI_00` = CDC 串口，`MI_02` = JTAG 调试单元（WinUSB）。

## 完整引脚分配

```
LCD      CS=21   PCLK=40   D0=46  D1=45  D2=42  D3=41   RST=3   BL=5
触摸     SCL=10  SDA=11    RST=1  INT=4
音频     SCLK=48 MCLK=2    LCLK=38 DOUT=47 DSIN=39      PA_EN=9
SD       CLK=15  CMD=14    D0=16  D1=17  D2=12  D3=13
按键     BOOT=0 (strapping, 也是唯一的板载按键)
USB      D-=19   D+=20
RTC_INT  6
```

## 扩展性分析

### 瓶颈：空闲 GPIO 只有 3 个

ESP32-S3 共 45 个可用 GPIO，这块板子占掉了 42 个：

| 占用者 | 引脚 |
|---|---|
| 屏幕（QSPI） | 3, 5, 21, 40, 41, 42, 45, 46 |
| 音频（I2S） | 2, 9, 38, 39, 47, 48 |
| SD 卡（SDMMC 4-bit） | 12, 13, 14, 15, 16, 17 |
| I²C 总线 | 10, 11 |
| 触摸 | 1, 4 |
| 按键 / USB / RTC_INT | 0 / 19, 20 / 6 |
| flash + PSRAM（SPI0/1） | 26–32 |
| 八线 PSRAM | 33–37 |

**剩下能自由使用的：GPIO 7、8、18**

| 引脚 | 能力 | 备注 |
|---|---|---|
| GPIO7 | 数字 IO、PWM、**ADC1_CH6** | ADC1 独立于 WiFi，模拟读数可靠 |
| GPIO8 | 数字 IO、PWM、**ADC1_CH7** | 同上 |
| GPIO18 | 数字 IO、PWM、ADC2_CH8 | 当数字口用没问题；**ADC2 与 WiFi 共用硬件**，开 WiFi 时模拟读数不可靠 |

三者都能跑 LEDC（PWM）和 RMT（驱动 WS2812 灯带、红外收发）。

### 没有电容触摸外设

ESP32-S3 **取消了**原版 ESP32 的电容触摸功能，所以无法在引脚上直接贴触摸按键。

### 引出的扩展接口

**I²C 排针 —— 最有价值的一条**，但它和 6 个板载器件共用总线。接新模块前必须避开这些地址：

| 地址 | 器件 |
|---|---|
| `0x15` | CST816S 触摸 |
| `0x30` | ES8311 音频 codec |
| `0x51` | PCF85063 RTC |
| `0x55` | BQ27220 电量计 |
| `0x6B` | QMI8658 六轴 IMU |
| `0x80` | ES7210 回声消除 |

常见的可挂载设备：SSD1306 OLED（`0x3C`）、BME280（`0x76`/`0x77`）、
TCA9554 / PCF8574 IO 扩展（`0x20`）、各种手势/环境传感器。

**UART 排针**（TXD=43 / RXD=44）：与另一块 MCU 通信、接 GPS 模块。

**USB 焊盘**（GPIO19/20）：已被 Type-C 座占用。

**电源**：5V / 3V3 / GND。

### 需要更多 IO 怎么办

上 I²C IO 扩展芯片。TCA9554 或 PCF8574 一颗补 8 个 IO，多个还能级联，
而板子本来就引出了 I²C 排针 —— 这是这块板子唯一的现实扩展路径。

### 存储与内容

- **microSD（4-bit SDMMC）**：内容的主要管道。图片、GIF 帧、视频、音乐都放这
- **16MB flash**，出厂分区布局：

  | 分区 | 大小 | 用途 |
  |---|---|---|
  | `factory` | 6.6MB (0x20000 + 0x692000) | 出厂程序 |
  | `ota_0` | 4MB | OTA 备份槽 |
  | `assets` | 4MB | **SPIFFS 素材区** —— 出厂系统专门留给素材的 |

- **8MB OPI PSRAM**：帧缓冲、动画缓存
- 出厂固件的依赖里已包含图片解码组件：`esp_new_jpeg`（JPEG）、`libpng`（PNG）、
  `esp_lv_decoder`、`esp_mmap_assets`

### 音频能力

ES8311 codec + 功放使能脚（GPIO9）+ 喇叭焊盘 + 双麦克风 + ES7210 回声消除。
做会说话、能语音交互的应用是现成的。

### 电池

MX1.25 2PIN 座接 3.7V 锂电，板载充电，BQ27220 电量计（I²C `0x55`）可读电量。

## 接外部器件时的坑

**电平**：ESP32-S3 是 **3.3V** 逻辑。经典 Arduino Uno/Nano 是 5V，直连会打坏 GPIO。
要么加双向电平转换模块，要么用 3.3V 的板子（3.3V Pro Mini、ESP32、RP2040）。

**I²C 地址**：接之前先确认模块地址没撞上表里那 6 个。

**QSPI 屏幕不可共享**：屏幕占了 SPI2_HOST，想再加 SPI 设备需要重新规划总线。

## 相关资源

- [官方文档](https://docs.waveshare.net/ESP32-S3-Touch-LCD-1.85B)
- [官方仓库](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85B)
  （示例、出厂固件、原理图）
- [esp-brookesia](https://github.com/espressif/esp-brookesia) —— 出厂系统基于的 HMI 框架
