# DOIO KB16 Codex Micro

将 DOIO KB16 Rev2 （DOIO 16 有线版）作为 Codex Micro 控制器使用的 QMK 固件与 Windows WPF Mapper。

> 非官方项目。请只在 `DOIO KB16-01 Rev2 / APM32F103CBT6` 上使用，并在刷写前确认 Bootloader、固件文件名和 SHA-256。

当前保留两个固件版本：

- `v1.2`：稳定的原生 Codex 控制与四层键位。
- `v1.3.5`：可配置键位、普通三层常亮 HSV 灯控，以及自定义 Codex 对话状态灯颜色。

Mapper 当前版本为 `1.3.5.0`。数字、导航、系统三层共用普通灯光设置；Codex 层继续由主机状态协议独立绘制六个任务灯。

## 下载与使用

请从 [GitHub Releases](https://github.com/hcyniubi/doio-kb16-codex-micro/releases) 下载：

- `doio_kb16_rev2_codex_micro_configurable_v1_3_5_status_colors.bin`
- `Doio.Kb16.Mapper-v1.3.5-preview-complete.zip`

Mapper 是 Windows x64 便携程序。解压完整 ZIP 后运行 `Doio.Kb16.Mapper.exe`；

完整步骤见 [docs/usage.md](docs/usage.md)，v1.3.5 发布说明见 [docs/release-v1.3.5.md](docs/release-v1.3.5.md)。

## 硬件与兼容性

- Keyboard: `doio/kb16/rev2`
- USB VID:PID: `303A:8360`
- Raw HID usage: `FF00:0001`
- Report ID: `6`
- RGB DMA: DMA1 Channel 6
- OLED DMA: DMA1 Channel 5

这些协议标识来自兼容性调查，并非公开稳定接口。ChatGPT Desktop 后续版本可能改变行为。


## 安全说明

固件构建与测试不会自动刷写键盘。刷写前请确认设备型号、Bootloader 和固件 SHA-256。已验证哈希见 [firmware/SHA256SUMS.txt](firmware/SHA256SUMS.txt)。

Mapper 不会刷写固件、不会安装后台服务、不会进入 Bootloader。它只通过键盘 Raw HID Channel 3 读取和写入键位/灯光配置。ChatGPT Desktop 的 Codex 状态灯走 Channel 2，Mapper 不代理也不翻译状态灯消息。

本项目与 OpenAI、DOIO 或 QMK 官方无隶属或背书关系。
