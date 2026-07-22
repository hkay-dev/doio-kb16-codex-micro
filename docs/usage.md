# DOIO KB16 Codex Micro setup guide

Supported hardware is the `DOIO KB16-01 Rev2 / APM32F103CBT6`. Don't flash this onto Rev1, an ATmega32U4 model, or hardware you haven't identified.

## 1. Download the files

Use the v1.4.0 firmware from this repo. If you want to edit the ordinary layers
from Windows, grab the v1.3.5 Mapper package from the original project's
[releases](https://github.com/hcyniubi/doio-kb16-codex-micro/releases).

- Firmware: `doio_kb16_rev2_codex_micro_oled_v1_4_0.bin`
- Mapper: `Doio.Kb16.Mapper-v1.3.5-preview-complete.zip`

Check their SHA-256 hashes:

```text
7321AEEF382AFF8B6DA4A9A38862EF3A818BA7ED5DA819A26ACADE7C2379F4EF  doio_kb16_rev2_codex_micro_oled_v1_4_0.bin
65FB2D298D27C5533A1EA400D0CAAB46465BFDB4D0B9FA7ABCB5E8CEB68DBB89  Doio.Kb16.Mapper-v1.3.5-preview-complete.zip
```

## 2. Flash the firmware

Back up the factory firmware before flashing if you can. A full SWD read of the entire flash is the preferred recoverable backup. If you hit readout protection, stop. Don't clear protection or do a mass erase.

This repo only ships a built QMK BIN. It doesn't include the factory firmware. Check all of these before flashing:

- The target is a `DOIO KB16-01 Rev2 / APM32F103CBT6`.
- The bootloader uses the Maple/STM32duino path.
- The BIN filename and SHA-256 match the values above.

Use a local flashing process that you've already tested. This project's Mapper won't flash the firmware for you.

## 3. Install ChatGPT Desktop

The Codex-layer status lights don't need a bridge, Work Louder Input, a proxy, or any background process.

The normal data paths are:

```text
ChatGPT Desktop -> Raw HID Channel 2 -> KB16 firmware -> Codex task/status lights
Mapper          -> Raw HID Channel 3 -> KB16 EEPROM configuration
```

On another Windows x64 computer, you only need to:

1. Install ChatGPT Desktop and sign in.
2. Connect a KB16 that already has the matching firmware.
3. Copy the complete Mapper ZIP or its fully extracted folder if you want to edit key bindings or ordinary-layer lighting.

You don't need the .NET runtime, Work Louder Input, a QMK toolchain, or a background service.

## 4. Run the Mapper

Extract `Doio.Kb16.Mapper-v1.3.5-preview-complete.zip` and keep all seven files together:

- `Doio.Kb16.Mapper.exe`
- `Doio.Kb16.Mapper.pdb`
- `D3DCompiler_47_cor3.dll`
- `PenImc_cor3.dll`
- `PresentationNative_cor3.dll`
- `vcruntime140_cor3.dll`
- `wpfgfx_cor3.dll`

Run `Doio.Kb16.Mapper.exe`. The app may not open if you copy only the EXE.

## 5. Edit key bindings

Click `Read Again` to read the device's current generation, CRC, and complete 340-byte configuration.

- Codex layer: the 16 native controls may only swap positions. None can be duplicated or omitted. The left encoder stays locked to the native Codex encoder.
- Number, Navigation, and System layers: keys can run keyboard, media, mouse, scroll, layer, RGB, OLED brightness, and protected bootloader actions.
- Changes stay in a local draft until you click `Apply to Device` and confirm.

The Mapper reads another device snapshot right before writing. It blocks the write if the generation, CRC, or configuration changed, which keeps it from overwriting another edit.

## 6. Edit lighting

The Number, Navigation, and System layers share one solid HSV setting. The
Windows Mapper edits that color.

Click the rainbow button to the left of `Read Again` to open the lighting panel:

- Moving Brightness, Hue, or Saturation previews the change right away but doesn't write EEPROM.
- `Save Lighting` stores the current HSV values and checks them by reading them back.
- `Cancel` or closing the popup restores the complete RGB Matrix state from when you opened the panel and doesn't write EEPROM.

ChatGPT Desktop still controls the Codex layer's background and six task-status lights through Channel 2.

The Codex layer also has an on-device menu. Hold the top-right **Right** encoder
for one second to open it. Turn Right to browse and press it to select. Turn the
large **M** encoder to change a value, and press M to go back. Hold Right again
to save and close.

That menu controls alerts, repeats, reminders, alert layout, ambient area, idle
glow, brightness, speed, night mode, OLED brightness, and the dashboard. It
also has live previews for every app effect, every alert class, and all six
task states.

## 7. Recovery and limits

The Mapper can ask the firmware to restore the v1.2 default keymap, but that writes the device configuration. Confirm that you have an exported JSON file or another known recovery path first.

The Mapper doesn't:

- Flash firmware
- Install a background service
- Start a bridge or Work Louder Input
- Enter the bootloader automatically
- Run macros, type text, delay action sequences, or launch programs

If another app has claimed the HID interface, close other configurators and read the device again.
