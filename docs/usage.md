# DOIO KB16 Codex Micro 使用教程

适用硬件：`DOIO KB16-01 Rev2 / APM32F103CBT6`。不要在 Rev1、ATmega32U4 版本或未确认硬件上刷写。

## 1. 下载文件

从 GitHub Release `v1.3.5` 下载两个文件：

- 固件：`doio_kb16_rev2_codex_micro_configurable_v1_3_5_status_colors.bin`
- Mapper：`Doio.Kb16.Mapper-v1.3.5-preview-complete.zip`

校验 SHA-256：

```text
B21691424B22950562247E8B36D6307A0E0CCA481221C22592D9EE8ADBA45D0F  doio_kb16_rev2_codex_micro_configurable_v1_3_5_status_colors.bin
65FB2D298D27C5533A1EA400D0CAAB46465BFDB4D0B9FA7ABCB5E8CEB68DBB89  Doio.Kb16.Mapper-v1.3.5-preview-complete.zip
```

## 2. 刷写固件

刷写前建议先备份原厂固件。真正可恢复的备份优先使用 SWD 全 Flash 读取；如果遇到读保护，请停止，不要解除保护或量产擦除。

本仓库只提供已构建的 QMK BIN，不提供原厂固件。刷写时请确认：

- 目标硬件是 `DOIO KB16-01 Rev2 / APM32F103CBT6`
- Bootloader 是 Maple/STM32duino 路径
- 选择的 BIN 文件名和 SHA-256 与上方一致

刷写本身请使用你已经验证可用的本机流程。本项目的 Mapper 不会替你刷写固件。

## 3. 安装 ChatGPT Desktop

Codex 层状态灯不需要 Bridge、Input、代理服务或后台程序。

正常使用路径是：

```text
ChatGPT Desktop -> Raw HID Channel 2 -> KB16 firmware -> Codex task/status lights
Mapper          -> Raw HID Channel 3 -> KB16 EEPROM configuration
```

在另一台 Windows x64 电脑上使用时，只需要：

1. 安装并登录 ChatGPT Desktop。
2. 连接已经刷入匹配固件的 KB16。
3. 如需编辑键位或普通层灯光，复制完整 Mapper ZIP 或完整解压目录。

不需要安装 .NET runtime、Work Louder Input、QMK 工具链或后台服务。

## 4. 运行 Mapper

解压 `Doio.Kb16.Mapper-v1.3.5-preview-complete.zip`，保持七个文件在同一目录：

- `Doio.Kb16.Mapper.exe`
- `Doio.Kb16.Mapper.pdb`
- `D3DCompiler_47_cor3.dll`
- `PenImc_cor3.dll`
- `PresentationNative_cor3.dll`
- `vcruntime140_cor3.dll`
- `wpfgfx_cor3.dll`

运行 `Doio.Kb16.Mapper.exe`。如果只复制 EXE，程序可能无法打开。

## 5. 编辑键位

点击 `重新读取` 后，Mapper 会读取设备当前 Generation、CRC 和完整 340 字节配置。

- Codex 层：16 个原生控制只能互相交换，不能重复或缺失。左旋钮锁定为 Codex 原生旋钮。
- Number / Navigation / System：可配置按键、媒体、鼠标、滚轮、层切换、RGB、OLED 亮度和受保护的 Bootloader 动作。
- 修改只保存在本地草稿中，点击 `应用到设备` 并确认后才会写入键盘。

写入前 Mapper 会再次读取设备快照。如果 Generation、CRC 或配置内容已经变化，写入会被阻止，避免覆盖别的编辑。

## 6. 编辑普通层灯光

v1.3.5 固件支持 Number、Navigation、System 三层共用一组普通层常亮 HSV 灯光。

点击 `重新读取` 左侧的彩虹按钮可以打开灯光面板：

- 拖动 Brightness / Hue / Saturation 会即时预览，但不会写 EEPROM。
- `保存灯光` 会持久化当前 HSV，并回读校验。
- `取消` 或关闭弹窗会恢复打开面板前的完整 RGB Matrix 状态，不写 EEPROM。

Codex 层仍由 ChatGPT Desktop 通过 Channel 2 控制背景和六个任务状态灯。

## 7. 恢复和边界

Mapper 可以请求固件恢复 v1.2 默认键位，但这会写入设备配置。执行前请确认当前导出的 JSON 或已知恢复路径。

Mapper 不会：

- 刷写固件
- 安装后台服务
- 启动 Bridge 或 Input
- 自动进入 Bootloader
- 执行宏、文本输入、延迟序列或程序启动

如果遇到 HID 被占用，请关闭其他配置器后重新读取。
