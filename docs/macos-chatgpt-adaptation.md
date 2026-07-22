# macOS adaptation for the official ChatGPT app

## Finding

The Codex layer works on macOS with the official ChatGPT app and the v1.4.0 firmware. A physical KB16 Rev2 connected as `303A:8360`, ChatGPT sent native status and RGB calls, an Agent key selected its task, and a long encoder press opened the in-app hardware page.

The Windows Mapper is the only Windows-only piece. It's optional for normal Codex use, though it's still needed to change the three ordinary layers and their shared lighting unless we build a Mac Mapper.

The v1.4.0 lighting build adds the parts v1.3.5 was missing. It reads the app's numeric effect IDs, animates both global lighting zones on the KB16's shared LED matrix, and runs a configurable green alert when a task becomes complete.

## Why the existing firmware should connect

The installed official app at `/Applications/ChatGPT.app` was inspected on 2026-07-21.

| App fact | Installed value |
|---|---|
| Bundle identifier | `com.openai.codex` |
| App version | `26.715.61943` |
| Bundled hardware library | `@worklouder/device-kit-oai` |
| macOS HID transport | Native topology watcher plus Darwin ARM64 `node-hid` |

Its bundled `codex-micro-service` discovers a Codex Micro with these exact values.

| Field | Decimal | Hex |
|---|---:|---:|
| Vendor ID | `12346` | `303A` |
| Product ID | `33632` | `8360` |
| Usage Page | `65280` | `FF00` |

The service also treats a device as USB when its release number is divisible by four. This firmware uses `DEVICE_VER 0x0100`, which meets that check.

The repo's `config.h` already supplies the same VID, PID, Usage Page, Usage ID, Report ID 6, 64-byte reports, manufacturer, and product name. The Mac app has native Codex Micro discovery code, so it doesn't need a keyboard shortcut shim, AppleScript, Karabiner-Elements, or a local bridge.

This matches Work Louder's own claim that Codex Micro supports Mac and Windows and is configured inside ChatGPT Codex. See the [Codex Micro product page](https://worklouder.cc/codex-micro) and [official setup guide](https://worklouder.cc/openai-micro-setup).

## What the ChatGPT app already handles

The bundled Mac app understands the same native events emitted by this firmware.

- `AG00` through `AG05` select six agent or thread slots.
- `ACT06`, `ACT07`, `ACT08`, `ACT09`, `ACT10`, and `ACT12` run command-key actions.
- `ENC_CC`, `ENC_CW`, and the encoder switch control composer navigation or reasoning depth.
- `v.oai.rad` carries joystick angle and distance.
- `v.oai.thstatus` sends six thread-light states back to the keyboard.
- `v.oai.rgbcfg` sends ambient and key lighting.

The app's built-in default command layout is Fast, Approve, Reject, Split, Mic, and Codex. Its default joystick directions toggle Plan mode, go forward, toggle the sidebar, and go back. The app stores a configurable command ID for each command slot, so the native layer can be remapped inside ChatGPT.

Work Louder's setup guide says a single Agent Key tap focuses its thread in the background, a double tap brings ChatGPT forward, and a long dial press opens the hardware configuration page inside Codex.

The Mac app also has an Input Monitoring permission check and a direct link to the matching macOS privacy pane. Expect to grant that permission when ChatGPT asks for it. The HID transport itself is bundled in the app.

## Lighting behavior

Work Louder documents six Agent key states. White means idle, blue means thinking, green means complete, amber means the task needs input, red means error, and off means no task is assigned.

This KB16 build intentionally leaves Idle dark. Complete, needs-input and
error states pulse smoothly in their own colors until you press the matching
Agent key.

The inspected ChatGPT app has seven numbered effects for thread, key, and ambient lighting.

| ID | Effect | KB16 rendering |
|---:|---|---|
| `0` | Off | LEDs off |
| `1` | Solid | One steady color |
| `2` | Snake | A six-key soft tail moving through the 4 by 4 grid |
| `3` | Rainbow | A moving hue spread across the grid |
| `4` | Breath | A full fade from dark to bright |
| `5` | Gradient | A smooth brightness gradient across the grid |
| `6` | Shallow breath | A gentler fade from half brightness to full |

ChatGPT uses ambient snake lighting for a selected task that's working, voice recording, voice processing, and its global snaking status. It uses ambient solid lighting for a selected non-working task and briefly after task selection. A selected or pulsing task key breathes. The app also sends brightness, animation speed, key-zone sync, and ambient-zone sync.

