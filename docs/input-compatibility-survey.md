# DOIO KB16 / Work Louder Input 兼容性勘测

日期：2026-07-19
对象：DOIO KB16 Rev2，`codex_micro_compat_v1_2`
方式：实时枚举、Input 日志取证、Input 0.17.0 离线静态分析；未安装更新、未写配置、未刷写设备。

## 结论

当前 v1.2 已通过 Input 的设备发现和基础 RPC 握手，但尚未达到 Input 可配置状态。

- Input 将设备正确识别为 `codex_micro`。
- `sys.version`、`device.status` 和 `host.focused_app` 已能正常通信。
- Input 随后调用 `fs.list` 读取设备配置文件，固件返回 `Method not found`。
- 因为无法取得 `keymap.json` 和 `smart_actions.json`，Input 无法载入或保存按键、旋钮、摇杆、层及快捷键配置。
- 兼容性瓶颈不是 USB 身份，而是设备文件系统与动态键位执行层。

当前可评为：**发现兼容、基础 RPC 兼容、配置协议不兼容**。

## 现场状态

- USB：`VID 303A / PID 8360`
- Input 识别类型：`codex_micro`
- HID 通信：厂商自定义 Usage Page `0xFF00`
- Input 安装版本：`0.17.0`
- 已下载但未安装版本：`0.17.1`
- 0.17.1 官方变更仅为修复关闭弹窗时灯光变化反转，不涉及 Codex Micro 设备模型或文件协议。
- 勘测开始时 Input 已在运行；本次没有启动、关闭或操作其界面。

Input 日志中的成功响应：

```json
{"id":297,"result":{"version":"0.1.0-qmk"}}
{"id":608,"result":{"ok":true}}
{"id":534,"result":{"version":"0.1.0-qmk","profile_index":0,"layer_index":1,"battery":100,"is_charging":false}}
```

关键失败：

```text
method: fs.list
Error: Method not found
```

日志中没有发现 `fs.write`、`fs.writebin`、`fs.delete`、`fs.begin`、`fs.commit`、`sys.bootloader` 或 `sys.selftest`，因此没有证据表明本次连接写入了配置或进入刷写流程。

## Input 如何识别 Codex Micro

Input 0.17.0 使用以下条件：

1. VID 必须为十进制 `12346`，即 `0x303A`。
2. PID `33632`，即 `0x8360`，被注册为 `codex_micro`。
3. HID Usage Page 必须为 `65280`，即 `0xFF00`。
4. 厂商字符串优先匹配 `Work Louder` 或 `Work_Louder`；找不到厂商匹配时会退回只按 VID 筛选。

v1.2 已满足这些条件，所以无需更改 USB 身份。

## ChatGPT 与 Input 的职责边界

Input 内置的 Codex Micro 原生第 1 层包含：

- `AG00–AG05`
- `ACT06–ACT12`
- `ENC_CC / ENC_CW / ENC_CLK`
- Vendor Joystick

Input 明确锁定 Codex Micro 的第 1 层，界面提示该层必须使用 Codex Micro 应用配置。因此：

- ChatGPT/Codex 继续管理原生任务键、命令键、语音、技能和原生旋钮。
- Input 主要管理后续自定义层、普通键盘快捷键、Action、Multi Action、灯光和 AppSense。
- ChatGPT 中“自定义快捷键”与 Input 的具体交接方式，不能仅凭现有 UI 文案确认；Input 0.17.0 的静态代码并不允许直接编辑原生第 1 层。

## Input 的 Codex Micro 硬件模型

Input 固定显示原生 Codex Micro 的：

- 13 个机械键，排列为 `2 + 4 + 4 + 3`
- 1 个旋钮，包含逆时针、顺时针和按下三个动作
- 1 个摇杆
- 最多 6 个可编程层

与 DOIO KB16 的对应关系：

- 12 个实体 Codex 键可对应 `AG00–AG05`、`ACT06–ACT10`、`ACT12`。
- 原生模型中的 `ACT11` 在本项目中没有实体键，应保持空槽。
- 四个实体方向键可作为摇杆的四方向输入。
- 左旋钮可作为 Input 唯一显示的原生旋钮。
- 中、右旋钮不会出现在 Codex Micro 的 Input 模型中，应继续保持 QMK 原有行为。
- Input 不会显示 KB16 的物理 4×4 外观，而会显示原生 Codex Micro 外观。

## Input 需要的配置文件

Input 通过设备文件系统管理两份主要文件：

### `keymap.json`

