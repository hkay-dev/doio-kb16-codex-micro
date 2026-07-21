# DOIO KB16 Codex Micro

This firmware turns a **DOIO KB16-01 Rev2** into a surprisingly convincing
Codex Micro for the official ChatGPT desktop app. It works on macOS, keeps six
Codex tasks on six physical keys, shows their live states on the OLED, and uses
all 16 RGB LEDs for alerts and ambient animation.

No bridge, AppleScript helper, Karabiner rule, or background service is needed.
Plug the board in, open ChatGPT, and the app sees it as a Work Louder Codex
Micro.

> [!WARNING]
> This build is only for the `DOIO KB16-01 Rev2 / APM32F103CBT6`. Don't flash
> it onto Rev1, an ATmega32U4 board, or hardware you haven't identified.

## A big thank-you to the original project

This is a fork of
[hcyniubi/doio-kb16-codex-micro](https://github.com/hcyniubi/doio-kb16-codex-micro),
and that project did the hard part. It worked out the Codex Micro USB identity,
the Raw HID messages used by ChatGPT, the six Agent controls, the command
controls, joystick events, status lighting, and a safe configuration channel.
It also supplied the QMK base, four useful layers, a Windows Mapper, tests,
recovery notes, and careful hardware warnings.

That was an excellent starting place. This fork keeps the original MIT license
and the original copyright notice, and builds its Mac and lighting work on top
of that foundation.

## What the base firmware already did

The original firmware made the KB16 identify itself as the device ChatGPT
expects and speak the same native control language.

- Six keys act as Agent 1 through Agent 6.
- Six command keys cover Fast, Approve, Reject, Split, Mic, and Codex.
- Four keys stand in for the original planar joystick.
- The left encoder sends the native Codex dial events.
- ChatGPT sends each task's state and color back over Raw HID.
- Number, Navigation, System, and Codex layers give the board a useful life
  outside ChatGPT too.
- A Windows Mapper can rearrange controls and change the shared lighting on the
  three ordinary layers.
- Two CRC-checked EEPROM slots keep Mapper changes safe across restarts.

The Mapper is still included and still works with this firmware. It isn't
needed for normal Codex use on a Mac.

## What this fork adds

### Native support for the official Mac app

The macOS ChatGPT app already has the Codex Micro service and HID transport
inside it. This firmware matches the device identity that service looks for,
so ChatGPT discovers the KB16 directly.

The live path is simple.

```text
ChatGPT for macOS -> Raw HID Channel 2 -> KB16 -> keys, OLED, and RGB
Windows Mapper    -> Raw HID Channel 3 -> KB16 -> saved key and light settings
```

We tested native discovery, Agent selection, the Codex dial, task-state
updates, app-driven RGB, reconnects, and the on-device menu on a real KB16.

### Six live task slots

Each Agent key keeps its own state even when a full-board effect is running.
The palette was tuned for the KB16's RGB-only LEDs and acrylic bottom, so Idle
and Working don't blur into the same cold blue-white.

| State | Key light | OLED mark |
|---|---|---|
| Idle | Warm white `#FFD09A` | `I` |
| Working | Electric-blue breath `#005CFF` | `W` |
| Complete | Slow green breath | `C` |
| Needs input | Amber heartbeat | `!` |
| Error | Repeating red pulses | `E` |
| Empty | Off | `-` |
| Unknown | Host color | `?` |

Agent lighting follows the slot if you move an Agent control with the Mapper.
It isn't tied to the six original LED positions.

### Full-board alerts that still show the source slot

The whole acrylic case can light up when something needs you. Alerts are
layer-independent, so they still appear if you're using Number, Navigation, or
System when a task finishes.

- Done alerts come in Single, Double, and Wave styles.
- Done can run 1 through 9 times or keep going until you focus that task.
- Needs Input comes in Double, Heartbeat, and Snake styles.
- Error comes in Chase, Pulse, and Solid styles.
- Reconnect gets its own blue sweep.
- Unread completed tasks can remind you after 15, 30, 60, or 120 seconds.
- Error beats Needs Input, which beats Done, Reconnect, and reminders.
- Lower-priority alerts wait their turn instead of disappearing.
- Mute stops the big alerts and leaves every Agent key's state visible.

Three alert layouts make it clear which slot caused the animation.

| Layout | What it does |
|---|---|
| Slot Focus | Starts brightest at the source key and fades across the board |
| Full Board | Uses the same brightness on all 16 keys |
| Perimeter | Runs around the outer 12 keys and keeps the source key lit |

### A real OLED dashboard and settings menu

The Codex layer now has a compact dashboard for USB state, task slots, the
active alert, mute, and night mode. The text sits inside the part of the OLED
that remains visible under the stock display shroud.

The screen also gives you a complete lighting menu, so normal customization
doesn't need a computer.

- Hold **Right** for one second to open the menu.
- Turn **Right** to browse and press **Right** to open or run something.
- Turn the large **M** encoder to change a value.
- Press **M** to go back.
- Hold **Right** to save and close.

The menu covers alert switches and styles, Done repeats, alert layout,
reminders, mute, ambient area, idle glow, brightness, animation speed, night
mode, OLED brightness, and the dashboard. Save writes once. Cancel puts the
opening values back and doesn't write EEPROM.

The Preview section runs Solid, Snake, Rainbow, Breath, Gradient, Soft Breath,
Done, Needs Input, Error, Reconnect, and a six-slot Status Demo. The Slot Key
pages explain every dashboard mark right on the board.

### Smooth RGB animation on one shared matrix

The real Codex Micro has separate key and ambient lighting zones. The KB16 has
one RGB LED under each key, so the firmware blends both ChatGPT zones onto that
single 4 by 4 matrix.

- A serpentine path keeps Snake and Chase motion continuous across rows.
- Smoothstep fades cut the harsh steps out of pulses and breathing.
- Ambient animation sits under the six per-slot Agent colors.
- Full Board and Perimeter ambient areas are both available.
- Idle Glow can softly light the ten non-Agent keys from 0 through 30 percent.
- Brightness caps run from 10 through 100 percent.
- Animation speed runs from 50 through 150 percent.
- Night Mode adds a 35 percent ceiling.
- ChatGPT's Off, Solid, Snake, Rainbow, Breath, Gradient, and Shallow Breath
  effects all render locally.

## Physical controls

The names on the OLED match the actual layout on the right side of the KB16.

- **Left** is the small top-left encoder. The Codex layer keeps this locked to
  ChatGPT's native dial.
- **Right** is the small top-right encoder. It drives the on-device menu.
- **M** is the large encoder underneath them. It changes menu values and goes
  back.

Pressing M outside the menu cycles through Number, Navigation, System, and
Codex. Its short action waits for release, and a three-second hold cancels that
action and returns straight to Codex.

## Install it on a Mac

### 1. Check the board

Confirm that it is exactly a `DOIO KB16-01 Rev2 / APM32F103CBT6`. Back up the
factory firmware through SWD if you can. Stop if the chip has readout
protection. Don't clear protection or mass-erase it just to get a backup.

### 2. Get the tested firmware

Use
[`firmware/releases/v1.4.0/doio_kb16_rev2_codex_micro_oled_v1_4_0.bin`](firmware/releases/v1.4.0/doio_kb16_rev2_codex_micro_oled_v1_4_0.bin).
Check it against [`firmware/SHA256SUMS.txt`](firmware/SHA256SUMS.txt).

### 3. Flash through the Maple bootloader

The tested board appears as `1eaf:0003`, with its application on alternate
setting 2 at `0x08002000`.

```sh
dfu-util -d 1eaf:0003 -a 2 \
  -D doio_kb16_rev2_codex_micro_oled_v1_4_0.bin -R
```

Don't copy that command onto a different board revision or bootloader. QMK
Toolbox is also fine on macOS once it shows the expected Maple device.

### 4. Open ChatGPT

Open the official ChatGPT app, sign in, open Codex, and plug the KB16 in. Grant
Input Monitoring if macOS asks. The board should return as `Work Louder Codex
Micro` with USB ID `303A:8360`.

The first six Agent keys should follow your six Codex tasks. A single tap
focuses the task in the background, a double tap brings ChatGPT forward, and a
long press on the native Left encoder opens ChatGPT's hardware page.

## Build it yourself

Copy
[`firmware/keymaps/codex_micro_configurable_v1_3`](firmware/keymaps/codex_micro_configurable_v1_3)
into a QMK checkout, apply the descriptor files from
[`firmware/qmk-core-patches`](firmware/qmk-core-patches), and run this from the
QMK root.

```sh
make doio/kb16/rev2:codex_micro_configurable_v1_3 EXTRAFLAGS="-Werror"
```

The folder keeps its v1.3 name because the 340-byte Mapper format and Channel 3
protocol haven't changed. The OLED and lighting build itself is v1.4.0.

CI runs the protocol, configuration, settings, alert, menu, Mapper, and static
compatibility tests. The release build also passes an ARM compile with warnings
treated as errors.

## Limits worth knowing

- This imitates the official device's USB identity and behavior. OpenAI hasn't
  published this as a stable hardware API, so a later ChatGPT update could
  change it.
- The KB16 has 16 keys and three encoders. It doesn't gain the official
  hardware's touch sensor, true planar joystick, Bluetooth, or separate ambient
  LED zone.
- Four keys stand in for joystick directions.
- The included Mapper is Windows-only. Saved settings stay on the keyboard when
  you move it back to a Mac, and the on-device lighting menu works everywhere.
- Work Louder Input can discover the board, but this firmware doesn't implement
  its device filesystem calls.
- The repo doesn't include the factory DOIO firmware.

The deeper protocol notes, Mac findings, and recovery details live in
[`docs/macos-chatgpt-adaptation.md`](docs/macos-chatgpt-adaptation.md) and
[`docs/usage.md`](docs/usage.md).

## Credits and license

- Original project and reverse-engineering work by
  [hcyniubi](https://github.com/hcyniubi)
- Built on [QMK Firmware](https://qmk.fm/)
- Inspired by the
  [Work Louder Codex Micro](https://worklouder.cc/codex-micro)
- Tested with the official ChatGPT desktop app from OpenAI

This is an unofficial community project. It isn't affiliated with or endorsed
by OpenAI, Work Louder, DOIO, or QMK.

The code stays under the original [MIT license](LICENSE).
