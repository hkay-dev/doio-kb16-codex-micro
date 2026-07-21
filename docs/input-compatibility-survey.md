# DOIO KB16 and Work Louder Input compatibility survey

Date: 2026-07-19

Target: DOIO KB16 Rev2 running `codex_micro_compat_v1_2`

Method: live enumeration, Input log analysis, and offline static analysis of Input 0.17.0. No update was installed, no configuration was written, and no device was flashed.

## Conclusion

The current v1.2 firmware passes Input's device discovery and basic RPC handshake, but Input can't configure it yet.

- Input correctly identifies the device as `codex_micro`.
- `sys.version`, `device.status`, and `host.focused_app` work.
- Input then calls `fs.list` to read the device configuration files, and the firmware returns `Method not found`.
- Input can't load or save keys, encoders, the joystick, layers, or shortcuts because it can't get `keymap.json` and `smart_actions.json`.
- USB identity isn't the compatibility blocker. The missing pieces are the device filesystem and a dynamic keymap execution layer.

Current rating is **discovery compatible, basic RPC compatible, configuration protocol incompatible**.

## Observed state

- USB: `VID 303A / PID 8360`
- Input device type: `codex_micro`
- HID transport: vendor-defined Usage Page `0xFF00`
- Installed Input release: `0.17.0`
- Downloaded but not installed: `0.17.1`
- The only official 0.17.1 change fixes a lighting reversal when closing a popup. It doesn't change the Codex Micro device model or file protocol.
- Input was already running when the survey began. The survey didn't start, stop, or use its interface.

Successful responses in the Input log:

```json
{"id":297,"result":{"version":"0.1.0-qmk"}}
{"id":608,"result":{"ok":true}}
{"id":534,"result":{"version":"0.1.0-qmk","profile_index":0,"layer_index":1,"battery":100,"is_charging":false}}
```

The important failure was:

```text
method: fs.list
Error: Method not found
```

The logs have no `fs.write`, `fs.writebin`, `fs.delete`, `fs.begin`, `fs.commit`, `sys.bootloader`, or `sys.selftest` calls, so there's no sign that this connection wrote configuration or entered a flashing flow.

## How Input identifies Codex Micro

Input 0.17.0 uses these checks:

1. VID must be decimal `12346`, which is `0x303A`.
2. PID `33632`, which is `0x8360`, is registered as `codex_micro`.
3. HID Usage Page must be `65280`, which is `0xFF00`.
4. The vendor string should match `Work Louder` or `Work_Louder`. When no vendor matches, Input falls back to filtering by VID alone.

v1.2 meets these checks, so its USB identity doesn't need to change.

## ChatGPT and Input ownership

Input's built-in native Codex Micro layer 1 has:

- `AG00-AG05`
- `ACT06-ACT12`
- `ENC_CC / ENC_CW / ENC_CLK`
- Vendor Joystick

Input locks Codex Micro layer 1 and tells the user to configure that layer in the Codex Micro app. That splits responsibility this way:

- ChatGPT and Codex own the native task keys, command keys, voice, skills, and native encoder.
- Input mainly owns later custom layers, ordinary keyboard shortcuts, actions, multi-actions, lighting, and AppSense.
- The exact handoff between ChatGPT's custom shortcuts and Input can't be proven from the current interface text alone. Input 0.17.0's static code doesn't let the user edit native layer 1 directly.

## Input's Codex Micro hardware model

Input always shows the native Codex Micro as:

- 13 mechanical keys in a `2 + 4 + 4 + 3` layout
- 1 encoder with counterclockwise, clockwise, and press actions
- 1 joystick
- Up to 6 programmable layers

The DOIO KB16 maps to that model like this:

- 12 physical Codex keys can cover `AG00-AG05`, `ACT06-ACT10`, and `ACT12`.
- This project has no physical key for the native model's `ACT11`, so that slot should stay empty.
- The four physical arrow keys can act as the joystick's four directions.
- The left encoder can act as Input's one native encoder.
- The middle and right encoders don't appear in Input's Codex Micro model and should keep their fixed QMK behavior.
- Input shows the native Codex Micro shape, not the KB16's physical 4x4 layout.

## Configuration files Input expects

Input manages two main files through the device filesystem.

### `keymap.json`

It has:

