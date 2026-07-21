# DOIO KB16 Codex Mapper v1.3

Portable Windows x64 WPF editor for firmware `codex_micro_configurable_v1_3`.
Run `portable-win-x64\Doio.Kb16.Mapper.exe`; no installation, service,
auto-start entry or Input integration is used.

The editor reads the device before editing, presents four fixed layer tabs, a
4x4 physical key grid and three encoder cards, and writes only through binary
Channel 3. Codex native controls are swapped so duplicates cannot be created;
the Codex left encoder is visibly locked. Ordinary layers provide categorized
search, key/modifier entry, media, mouse, layer, RGB/OLED and protected
Bootloader actions.

Changes remain a local draft until **应用到设备** is confirmed. A write is
followed by a full readback and generation/CRC comparison. Pending edits can be
reverted; configurations can be imported/exported as JSON; restore-defaults
requests the firmware's exact v1.2 default. The editor never flashes firmware
or enters the Bootloader automatically.

Build and test:

```powershell
dotnet run --project Doio.Kb16.Mapper.Tests\Doio.Kb16.Mapper.Tests.csproj -c Release
dotnet publish Doio.Kb16.Mapper\Doio.Kb16.Mapper.csproj -c Release -r win-x64 --self-contained true -o portable-win-x64
```

If Input or another configurator owns the HID interface, connection fails with
an explicit instruction to close the conflicting configurator. The editor
requires v1.3 firmware; it cannot configure an unflashed v1.2 device.

## 2026-07-19 connection fix

The first portable build incorrectly treated the 16-byte Hello capability
response as a one-byte acknowledgement and reported `设备确认包格式无效`. The
current build parses and validates all capability fields. A live v1.3 device
Hello and full 340-byte configuration read passed after this correction; the
firmware does not need to be reflashed for this editor-only update.

## 2026-07-19 action display fix

Mapper `1.3.2.0` resets the action category to `全部` and clears action search
whenever a different physical key or encoder action is selected. The editor
then selects the action already stored in the draft. This prevents a mapping
outside the previous filter from appearing blank; it does not change the draft
or write the device.
