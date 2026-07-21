# On-device lighting and OLED plan

## Goal

Turn the KB16 into a self-contained Codex lighting controller that can be tuned from its OLED and encoders. Keep the six Agent LEDs independent, keep the official ChatGPT controls working, keep the Windows Mapper's 340-byte payload unchanged, and keep a known-good rollback image at every physical flash.

This plan covers the whole requested set.

- Green completion alerts
- Amber approval alerts
- Red error alerts
- Unread reminders
- Alert priority and queueing
- Alert mute
- Manual night mode
- Full-board and perimeter ambient layouts
- User brightness and speed controls
- Reconnect animation
- Standalone previews
- OLED task dashboard
- OLED settings menu
- Persistent preferences

## Baseline and rollback

The working lighting build remains the immediate rollback image.

| Artifact | SHA-256 |
|---|---|
| Factory application backup | `6d9e1b95a748ec477a01e722da3af847ad1f310daeda23c4977623771484b5cb` |
| Pre-lighting v1.3.5 application backup | `b31a4bb4a321e29510594f6caf29ccced2f0ac2292304be893c53b32c8920d6e` |
| Working lighting BIN | `9ef4fd8793a68b3f7593813e3954e1537b99518d41b489899893b826e6c476ad` |

The working BIN is 53,336 bytes in a 122,880-byte application region. The new code has plenty of flash room, but the build still gets a hard size check before flashing.

## Compatibility boundaries

These parts stay fixed.

- USB identity `303A:8360`
- Vendor Usage Page `FF00`
- Report ID `6`
- 64-byte reports
- Channel 2 ChatGPT JSON-RPC
- Channel 3 Mapper protocol version `1`
- 340-byte Mapper configuration payload
- Two 356-byte A/B configuration slots at offsets `0` and `356`
- Six movable Agent controls and their LED lookup
- Official left encoder behavior on the Codex layer
- Existing Number, Navigation, and System bindings
- Existing layer icons and guarded bootloader behavior

The on-device settings won't enter the 340-byte payload. That keeps the Windows Mapper's serializer, CRC, generation checks, and saved layouts compatible.

## Persistent settings storage

The earlier idea of using QMK's 32-bit user word doesn't work here. `EECONFIG_USER_DATA_SIZE` is nonzero, so QMK uses that word as the version marker for the whole user data block.

The settings will use a small record immediately after the existing 712 bytes.

| Offset | Length | Owner |
|---:|---:|---|
| `0` | `356` | Mapper configuration slot A |
| `356` | `356` | Mapper configuration slot B |
| `712` | `16` | Lighting and OLED preferences |

`EECONFIG_USER_DATA_SIZE` will move from `712` to `728`. The existing slot offsets and payload bytes won't move.

The packed settings record will have these fields.

| Field | Bytes | Meaning |
|---|---:|---|
| Magic | 2 | Rejects erased or unrelated data |
| Schema | 1 | Starts at version `1` |
| Flags | 1 | Alert toggles, mute, night mode, and perimeter mode |
| Brightness | 1 | User cap from 10 through 100 percent |
| Speed | 1 | Animation speed from 50 through 150 percent |
| Background | 1 | Idle background level from 0 through 30 percent |
| Reminder | 1 | Off, 15, 30, 60, or 120 seconds |
| Completion style | 1 | Single pulse, double pulse, or wave |
| Completion repeats | 1 | One through nine cycles, or until focused |
| Alert layout | 1 | Slot focus, full board, or perimeter |
| Approval style | 1 | Double pulse, heartbeat, or snake |
| Error style | 1 | Chase, pulse, or solid |
| OLED brightness | 1 | Screen level from 16 through 255 |
| CRC16 | 2 | Detects a partial or corrupt write |

Invalid settings fall back to safe defaults. Settings only write when the user saves or resets the menu, never for every encoder tick. That keeps wear low.

Default values will be 100 percent brightness, normal speed, no idle background, 30-second reminders, double green completion, double amber approval, red chase error, alerts enabled, soundless mute off, night mode off, and full-board ambient mode.

## Status model

Each of the six slots will keep a normalized local state.

| State | ChatGPT color or signal | OLED mark |
|---|---|---|
| Off | Effect off | `-` |
| Idle | White | `I` |
| Working | Blue | `W` |
| Complete or unread | Green | `C` |
| Needs input | Amber | `!` |
| Error | Red | `E` |
| Other | Any unrecognized color | `?` |

The firmware will compare the old and new normalized state for every `v.oai.thstatus` update. Alerts fire on a transition, not on repeated packets.

- Anything into Complete queues a completion alert.
- Anything into Needs input queues an approval alert.
- Anything into Error queues an error alert.
- The first snapshot after connecting populates state without replaying stale alerts.
- Pressing an Agent key acknowledges that slot's reminder.
- Leaving Complete clears that slot's reminder timer and acknowledgement state.

