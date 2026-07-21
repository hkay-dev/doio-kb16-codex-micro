# v1.3.5 Release Notes

## Assets

- Firmware:
  `doio_kb16_rev2_codex_micro_configurable_v1_3_5_status_colors.bin`
  - Size: 49,128 bytes
  - SHA-256: `B21691424B22950562247E8B36D6307A0E0CCA481221C22592D9EE8ADBA45D0F`
- Mapper:
  `Doio.Kb16.Mapper-v1.3.5-preview-complete.zip`
  - Size: 62,530,556 bytes
  - SHA-256: `65FB2D298D27C5533A1EA400D0CAAB46465BFDB4D0B9FA7ABCB5E8CEB68DBB89`
  - Main EXE SHA-256: `91133EB3B701EA419B3791CC8418D405CA6168453A368A7AF055BEE0032F0EB5`

## Highlights

- Adds shared solid HSV lighting for Number, Navigation and System layers.
- Adds custom Codex task/status-light color configuration while preserving the Codex layer host-controlled background and six task lights.
- Keeps USB identity `303A:8360`, Raw HID usage `FF00:0001`, Report ID `6` and 64-byte reports.
- Keeps Channel 3 protocol version `1`, the 340-byte key configuration, schema, Generation/CRC and JSON draft behavior.
- Mapper previews ordinary-layer lighting without EEPROM writes, saves with readback verification and restores the opening RGB Matrix state on cancel/close.

## Compatibility

- Target hardware: `DOIO KB16-01 Rev2 / APM32F103CBT6`
- Firmware keymap: `codex_micro_configurable_v1_3`
- Mapper: Windows x64 portable WPF package, version `1.3.5.0`

The complete Mapper ZIP must be extracted and kept together. Do not copy only `Doio.Kb16.Mapper.exe`.

## Safety Notes

This is not factory firmware and is not affiliated with OpenAI, DOIO or QMK. Back up an untouched same-revision keyboard before flashing when possible. If readout protection is encountered during backup, stop; do not unprotect or mass-erase the device.

Mapper does not flash firmware, install services, run a Bridge or enter Bootloader automatically. It only reads and writes the keyboard configuration over Raw HID Channel 3.

## Validation

- Mapper Release tests passed.
- Current Mapper seven-file portable folder was re-zipped for this release.
- Firmware binary hash was measured from `firmware/releases/v1.3.5/`.
- No device write, reset, Bootloader operation or flash was performed while preparing this GitHub release.