The real Codex Micro has separate key and ambient zones. The KB16 has one RGB LED under each of its 16 keys, and the acrylic bottom diffuses those LEDs through the whole case. The firmware composites both app zones onto that shared matrix. An animated ambient effect moves over a dim key-zone base, and the six assigned task LEDs still show their own per-thread colors on top.

When any known thread changes into ChatGPT's green complete state, the firmware runs the saved Done style across the board. Single, Double, and Wave styles can repeat one through nine times or run until that task is focused. The alert also appears on the Number, Navigation, and System layers. Once it ends, the board goes straight back to its per-key task colors and ordinary layer lighting. The first status snapshot after connecting doesn't fire old completion alerts.

## The practical Mac setup

1. Confirm that the keyboard is exactly `DOIO KB16-01 Rev2 / APM32F103CBT6`.
2. Make a recoverable factory backup if possible. Stop if SWD hits readout protection. Don't clear protection or mass-erase the chip.
3. Check the v1.4.0 BIN against `firmware/SHA256SUMS.txt`.
4. Put the board into its known Maple or STM32duino bootloader path and flash the BIN with QMK Toolbox on macOS or another already-tested process.
5. Launch the official ChatGPT app, sign in, open Codex, and connect the keyboard by USB.
6. Grant Input Monitoring if ChatGPT asks.
7. Long-press the native left encoder in Codex and check that the hardware configuration page opens.
8. Run the acceptance checks below.

