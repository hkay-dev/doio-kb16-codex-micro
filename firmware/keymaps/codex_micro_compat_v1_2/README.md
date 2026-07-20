# DOIO KB16 Rev2 Codex Micro compatibility v1.2

Independent successor to the verified `codex_micro_compat_v1_1` build. It
preserves the same Codex Micro USB/Raw HID identity, four layers, OLED,
Bootmagic, bootloader and host-controlled lighting while mapping all sixteen
Codex-layer keys and the physical left encoder to native Codex Micro events.

## Native controls

The Codex layer uses this physical 4x4 layout:

| | Column 1 | Column 2 | Column 3 | Column 4 |
|---|---|---|---|---|
| Row 1 | `AG00` | `AG01` | joystick up | joystick right |
| Row 2 | `AG02` | `AG03` | joystick left | joystick down |
| Row 3 | `AG04` | `AG05` | `ACT06` | `ACT07` |
| Row 4 | `ACT08` | `ACT09` | `ACT12` | Mic (`ACT10`) |

- The Mic key emits `ACT10`; ChatGPT Desktop groups it into the double-width
  `ACT10_ACT11` configuration slot. The firmware never emits `ACT11`.
- Discrete joystick keys emit `v.oai.rad` at the four cardinal angles. The
  last key pressed wins; releasing it restores the most recently held
  direction, and releasing the final direction sends a neutral position.
- The physical left encoder emits `ENC_CC`, `ENC_CW`, and `ENC_SW` on every
  layer. ChatGPT Desktop owns short-press and long-press interpretation.
- The physical right and middle encoders, their presses, and all non-Codex
  layer key positions retain the v1.1 mappings.

## Lighting behavior

- All six physical Agent keys send `AG00` through `AG05` and display their
  corresponding host thread status on LED positions `0, 1, 4, 5, 8, 9`.
- Uses the maximum active slot brightness as the global brightness.
- Treats a complete six-slot `brightness: 0` / `effect: off` snapshot as the
  desktop inactivity-off command. Partial off updates never blank the board.
- Restores lighting when any later host snapshot contains a non-zero slot.
- Uses no local inactivity timer; ChatGPT Desktop remains authoritative.
- On the Codex layer, the ten non-status keys use normalized cool white
  `RGB 215/211/255`, scaled by the host brightness.
- A task status whose host color is pure white receives the same cool-white
  correction, so it visually matches the background through warm switch
  housings. Blue, amber, green, red and breathing effects remain host-owned.
- On the other three layers, existing RGB effects remain, but their global
  value follows the host brightness. Inactivity-off overrides all four layers
  to black.
- `v.oai.rgbcfg`, `lights.preview` and `host.focused_app` remain acknowledged.
  The normally-off `keys` and `ambient` fields are deliberately not used as
  the global brightness source.

## Compatibility identity

- USB VID:PID: `303A:8360`
- Manufacturer/product: `Work Louder` / `Codex Micro`
- USB release: `0x0100`
- Vendor HID usage page/application: `0xFF00` / `0x01`
- Report ID: `6`
- HID report: 64 bytes total; RPC payload fragments up to 61 bytes

These identifiers and the vendor protocol are undocumented and may change in
a future ChatGPT Desktop release.

## Build and validation

```sh
qmk compile -kb doio/kb16/rev2 -km codex_micro_compat_v1_2
```

```powershell
& keyboards\doio\kb16\keymaps\codex_micro_compat_v1_2\tools\validate_binary.ps1 `
  -Binary doio_kb16_rev2_codex_micro_compat_v1_2.bin
```

The native host test compiles `tools/protocol_test.c` together with
`codex_micro_protocol.c` using `CODEX_MICRO_HOST_TEST` and warnings as errors.
It covers 20/50/80/100 percent brightness, common status colors, breathing,
white matching, complete inactivity-off across four layers, partial updates,
slot-5-only wake, six non-contiguous task LEDs, all native ACT events, Mic
press/release, encoder events, joystick overlap/fallback, config
acknowledgement, empty slots and six-key bounds.

Do not overwrite the verified v1.1 source or binary. Do not flash this build
until its host tests, QMK compile and binary descriptor validation have all
passed and the user separately confirms flashing.

## Verified build

Built on 2026-07-19 with QMK MSYS / GCC 15.2.0:

- Output: `doio_kb16_rev2_codex_micro_compat_v1_2.bin`
- File size: 45,144 bytes
- QMK payload: 45,128 bytes
- SHA-256: `742DC859C454951A0FB6D3B54FC9B7DC6D9E512BD9E01E466498BA9D8323A3E9`
- ELF sections: text 43,908 bytes, data 1,220 bytes, bss 19,256 bytes
- Host simulation, keymap validation, QMK compile and binary descriptor
  validation: passed

The device has not been flashed by the build process.
