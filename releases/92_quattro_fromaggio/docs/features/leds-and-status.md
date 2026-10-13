# LEDs and status

Six LEDs on the Workshop Computer front panel show mount status, MIDI activity, gates, SETUP mode, and short **stroke digit** animations when certain values change.

Layout (as printed on the panel):

```text
1 2
3 4
5 6
```

LED indices in documentation are **LED0–LED5** top-left through bottom-right in that grid (LED0 = top-left “1”, LED5 = bottom-right “6”).

## PLAY mode

| LED | Meaning |
|-----|---------|
| LED0 | USB **host**: lit when a MIDI device is mounted. **Device**: blinks with web/editor link activity. **Uncalibrated CV**: slow blink overrides the above until EEPROM pitch cal is loaded |
| LED1 | MIDI activity (flickers on traffic) |
| LED2 | Gate for Voice A (Pulse Out 1) |
| LED3 | Gate for Voice B (Pulse Out 2) |
| LED4 | In device mode: association with web editor. Otherwise follows status pattern |
| LED5 | Brief flash when config is saved to flash from SETUP exit |

When both CV voices are idle and no USB activity occurs, the panel is mostly quiet apart from host mount on LED0.

## SETUP mode

Enter SETUP with hold **Z Down ~1 s**. All LEDs flash once on entry.

While SETUP is active:

- **LED4 and LED5** alternate steadily (heartbeat).
- **LED0–LED3** show the selected learn slot in **binary** (Main knob). Slot 4 → binary 0100 → LED2 on alone.

Gate LEDs may not track your performance usefully in SETUP; use the web SETUP monitor if you need clearer feedback while learning.

## Stroke glyphs

When the slot, MIDI channel, bend range, or engine patch changes (and in some SETUP transitions), all six LEDs draw the **decimal digit** for that value using a fixed stroke order, then return to normal status. Each digit animation finishes in under ~200 ms.

Examples:

- Digit **0**: all LEDs flash together.
- Digit **1**: sequence on LEDs 2, 4, 6 (middle-right, bottom-right, bottom-left in the 1–6 layout).
- Digit **8**: longer path through most LEDs.

This replaces a seven-segment display: you get a readable number without extra hardware.

## Entering / leaving SETUP (LED cues)

- **All flash once:** entered SETUP.
- **Alternating LED4/5:** still in SETUP.
- **Wipe / return to PLAY pattern:** saved exit (Up → Middle) or unsaved exit (hold Down again).

If LED4/5 keep alternating after you thought you left, you are still in SETUP. Hold Down ~1 s or complete the save gesture deliberately.

## Factory reset

Factory reset at boot (hold Z Down briefly at power-on) does not use a special LED sequence beyond normal boot. If reset worked, maps and envelope return to factory behaviour on the next note.

## Web editor link

In device mode with panel stream enabled, the front panel graphic in the browser mirrors switch and knob positions. LED states on the physical hardware still follow the rules above; the SVG panel does not replace LED2/3 gate indicators for CV.