## Alert engine

The engine will keep one active alert and a coalesced pending bitmask. Repeated packets for the same event won't fill a queue.

Priority runs in this order.

1. Error
2. Needs input
3. Completion
4. Reconnect
5. Reminder
6. Manual preview

A higher-priority alert can interrupt a lower-priority one. The interrupted event goes back into the pending mask when it still matters. A lower-priority event waits. Error and approval alerts have short cooldowns so a noisy task can't make the board flash continuously.

Custom alert mute skips all automatic whole-board alerts. It doesn't hide the six per-key status colors, and it doesn't block a preview started from the menu.

### Completion

- Single pulse uses one 750 ms smooth green pulse.
- Double pulse uses two 750 ms pulses with a 200 ms gap.
- Wave moves green through the serpentine path and fades behind it.
- Off leaves only the assigned Agent key green.

### Needs input

- Double pulse uses two smooth amber pulses.
- Heartbeat uses a short pulse, a short gap, a second pulse, and a longer fade.
- Snake runs amber around the grid once.
- Off leaves only the Agent key amber.

### Error

- Chase runs a bright red head with a fading tail for two laps.
- Pulse uses three slower red pulses.
- Solid holds the full board red for 1.5 seconds and fades out.
- Off leaves only the Agent key red.

### Reminder

A completed slot starts its reminder timer. After the chosen delay, the board gives one gentle green shallow pulse and the OLED names the slot. The timer repeats at the same interval until that Agent key is pressed or the task leaves Complete.

### Reconnect

The first successful host RPC after boot runs a short cool-blue sweep. This proves ChatGPT has actually opened the vendor HID channel. It only runs once per connection cycle.

## RGB composition

Rendering keeps four layers of priority.

1. Active custom alert or preview
2. Per-thread Agent LED
3. ChatGPT key and ambient zones
4. Optional user idle background

The alert temporarily covers the whole matrix. Everything underneath keeps its state and reappears on the first frame after the alert.

Per-thread colors still win on their six assigned LEDs during ordinary operation. A selected or pulsing task can breathe without changing the other five task LEDs.

### Full-board ambient mode

The existing compositor stays in place. ChatGPT's ambient animation runs across all 16 LEDs. When the app also requests a solid key zone, the solid color becomes a dim base under the moving ambient effect.

### Perimeter ambient mode

Ambient effects only touch the 12 outside positions of the 4 by 4 grid. The four inner positions use the key zone or idle background. Agent colors still override either zone wherever those controls were moved by the Mapper.

### Brightness

ChatGPT's requested brightness gets multiplied by the user's cap. Night mode adds a second 35-percent cap. Custom alerts use the same cap, so an error can't unexpectedly jump to full brightness at night.

### Speed

ChatGPT's normalized speed gets multiplied by the user's 50 to 150 percent setting. A speed of zero stays stopped. Custom alert periods use the same multiplier.

### Idle background

When ChatGPT turns both global zones off, non-Agent keys can show a dim cool white background. The six Agent keys keep their own state, including off for an unassigned slot. A zero setting keeps today's behavior.

## OLED dashboard

The Codex layer will use a text dashboard in place of the static Codex icon. The other three layers keep their existing icons.

The 128 by 32 screen fits four 21-character rows.

```text
CODEX USB      AG3
1I 2W 3C 4! 5E 6-
ALERT READY
R-HOLD SETTINGS
```

The dashboard will show connection state, six slot marks, selected slot when it can be inferred from a breathing effect, active alert, mute state, night mode, and the settings gesture.

During an automatic alert it will show the event and slot.

```text
TASK COMPLETE
AG03
DOUBLE PULSE
R-PRESS MUTE
```

The OLED is monochrome, so it can't reproduce RGB colors. ChatGPT doesn't send task titles over this protocol, so the display can show slot and state but not the conversation name.

The OLED only redraws when data changes or at a limited animation rate. This keeps I2C traffic away from the RGB DMA path.

## Menu controls

The menu opens only from the Codex layer.

- Hold the right encoder for one second to open settings.
- Turn the right encoder to move the cursor.
- Press the right encoder to enter a page, toggle a setting, or start a preview.
- Turn the middle encoder to change the highlighted value.
- Press the middle encoder to go back.
- Hold the right encoder again to save and close.
- Matrix keys do nothing while the menu is open.
- The left encoder keeps its official behavior outside the menu and isn't needed inside it.

The right encoder's normal short press remains a layer cycle. It runs on release only when the one-second settings hold didn't fire. Existing right-turn and middle-turn bindings remain unchanged outside the menu.

## Menu tree

