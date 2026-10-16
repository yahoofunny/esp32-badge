# CLAWD PET：养成与小智语音

CLAWD PET 与 PIXEL MON 合并成一个应用。保留 clawd 的形象、眼神、抚摸、拖动和摇晃清洁，
使用 PIXEL MON 的蛋、四种需求、健康、成长、joy/vigor 分支和七形态图鉴。
语音聊天直接发生在这只宠物身上，显示聆听／等待／说话状态、表情和中文字幕。

## 结构

- `system/game_pet.{h,cpp}`：两个固件共享的唯一养成与绘图实现。
- `firmware/clawd_pet/`：ESP-IDF 显示适配、触摸／IMU／RTC 和小智语音桥接。
- Arduino 启动器保留在 `factory`（`0x20000`），继续运行游戏、相册、设置。
- 新的语音桌宠运行在 `ota_0`（`0x6c0000`，4MB），替代原来小智的界面。
- 点击唯一的 CLAWD PET 入口时，如果检测到 `project=clawd-pet` 就重启进入它；
  进入后养成与聊天在同一固件中运行。长按 BOOT 返回启动器。
- 若尚未安装语音版本，入口运行本地养成。原厂固件保存在 `backup/`。

这一步保留两份固件，以免为了接语音而一次迁移所有游戏和相册。
没有把原厂界面嵌进桌宠，也没有通过另一份固件后台处理音频。

## 构建

固定使用 XiaoZhi v2.3.0，提交 `1b48ebd7863695bf80d384c6c09af6299a6d7d0e`，兼容本机 IDF 5.5.3。
`firmware/clawd_pet/dependencies.lock` 固定本次验证的依赖版本。
该版本已有 1.85B 的驱动与 CMake 分支，但漏了 Kconfig 板型选项，准备脚本会补齐。

先按 README 备份连接设备的完整 Flash，保存为 `backup/1.85B-stock-16MB.bin`。
安装工具（包括默认的只读检查）需要这份本机备份来核对分区表；备份不随仓库发布。
在仓库根目录的 PowerShell 中：

```powershell
python tools/prepare_clawd_voice.py
cmd /c tools\idf_env.bat idf.py -C reference/xiaozhi -B build/clawd-voice set-target esp32s3
cmd /c tools\idf_env.bat idf.py -C reference/xiaozhi -B build/clawd-voice build
python tools/install_voice_pet.py          # 检查产物，不写设备
python tools/install_voice_pet.py --flash  # 备份后仅写 ota_0
```

`reference/xiaozhi` 是忽略的上游源码；准备脚本从固定提交重建 overlay，不应在其中手工保存修改。
固件自动 OTA 和共享素材下载已关闭，避免覆盖定制桌宠、启动器或现有模型。
配网、激活、音频、协议仍使用小智客户端。已有 NVS 中的网络与账号配置保留；
能否直接沿用激活状态要由联网验证决定。

## 界面与资源

- BOOT 单击或点击宠物页标题：开始／结束对话；长按 BOOT：回主页。
- 聆听／说话时使用同一只进化后的 clawd，语音表情不修改需求或进化状态。
- `self.pet.get_state` 是只读 MCP 工具，供模型读取名字、形态、需求、睡眠与生病状态。
- 一个绘制缓冲和一个稳定图像缓冲均在 PSRAM，合计约 506KiB；仅复制并提交变化的行。
- 触摸固定每 20ms 扫描，按下即唤醒；绘制独立节流，互动时约 25fps、待机约 12fps、语音时 10fps、省电时 2fps。
  省电仅降低绘制频率，不降低触摸扫描。音频由原生小智服务统一管理。
- 互动键按下立即高亮，松手执行一次并保留约 150ms 反馈。触摸唤醒只重置屏幕闲置计时，不反复配置 Wi-Fi。
- 触摸边沿和养成动作日志放在 DEBUG，避免连续点击时串口输出阻塞；`pet-perf` 每 10 秒汇总输入、更新、绘制和缓冲复制耗时。
- 没有另起 Arduino I2S 合成器；养成提示音通过小智的音频服务播放。
- 触摸、六轴与 RTC 的 I²C 等待为 50ms。此工程 tick 为 10ms，不能设置小于一个 tick 的正超时。
- 顶部不显示“待命”；聆听、连接和说话仍显示各自的状态。

## 换装

点养成页的 `STYLE` 打开衣柜，点击左右箭头或在预览区横向滑动浏览。
`WEAR` 穿上并保存，`BACK` 取消本次预览；底部保留 `RENAME`，成体时变为 `NEW EGG`。
第一项 `ORIGINAL` 恢复原来的进化标记，其余 20 项是花朵、帽子、耳机、发卡、气泡等简单像素配饰。
每项都叠加在原模型上，不替换或缩放身体；预览、养成页和语音页使用同一个身体绘制函数。
换装不改变需求、成长或图鉴，原来的眼神和说话表情也继续保留。

`assets/clawd-gifs/` 保存去重后的 24 个原素材作为设计参考。
固件中的 `system/outfits.cpp` 直接绘制轻量配饰，无 GIF 解码和整帧内存分配。
配饰锚定当前进化形态的身体边界，不会因参考 GIF 的透明留白和体型差异而忽大忽小。

## 存档

统一使用 `clawdpet2/blob`。首次进入优先迁移 `pixmon/blob`，否则迁移原来的 `clawdpet/blob`。
保留名字、需求、成长和图鉴；旧 namespace 不删除。迁移后的状态需要通过完整性检查。
换装单独存入 `clawdpet2/outfit` 的一个字节，不改变原养成存档的布局。

## 本机验证

`tests/pet_state_test.cpp` 执行真实养成代码，覆盖孵化、两种旧存档、迁移优先级、异常值和语音显示。
`tests/photo_blit_test.cpp` 覆盖 JPEG/GIF 分块缩放、边缘、铺满裁剪。
`tests/launcher_drag_test.cpp` 执行真实启动器逻辑，覆盖旋转拖动、吸附、点击与倾斜冲突。
`tests/pet_state_test.cpp` 还覆盖衣柜触摸、循环、取消、保存恢复、旧存档与语音期间的隐藏控件。
`tests/outfits_test.cpp` 在两种 RGB565 字节序下验证配饰、身体区域保护和屏幕边界。
`tests/pet_ui_pacing_test.cpp` 验证省电期间的短按、输入与绘制独立节流、按下／松手立即刷新及时间溢出。
`tests/gfx_circle_test.cpp` 验证按钮圆环的平方距离优化与原画面逐像素一致；按键测试还覆盖即时反馈和不重复执行。

主屏沿应用环拖动切换，松手吸附，点击打开；没有左上角时钟。
相册默认居中铺满、隐藏文件名；长按约 650ms 切换完整显示／铺满。
