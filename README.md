# DOIO KB16 Codex Micro

将 DOIO KB16 Rev2 作为 Codex Micro 控制器使用的 QMK 固件与 Windows WPF Mapper。

> 非官方项目。请只在 `DOIO KB16-01 Rev2 / APM32F103CBT6` 上使用，并在刷写前确认 Bootloader、固件文件名和 SHA-256。

当前保留两个固件版本：

- `v1.2`：稳定的原生 Codex 控制与四层键位。
- `v1.3.5`：可配置键位、普通三层常亮 HSV 灯控，以及自定义 Codex 对话状态灯颜色。

Mapper 当前版本为 `1.3.5.0`。数字、导航、系统三层共用普通灯光设置；Codex 层继续由主机状态协议独立绘制六个任务灯。

## 下载与使用

请从 [GitHub Releases](https://github.com/hcyniubi/doio-kb16-codex-micro/releases) 下载：

- `doio_kb16_rev2_codex_micro_configurable_v1_3_5_status_colors.bin`
- `Doio.Kb16.Mapper-v1.3.5-preview-complete.zip`

Mapper 是 Windows x64 便携程序。解压完整 ZIP 后运行 `Doio.Kb16.Mapper.exe`；不要只复制 EXE，WPF 自包含发布仍需要随包的五个 native DLL。

完整步骤见 [docs/usage.md](docs/usage.md)，v1.3.5 发布说明见 [docs/release-v1.3.5.md](docs/release-v1.3.5.md)。

## 仓库结构

- `firmware/keymaps/`：可复制到 QMK 的两个保留 keymap。
- `firmware/qmk-core-patches/`：Raw HID 描述符与 WS2812 PWM/DMA 必要补丁。
- `firmware/releases/`：已验证固件二进制。
- `mapper/Doio.Kb16.Mapper/`、`mapper/Doio.Kb16.Mapper.Tests/`：Mapper 源码与测试。
- `docs/`：协议调查、验证记录及交接文档。
- `local-releases/`：本机保留的最近两版便携程序；因单文件超过 100 MB，不进入 Git 历史。

## 硬件与兼容性

- Keyboard: `doio/kb16/rev2`
- USB VID:PID: `303A:8360`
- Raw HID usage: `FF00:0001`
- Report ID: `6`
- RGB DMA: DMA1 Channel 6
- OLED DMA: DMA1 Channel 5

这些协议标识来自兼容性调查，并非公开稳定接口。ChatGPT Desktop 后续版本可能改变行为。

## 构建

把 `firmware/qmk-core-patches/` 中的文件按相同相对路径复制到 QMK 工作树，并把目标 keymap 复制到：

```text
keyboards/doio/kb16/keymaps/
```

构建当前固件：

```sh
make doio/kb16/rev2:codex_micro_configurable_v1_3 EXTRAFLAGS="-Werror"
```

运行 Mapper 测试：

```powershell
dotnet restore mapper/Doio.Kb16.Mapper.Tests/Doio.Kb16.Mapper.Tests.csproj --locked-mode
dotnet run --project mapper/Doio.Kb16.Mapper.Tests/Doio.Kb16.Mapper.Tests.csproj -c Release --no-restore
```

## 安全说明

固件构建与测试不会自动刷写键盘。刷写前请确认设备型号、Bootloader 和固件 SHA-256。已验证哈希见 [firmware/SHA256SUMS.txt](firmware/SHA256SUMS.txt)。

Mapper 不会刷写固件、不会安装后台服务、不会进入 Bootloader。它只通过键盘 Raw HID Channel 3 读取和写入键位/灯光配置。ChatGPT Desktop 的 Codex 状态灯走 Channel 2，Mapper 不代理也不翻译状态灯消息。

本项目与 OpenAI、DOIO 或 QMK 官方无隶属或背书关系。