```text
LIGHTING
  ALERTS
    COMPLETE
      ENABLED
      STYLE
    APPROVAL
      ENABLED
      STYLE
    ERROR
      ENABLED
      STYLE
    REMINDER
    MUTE
  AMBIENT
    LAYOUT
    BACKGROUND
    BRIGHTNESS
    SPEED
    NIGHT MODE
  PREVIEW
    SOLID
    SNAKE
    RAINBOW
    BREATH
    GRADIENT
    SHALLOW BREATH
    COMPLETE
    APPROVAL
    ERROR
    RECONNECT
  DISPLAY
    OLED BRIGHTNESS
    DASHBOARD
  SAVE AND EXIT
  CANCEL
  RESET DEFAULTS
```

Edits live in a draft. Lighting values apply immediately for feedback. Save writes one settings record. Cancel restores the opening settings without writing. Reset asks for a second press, installs defaults, previews them, and only writes on Save and Exit.

## Standalone previews

Official effect previews use a neutral test color unless the effect has a fixed status color. They last until the cursor moves, the menu exits, or another preview starts.

- Solid uses cool white.
- Snake uses working blue.
- Rainbow uses the full hue cycle.
- Breath uses cool white.
- Gradient uses blue.
- Shallow breath uses cool white.
- Complete uses the selected completion style.
- Approval uses the selected approval style.
- Error uses the selected error style.
- Reconnect uses its cool-blue sweep.

Preview ignores alert mute but still respects brightness, speed, night mode, and ambient layout.

## Input safety

Opening the menu must never send a delayed right-encoder press to ChatGPT or the layer system. Closing it must release every locally held menu gesture. Matrix events are swallowed while the menu is active so an effect preview can't accidentally approve, reject, archive, or start a task.

Bootloader entry stays on its existing guarded System-layer action. The settings menu won't add another boot path.

## Test plan

### Settings storage

- Erased bytes load defaults.
- Bad magic loads defaults.
- Unknown schema loads defaults.
- Bad CRC loads defaults.
- Every enum and numeric field gets clamped.
- Save writes once.
- Cancel writes zero times.
- Existing 712 configuration bytes don't change.
- A Mapper commit doesn't change the settings record.
- Restore Mapper defaults doesn't change lighting preferences.

### Status transitions

- Initial snapshots don't alert.
- Working to Complete queues completion.
- Idle to Complete queues completion.
- Complete repeated packets don't queue again.
- Any state to Needs input queues approval.
- Any state to Error queues error.
- Leaving Complete cancels its reminder.
- Agent press acknowledges only its own reminder.

### Priority and queueing

- Error interrupts completion.
- Approval waits behind error.
- Completion waits behind approval.
- Repeated identical events coalesce.
- Muted automatic events don't start.
- Manual preview still starts while muted.
- The underlying per-key state returns after every alert.

### Rendering

- Every official numeric effect still renders.
- String effect compatibility still works.
- Full-board mode uses all 16 positions.
- Perimeter mode uses exactly 12 ambient positions.
- Agent LEDs override ambient and idle background.
- Night mode caps app lighting and alerts.
- Speed scaling changes moving and breathing periods.
- Idle background never fills an unassigned Agent LED.
- Alerts appear on all four layers.

### OLED and controls

- Right short press keeps its old action.
- Right hold opens the menu without its old action.
- Right turns move one item per detent.
- Middle turns change and clamp values.
- Middle press backs out one page.
- Matrix keys are swallowed in the menu.
- Save persists.
- Cancel restores the opening draft.
- Reset needs confirmation.
- OLED lines never exceed 21 characters.
- Leaving the menu restores the dashboard or layer icon.

### Build and binary

- Protocol host tests pass with `-Werror`.
- Configuration host tests pass with `-Werror`.
- New settings and menu tests pass with `-Werror`.
- Static compatibility validation passes.
- Full QMK ARM build passes with `EXTRAFLAGS=-Werror`.
- The BIN stays below 122,880 bytes.
- Every expected USB descriptor occurs exactly once.

## Physical acceptance

The live test will go in this order.

1. Back up the currently flashed application.
2. Flash the validated BIN through Maple alt setting 2.
3. Confirm `303A:8360` and Usage Page `FF00` return.
4. Confirm ChatGPT answers `device.status`, `v.oai.rgbcfg`, and `v.oai.thstatus`.
5. Open and close the menu with the right encoder.
6. Preview every official effect.
7. Preview completion, approval, error, and reconnect.
8. Save brightness, speed, background, and perimeter settings.
9. Reboot and confirm those settings return.
10. Finish a real task and check completion plus the reminder.
11. Select the completed task and check reminder acknowledgement.
12. Trigger a real approval state.
13. Trigger a safe real error state if one is naturally available.
14. Hold push-to-talk and check recording and processing ambient effects.
15. Switch layers during an alert and confirm the full-board overlay survives.

Any failure in discovery, USB identity, OLED startup, normal key input, or RGB output stops the test and restores the working lighting BIN.
