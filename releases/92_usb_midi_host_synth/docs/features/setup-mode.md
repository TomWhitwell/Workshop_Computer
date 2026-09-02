# SETUP mode

SETUP is the card’s on-panel **MIDI learn** mode. You assign a MIDI CC, note, or hardware knob to each parameter slot without a laptop. The same slot table is editable from the web editor Maps tab; both paths write to the same flash block.

## Entering and leaving

**Enter:** from PLAY, hold **Z Down** about one second. All six LEDs flash once. LED4 and LED5 then alternate slowly. You remain in SETUP after you release Down; it is a toggle, not a hold-to-stay mode.

**Save and exit:** move Z to **Up**, then to **Middle**. Maps and extended config write to flash. LEDs wipe and you return to PLAY.

**Exit without saving:** hold **Z Down** ~1 s again. Same wipe, but changes since you entered SETUP are discarded.

Releasing Down after entering often passes through Middle on the way back up. That Middle edge is **ignored** so you do not accidentally save when you only meant to adjust the switch.

## What Main does in SETUP

**Main** selects the learn slot **0–12**. LED0–LED3 show the slot number in binary (slot 5 = LEDs 0 and 2 on, for example). When the slot changes, the panel draws the slot digit on all six LEDs as a quick **stroke glyph** animation.

In PLAY, Main has no function.

## Learning a control

1. Turn Main to the slot you want (see table below).
2. Perform the source you want mapped:
   - Move a **MIDI CC** on your controller.
   - Press a **note** (for note-on learn where supported).
   - Turn **X** or **Y** to map the hardware knob to the slot.
3. The card stores channel, CC number (or note), and source type.
4. Repeat for other slots.
5. Save with Z Up → Middle, or abandon with hold Down.

If you use a laptop, the web editor **SETUP monitor** section mirrors slot selection and shows the last MIDI message heard while in SETUP. Enable **MIDI relay** so messages from a keyboard on the PC reach the card.

## Slot reference

| Slot | Parameter | Typical use |
|------|-----------|-------------|
| 0 | Reserved | — |
| 1 | Voice A MIDI channel | Default ch 1 |
| 2 | Voice B MIDI channel | Default ch 2 |
| 3 | Pitch bend range (1–12 semitones) | Default ±2 |
| 4 | Audio engine / voice matrix | Factory **CC 24** Omni, values 0–120 |
| 5 | Reserved | Unused |
| 6 | Reserved | Unused |
| 7 | Attack | Factory **knob X** |
| 8 | Decay | Learn a CC |
| 9 | Sustain | Learn a CC |
| 10 | Release | Factory **knob Y** |
| 11 | Cutoff | Learn a CC |
| 12 | PWM | Learn a CC |

Slots 5 and 6 were used in older firmware for features that were removed in 0.10.0. Leave them unmapped.

## LEDs in SETUP

- **LED4 / LED5:** alternate blink — you are in SETUP.
- **LED0–3:** binary slot index.
- **Stroke glyphs:** brief digit animation when slot, channel, bend range, or engine changes (in PLAY or SETUP).

Gate indicators on LED2/3 are less meaningful in SETUP; prefer the SETUP monitor in the web editor if you are debugging learn.

## Factory reset vs SETUP save

| Action | When | Result |
|--------|------|--------|
| Z Up → Middle in SETUP | While running | Writes current learn table + engine defaults |
| Hold Z Down at power-on | First ~0.1 s of boot | Full factory reset (channels, maps, envelope) |
| Hold Z Down ~1 s in SETUP | While running | Exit SETUP without saving |

Factory reset at boot does not require entering SETUP first.

## Tips

Learn **Omni** channel for CCs that should work regardless of transmit channel (factory engine map uses Omni).

If a slot already has a mapping, sending a new learn overwrites it.

For decay and sustain with no factory knob, assign a slider on your controller once and save; otherwise they stay at boot defaults until you change them in the Live tab.