- Configuration version and current profile
- Profiles and layers
- Per-layer key matrix, encoder, and joystick maps
- Actions and macros
- Multi-actions
- Action groups
- AppSense `linkedApps`
- Lighting settings

### `smart_actions.json`

It holds host-assisted Smart Actions such as:

- Insert text: `kb.sa.inserttext`
- Run a command: `kb.sa.exec`
- Open a URL: `kb.sa.openurl`
- Open an app: `kb.sa.openapp`

Input 0.17.0 disables both Smart Actions and the Cheat Sheet for Codex Micro, so the first compatible release doesn't need these riskier actions.

## Missing RPC methods

Input needs these file RPC methods:

| RPC | Purpose | v1.2 |
|---|---|---|
| `fs.list` | List files, sizes, and checksums | Missing |
| `fs.readbin` | Read a file in chunks | Missing |
| `fs.writebin` | Write Base64 chunks | Missing |
| `fs.delete` | Delete a file | Missing |
| `fs.begin` / `fs.commit` | Multi-file transaction | Missing, can wait for a later release |
| `fs.read` / `fs.write` | Small JSON-file interface | Missing, and Input's main path mostly uses the chunked interface |

Already present and tested:

| RPC | State |
|---|---|
| `sys.version` | Passes |
| `device.status` | Passes |
| `host.focused_app` | Passes, though testing only proved that the firmware receives and acknowledges it |
| `lights.preview` | v1.2 acknowledges it |
| Codex `v.oai.*` | Tested on real v1.2 hardware |

Each `fs.writebin` chunk can carry up to 4,096 Base64 characters. Its arguments have the filename, data, offset, append flag, and completion flag. `fs.readbin` must return Base64 data and `total_size`.

## Suggested next stage

Create a separate `codex_micro_input_compat_v1_3` keymap and leave v1.2 alone.

1. Add read-only `fs.list` and `fs.readbin` with a minimal valid `keymap.json`, then check whether Input can open the full Codex Micro page.
2. Store the configuration in a separate MCU flash or EEPROM region with dual buffers, a length, a version, and a CRC so power loss can't corrupt both copies.
3. Add `fs.writebin` for `keymap.json` only. Cap the total length and offsets, validate the JSON shape, and switch buffers atomically when the upload finishes.
4. Add an Input-keycode executor for five custom layers covering QMK keycodes, chords, actions, and multi-actions. Keep the native first layer on the current Codex event path.
5. Use `host.focused_app` and `linkedApps` for automatic layer switching, with the middle encoder and a physical recovery path still available.
6. Leave out Smart Actions, command execution, firmware updates, bootloader entry, self-test, and writes to arbitrary filenames in the first release.
7. Keep the middle and right encoders fixed in firmware. Let Input control only the left encoder, 12 physical Codex keys, and the four directions.

## Risks and acceptance checks

- Check whether QMK's available flash and RAM can hold the JSON, chunk buffer, stored configuration, and executor.
- Input models 13 native keys, while this project has only 12 non-joystick keys. The empty `ACT11` slot must stay safely disabled.
- Input advertises 6 layers, while the current KB16 firmware has 4. Expanding to 6 needs its own decision and shouldn't silently change the middle encoder's current layer behavior.
- ChatGPT and Input may open the same Raw HID interface at once. Test concurrent I/O, reconnects, and message fragmentation.
- Block every Input path that could update firmware or enter the bootloader.
- Keep both a restore-default action and a Bootmagic recovery path before allowing configuration writes.

## Not done during this survey

- Input 0.17.1 wasn't installed.
- No Save, Sync, or Update control was used in Input.
- No test write was sent to the device.
- No QMK source or firmware was changed.
- Nothing was compiled or flashed.

## Sources

- Work Louder Input: <https://worklouder.cc/input>
- Codex Micro product page: <https://worklouder.cc/codex-micro>
- Input 0.17.1 release notes: <https://github.com/worklouder/input-releases/releases/tag/v0.17.1>
- Local Input log: `C:\Users\黄辰飏\AppData\Roaming\input\logs\main.log`
- Local Input 0.17.0 installation: `C:\Users\黄辰飏\AppData\Local\Programs\input`
- v1.2 firmware source and artifact: `outputs\codex_micro_compat_v1_2`
