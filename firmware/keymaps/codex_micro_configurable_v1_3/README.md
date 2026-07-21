# DOIO KB16 Codex Micro configurable v1.3

Independent successor to the verified v1.2 firmware. It preserves the Codex
Micro USB identity, Channel 2 JSON-RPC, OLED, lighting, DMA6, Bootmagic and four
fixed layers while adding a persistent dynamic configuration on binary Channel
3. This directory is a standalone source mirror and does not replace v1.2.

## Configuration model

- Codex layer: all sixteen native controls (`AG00`-`AG05`, four joystick
  directions, `ACT06/07/08/09/12`, Mic `ACT10`) must appear exactly once.
- The six AG status LEDs follow their controls when they are moved.
- Codex left encoder is locked to `ENC_CC`, `ENC_CW` and `ENC_SW`; its desktop
  short/long-press interpretation is unchanged. Codex right and middle encoders
  are editable.
- Number, Navigation and System expose sixteen keys plus all nine encoder
  controls. Actions support keyboard chords, consumer/media, mouse buttons and
  wheel, layer selection, RGB, OLED brightness and guarded Bootloader entry.
- Middle encoder press is deferred: a release before three seconds executes its
  configured action; a three-second hold cancels the short action and returns
  to Codex.
- The default payload exactly reproduces v1.2. Invalid or Bootmagic-cleared
  storage restores that default.

## Persistence and protocol

The 340-byte packed payload uses two 356-byte EEPROM slots with magic, schema,
generation and CRC32. A commit writes and verifies the inactive slot before it
becomes active. Restore-defaults invalidates stale slots before installing the
default, so an older higher generation cannot reappear after reboot.

Channel 2 remains the Codex Micro JSON-RPC path. Channel 3 uses 64-byte reports
with Report ID 6, protocol version, opcode, request ID, fragment index/count,
length and up to 55 payload bytes. It supports hello, read, staged write,
commit and restore-default operations. Bad versions, lengths, ordering, CRC,
actions and commits while input is held are rejected.

Mapper v1.3.5 extends Channel 3 without changing the protocol version or the
340-byte key configuration. Four independent lighting operations read the
current RGB Matrix state, preview a solid HSV color without EEPROM writes,
commit a solid HSV color, or restore the complete pre-preview state without an
EEPROM write. Number, Navigation and System share this RGB Matrix state. The
Codex layer uses its host-driven background and six task lights.

The Codex renderer accepts the official numeric and string effect forms for
off, solid, snake, rainbow, breath, gradient and shallow breath. Brightness,
speed and thread-to-zone sync flags are handled too. A serpentine 16-LED path
keeps moving effects continuous across the 4 by 4 grid. Since the KB16 has one
shared lighting zone, key and ambient settings are composited on the same LEDs,
with per-thread colors kept on their six assigned keys.

The local alert engine adds configurable whole-board completion, needs-input,
error, reconnect and unread-reminder animations. Error wins over needs input,
which wins over completion, reconnect and reminder. Interrupted automatic
alerts wait in a coalesced queue. Muting those alerts never hides the six
per-key task states. User brightness and speed caps apply to both ChatGPT and
local effects, night mode adds a 35 percent ceiling, perimeter mode keeps
ambient motion on the outer twelve keys, and idle background can softly light
the ten non-Agent positions.

Done Repeats can run the chosen completion style from one through nine times,
or keep repeating until that Agent slot is opened. Pressing its Agent key or
seeing the slot leave Complete stops the repeating alert immediately. Older
saved records read the previously reserved byte as the one-repeat default.

Alert Layout controls how the board identifies the slot behind an alert.
Slot Focus grades brightness outward from the movable Agent key, Full Board
uses equal brightness everywhere, and Perimeter keeps the effect on the outer
ring while leaving the source Agent key visible. The source lookup follows
Mapper remaps instead of assuming that Agent slots stay in their default keys.

Ordinary Agent keys now have a consistent motion language. Idle uses a
calibrated warm white at `FFD09A`, Working breathes a saturated electric blue
at `005CFF`, Complete slowly breathes green, Needs Input uses
an amber heartbeat, Error uses repeating red pulses, and Empty stays dark.
Preview has a Status Demo that puts all six states on slots 1 through 6 at the
same time, matching the OLED Slot Key.

The extra preferences use a separate 16-byte CRC16 record at user-data offset
712. The Mapper's two 356-byte slots and 340-byte payload stay byte-for-byte
compatible. Invalid settings restore defaults.

## OLED dashboard and menu

The Codex layer now shows the USB link, six one-character task states, the
active alert, mute state and night mode. Number, Navigation and System keep
their existing icons.

- Hold the right encoder for one second to open the menu.
- Turn the right encoder to move and press it to open a section or run an action.
- Turn the large M encoder to change every value or toggle and press it to go back.
- Hold the right encoder for one second inside the menu to save and close.
- Matrix keys are swallowed while the menu is open.
- Save writes once. Cancel restores the opening values without a write.
- Reset Defaults needs a second press and still waits for Save before writing.

The OLED menu uses only the three rows and seventeen columns that remain clear
of the physical display shroud. Every screen starts with its item number and
the item being shown. Values use the second row, and the third row names the
effect of the setting. A two-page Slot Key explains every dashboard symbol.
The dashboard splits the six Agent states across two rows with visible colons,
so `1:I` reads as slot 1 Idle rather than `1I`. OLED writes only happen when
visible text changes.

The menu covers alert toggles and styles, reminder time, mute, full or
perimeter ambient layout, idle background, brightness, animation speed, night
mode, all six official effect previews, all four status previews, OLED
brightness and dashboard visibility.

## Build and tests

```sh
make doio/kb16/rev2:codex_micro_configurable_v1_3 EXTRAFLAGS="-Werror"
```

`tools/config_test.c` covers defaults, the cross-language binary/CRC golden
vector, semantic validation, A/B fallback, busy-input rejection and durable
reset. `tools/protocol_test.c` covers Channel 2 and 3 dispatch, staged transfer,
native events, joystick fallback, six-slot lighting, every official effect,
dual-zone composition, completion pulses and AG-light relocation.
It also verifies lighting read, preview, restore, commit, invalid payload and
busy-input behavior, including zero EEPROM writes during preview and restore.
`tools/settings_test.c` covers the isolated CRC16 record and Mapper-byte
separation. `tools/alerts_test.c` covers animation sampling, mute and priority.
`tools/menu_test.c` covers navigation, live drafts, preview, save, cancel and
reset confirmation.
`tools/validate_keymap.py` checks the static compatibility boundaries and
`tools/validate_binary.ps1` scans the final binary descriptors.

## Compatibility identity

- VID:PID `303A:8360`
- Raw HID usage `FF00:0001`
- Report ID `6`, 64-byte reports
- PWM lighting remains on DMA1 Channel 6
- Four layers and STM32duino Bootloader remain unchanged

No flashing is performed by the build or test process. Physical-device
Channel 2/3 concurrency remains a post-flash acceptance test.
