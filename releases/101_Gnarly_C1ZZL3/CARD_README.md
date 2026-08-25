# C1ZZL3 Gnarly Card Guide

C1ZZL3 Gnarly is a dual-oscillator phase-distortion synth for the Music Thing
Modular Workshop Computer. It is a no-Turing, performance-focused C1ZZL3
version, with two oscillator lanes, recipe wave banks, named sound presets, and
CZ patch import support.

## What It Does

- Plays two phase-distortion oscillator lanes.
- Gives each lane its own amplitude, phase-distortion, and pitch envelope.
- Saves named sound presets from the browser editor.
- Uses four recipe banks for simple, compound, resonant, and CZ-style sounds.
- Responds to USB MIDI notes and MIDI CC performance controls.
- Removes Turing CV, pulse, and generated MIDI output so the panel can focus on
  synthesiser control.

## Oscillator 1 Page

Put the switch in the middle.

- Main sets shared pitch.
- X sets oscillator 1 phase distortion.
- Y selects oscillator 1 recipe slot.
- `Audio/CV In 1` adds pitch at 1V/oct.
- `CV In 1` adds phase distortion.
- `CV In 2` adds recipe-slot/wave modulation.
- `Pulse In 2` triggers the selected envelopes. Held gates keep loop-capable
  envelopes cycling; gate-off lets the envelopes complete naturally.

## Oscillator 2 Page

Put the switch up.

- Main sets oscillator 2 interval/spread, centred at unison.
- X sets oscillator 2 phase distortion.
- Y selects oscillator 2 recipe slot.

## Performance And Bank Page

Hold the switch down.

- Main selects recipe bank.
- X sets ring modulation.
- Y sets noise/grit.

Hold the switch down from the middle position to save the current performance
settings.

## Recipe Banks

- Bank 1: simple single-wave families.
- Bank 2: warmer compound pairings with double-sine support.
- Bank 3: brighter resonant/windowed pairings.
- Bank 4: odd/import-faithful CZ-style pairings for translated patches.

Recipe slots by bank:

- Bank 1:
  Slot 1 `Saw`
  Slot 2 `Square`
  Slot 3 `Narrow pulse`
  Slot 4 `Double sine`
  Slot 5 `Saw pulse`
  Slot 6 `Resonant saw window`
  Slot 7 `Resonant triangle window`
  Slot 8 `Resonant trapezoid window`
- Bank 2:
  Slot 1 `Double sine + Saw`
  Slot 2 `Double sine + Square`
  Slot 3 `Saw + Double sine`
  Slot 4 `Square + Double sine`
  Slot 5 `Narrow pulse + Double sine`
  Slot 6 `Saw pulse + Double sine`
  Slot 7 `Resonant triangle window + Double sine`
  Slot 8 `Resonant trapezoid window + Double sine`
- Bank 3:
  Slot 1 `Resonant saw window + Saw`
  Slot 2 `Resonant triangle window + Square`
  Slot 3 `Resonant trapezoid window + Narrow pulse`
  Slot 4 `Resonant saw window + Saw pulse`
  Slot 5 `Resonant triangle window + Saw pulse`
  Slot 6 `Resonant trapezoid window + Saw pulse`
  Slot 7 `Resonant saw window + Narrow pulse`
  Slot 8 `Resonant trapezoid window + Square`
- Bank 4:
  Slot 1 `Saw pulse + Double sine`
  Slot 2 `Resonant triangle window + Double sine`
  Slot 3 `Saw pulse + Narrow pulse`
  Slot 4 `Resonant trapezoid window + Double sine`
  Slot 5 `Resonant saw window + Saw pulse`
  Slot 6 `Narrow pulse + Resonant trapezoid window`
  Slot 7 `Square + Resonant saw window`
  Slot 8 `Resonant triangle window + Narrow pulse`

These are weighted blends rather than strict 50/50 mixes, so each recipe keeps
more of the character of its first waveform.

On the switch-down page, LEDs 1 and 2 show the bank:

- Bank 1: LEDs 1 and 2 off.
- Bank 2: LED 1 on.
- Bank 3: LED 2 on.
- Bank 4: LEDs 1 and 2 on.

LED 3 shows oscillator 2 interval/spread as a bipolar brightness from centre.
LEDs 4 and 5 show ring modulation and noise/grit. LED 6 shows the active edit
page at off, medium, or full brightness.

## USB MIDI

MIDI note-on behaves like a held gate. It triggers the selected envelopes and
keeps loop-capable envelopes cycling until MIDI note-off lets them complete
naturally.

MIDI CC controls:

- `CC1`: oscillator 1 phase distortion, useful from a mod wheel.
- `CC20`: oscillator 1 recipe slot.
- `CC21`: oscillator 2 recipe slot.
- `CC22`: ring modulation amount.
- `CC23`: recipe bank.
- `CC24`: oscillator 2 interval/spread.
- `CC25`: oscillator 2 phase distortion.
- `CC26`: noise/grit amount.
- `CC27`: oscillator 1 phase distortion, for eight-knob controllers.

## Web MIDI Editor

Hosted editor path after Workshop deployment:

```text
https://computer.musicthing.co.uk/programs/101-gnarly-c1zzl3/web/index.html
```

The editor can load to RAM, save envelope-only changes, save full sound presets,
read saved card slots, read settings, send settings, and decode CZ `.syx`
patches through the matching Import Lab.

Use Chrome or another browser with Web MIDI and SysEx support.