包含：

- 配置版本与当前 Profile
- Profiles 和 Layers
- 每层按键矩阵、旋钮和摇杆映射
- Actions/macros
- Multi Actions
- Action 分组
- AppSense 的 `linkedApps`
- 灯光配置

### `smart_actions.json`

包含主机协助执行的 Smart Actions，例如：

- 插入文本：`kb.sa.inserttext`
- 执行命令：`kb.sa.exec`
- 打开网址：`kb.sa.openurl`
- 打开应用：`kb.sa.openapp`

Input 0.17.0 的 Codex Micro 功能标记中，Smart Action 和 Cheat Sheet 当前均为禁用状态。因此首版兼容无需实现这些高风险动作。

## 缺失的 RPC

Input 配置所需的文件 RPC：

| RPC | 用途 | v1.2 |
|---|---|---|
| `fs.list` | 列出文件、大小和校验值 | 缺失 |
| `fs.readbin` | 分块读取文件 | 缺失 |
| `fs.writebin` | Base64 分块写入文件 | 缺失 |
| `fs.delete` | 删除文件 | 缺失 |
| `fs.begin` / `fs.commit` | 多文件事务 | 缺失，首版可暂缓 |
| `fs.read` / `fs.write` | 小型 JSON 文件接口 | 缺失，Input 主流程主要使用分块接口 |

现有且已验证：

| RPC | 状态 |
|---|---|
| `sys.version` | 通过 |
| `device.status` | 通过 |
| `host.focused_app` | 通过，但目前只确认收到并应答 |
| `lights.preview` | v1.2 已应答 |
| Codex `v.oai.*` | 已由 v1.2 实机验证 |

`fs.writebin` 每块最多承载 4096 个 Base64 字符，参数包括文件名、数据、偏移、是否追加和是否完成。`fs.readbin` 需要返回 Base64 数据及 `total_size`。

## 下一阶段建议

建议新建独立 `codex_micro_input_compat_v1_3`，不修改 v1.2：

1. 先实现只读 `fs.list` 和 `fs.readbin`，提供最小有效的 `keymap.json`，验证 Input 能完整打开 Codex Micro 页面。
2. 将配置存储在 MCU Flash/EEPROM 的独立区域，采用双缓冲、长度、版本和 CRC，避免断电损坏。
3. 再实现 `fs.writebin`，只允许写入 `keymap.json`；限制总长度、偏移和 JSON 结构，完成后原子切换。
4. 为 5 个自定义层实现 Input keycode 到 QMK 键码、组合键、Action 和 Multi Action 的执行器；原生第 1 层继续使用现有 Codex 事件。
5. 接入 `host.focused_app` 的 `linkedApps` 自动切层，但保留物理中旋钮切层和恢复路径。
6. 首版不实现 Smart Action、命令执行、固件更新、Bootloader、自测试或任意文件名写入。
7. 保持中、右旋钮为固件固定功能；Input 只控制左旋钮和 12 个实体 Codex 键/四方向。

## 风险与验收重点

- QMK 可用 Flash 和 RAM 是否容纳 JSON、分块缓冲、配置存储及执行器。
- Input 使用 13 键原生模型，而本项目只有 12 个非摇杆键；必须稳定屏蔽 `ACT11` 空槽。
- Input 声称 6 层，而当前 KB16 固件为 4 层；是否扩为 6 层需要单独确认，不能静默改变中旋钮现有切层体验。
- ChatGPT 和 Input 可能同时打开同一 Raw HID 接口，需验证并发读写、断线重连和消息分片不会互相干扰。
- 必须阻止 Input 通过任何路径触发固件更新或 Bootloader。
- 写配置前必须保留恢复默认配置和 Bootmagic 恢复路径。

## 本次未执行

- 未安装 Input 0.17.1
- 未点击 Input 中的保存、同步或更新
- 未向设备发送测试写入
- 未修改 QMK 源码或固件
- 未编译、未刷写设备

## 资料来源

- Work Louder Input：<https://worklouder.cc/input>
- Codex Micro 产品说明：<https://worklouder.cc/codex-micro>
- Input 0.17.1 发布说明：<https://github.com/worklouder/input-releases/releases/tag/v0.17.1>
- 本机 Input 日志：`C:\Users\黄辰飏\AppData\Roaming\input\logs\main.log`
- 本机 Input 0.17.0：`C:\Users\黄辰飏\AppData\Local\Programs\input`
- v1.2 固件源码与工件：`outputs\codex_micro_compat_v1_2`