QMK lists QMK Toolbox as the recommended macOS GUI and supports flashing `.bin` files. The exact bootloader still matters here because STM32F103 boards may use STM32duino rather than factory STM32 DFU. Follow the [QMK flashing guide](https://docs.qmk.fm/flashing), but don't guess a bootloader or flash address from a generic STM32 example.

## Physical checks

The physical board passed discovery, ChatGPT connection, Agent-key selection, long-press configuration, OLED navigation, saved settings, status previews, live task colors, and the full-board Done alert. These are still useful regression checks after a ChatGPT or firmware update.

1. Start a task and watch its assigned key breathe blue.
2. Select that working task and watch a blue snake move across the acrylic glow.
3. Finish the task and watch two full-board green pulses, followed by a green task-key pulse that stops when you press it.
4. Trigger a task that needs approval and check that its amber pulse keeps running until you press its Agent key.
5. Trigger an error and check that its red pulse keeps running until you press its Agent key.
6. Hold push-to-talk and check the recording snake, processing snake, and completed white state.
7. Keep several task states visible and make sure their separate key colors survive every global animation.
8. Switch to Number, Navigation, or System during a completion alert and make sure the green pulse still appears.
9. Quit and reopen ChatGPT, reconnect USB, wake from sleep, and repeat discovery.
10. Keep ChatGPT open and run a read-only Channel 3 hello from a Mac test tool to check shared HID access.

Stop at read-only checks if discovery fails. Capture the macOS IORegistry entry, report lengths, release number, Usage Page, and ChatGPT logs before changing firmware.

## Customizing the other three layers

There are three usable paths.

### Configure once on Windows

Use the translated Windows Mapper to set Number, Navigation, System, and their shared HSV lighting. The settings live in the keyboard's wear-levelled EEPROM, so the keyboard keeps them when moved back to the Mac.

This is the shortest route to a working device.

### Use Work Louder Input on Mac

Work Louder ships Input for Apple Silicon and Intel Macs, but the current firmware only passes discovery and the basic RPC handshake. Input then asks for `fs.list`, `fs.readbin`, and the rest of its device filesystem. This firmware doesn't implement those calls, so Input can't edit it yet.

Input isn't a replacement for the Mapper with this firmware release.

### Build a native Mac Mapper

Port the Channel 3 client and configuration model to a small SwiftUI app. Keep the firmware unchanged.

Suggested modules are:

- `CodexMicroHID` for IOHID discovery, open, read, reconnect, and device notifications
- `Channel3Protocol` for reports, request IDs, chunking, status codes, and timeouts
- `KB16Configuration` for the 340-byte binary model, semantic checks, CRC32, and JSON import or export
- `KB16Mapper` for the SwiftUI editor, drafts, confirmation, lighting preview, and readback checks

Start with a signed command-line probe that only runs Hello and Read Configuration. Move to SwiftUI only when the read-only path works with ChatGPT open.

## Channel 3 contract for the Mac Mapper

Every report is 64 bytes.

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 1 | Report ID `6` |
| 1 | 1 | Channel `3` |
| 2 | 1 | Protocol version `1` |
| 3 | 1 | Opcode, with `0x80` set in replies |
| 4 | 2 | Little-endian request ID |
| 6 | 1 | Chunk index |
| 7 | 1 | Chunk count |
| 8 | 1 | Payload length |
| 9 | Up to 55 | Payload |

The operations are:

| Opcode | Operation |
|---:|---|
| 1 | Hello |
| 2 | Read configuration |
| 3 | Begin write |
| 4 | Write chunk |
| 5 | Commit write |
| 6 | Restore v1.2 defaults |
| 7 | Read lighting |
| 8 | Preview lighting in RAM |
| 9 | Save lighting |
| 10 | Restore the pre-preview lighting state in RAM |

The 340-byte configuration has 16 native control bytes followed by six four-byte Codex encoder actions, 48 four-byte ordinary-layer key actions, and 27 four-byte ordinary-layer encoder actions. Each action is kind, modifier bits, and a little-endian 16-bit code.

Keep the Windows Mapper's safety behavior in the Mac port.

- Read generation, CRC, and all 340 bytes before editing.
- Keep changes in a local draft.
- Read the full snapshot again right before writing.
- Block a stale write.
- Commit through the staged chunk protocol.
- Read the result back and compare its CRC and bytes.
- Preview lighting without EEPROM writes.
- Restore the complete opening lighting state on cancel.
- Keep restore-defaults and bootloader entry behind clear confirmation.

## Risks

### The integration is private

The VID, PID, event names, and `v.oai.*` methods aren't a published OpenAI hardware API. The firmware works by matching the official device's identity and behavior. A ChatGPT update can change this contract.

Pin a known-good app version in the test record and rerun the acceptance matrix after every desktop-app update.

### Shared HID access still needs a real test

ChatGPT uses Channel 2 and the Mapper uses Channel 3, but both ride the same vendor HID interface. macOS usually allows non-exclusive IOHID opens, though the exact `node-hid` and IOHID combination needs a physical concurrency test.

Don't add a bridge unless that test proves one is needed. A Mapper can close and reconnect around writes if shared access is unreliable.

### Report framing can differ on macOS

The Windows client accepts either a 64-byte report or a leading zero plus the 64-byte report. The Mac probe should log only report length and header bytes, never user data, and should handle the report-ID convention used by IOHID on this device.

### The hardware only emulates the official shape

The official Codex Micro has 13 switches, one touch sensor, one encoder, and a planar joystick. The KB16 has 16 keys and three encoders. This firmware maps four keys to joystick directions and keeps two extra encoders for local QMK layers. It reproduces the protocol and workflow, not the original physical controls, Bluetooth, touch layer switching, or enclosure.

### Flashing is the highest-risk step

The included BIN is only for `DOIO KB16-01 Rev2 / APM32F103CBT6`. The project doesn't ship factory firmware. A wrong revision, bootloader, or flash process can leave the board needing SWD recovery.

## OLED lighting release

The OLED lighting build was flashed successfully on July 21, 2026.

| Item | Value |
|---|---|
| Release BIN | `firmware/releases/v1.4.0/doio_kb16_rev2_codex_micro_oled_v1_4_0.bin` |
| Size | `61688` bytes |
| SHA-256 | `c3e68e3ef48a52a9a218daa855410bcfa62a8bc921e16f575337f62c43385da0` |

The flash command is:

```sh
dfu-util -d 1eaf:0003 -a 2 -D doio_kb16_rev2_codex_micro_oled_v1_4_0.bin -R
```

The exact HID descriptor, USB identity, raw endpoints, manufacturer and
product strings each appeared once in the final BIN. The board returned as
`Work Louder Codex Micro` with `303A:8360` after reset.

## Physical acceptance

1. Confirm the Codex dashboard shows `USB` and six slot marks.
2. Hold the right encoder for one second and confirm the Lighting menu opens.
3. Open Preview and try Solid, Snake, Rainbow and Complete.
4. Cancel and confirm the ordinary task colors return.
5. Save a visible setting such as Perimeter or 50 percent brightness.
6. Unplug and reconnect the board and confirm that setting survives.
7. Finish a real ChatGPT task and confirm the configured completion alert runs.
8. Trigger or wait for a needs-input and error state when available.
9. Leave a completed task unread through the reminder interval and confirm the reminder repeats until its Agent key is pressed.

The native Mac connection works. A read-only Swift Channel 3 probe is still
the next piece if a Mac configuration app matters later.
