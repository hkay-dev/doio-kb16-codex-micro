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
Codex layer continues to use its host-driven background and six task lights.

## Build and tests

```sh
make doio/kb16/rev2:codex_micro_configurable_v1_3 EXTRAFLAGS="-Werror"
```

`tools/config_test.c` covers defaults, the cross-language binary/CRC golden
vector, semantic validation, A/B fallback, busy-input rejection and durable
reset. `tools/protocol_test.c` covers Channel 2 and 3 dispatch, staged transfer,
native events, joystick fallback, six-slot lighting and AG-light relocation.
It also verifies lighting read, preview, restore, commit, invalid payload and
busy-input behavior, including zero EEPROM writes during preview and restore.
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
