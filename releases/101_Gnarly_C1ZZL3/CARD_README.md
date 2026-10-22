# C1ZZL3 Gnarly Card Guide

C1ZZL3 Gnarly is a dual-oscillator phase-distortion synth for the Music Thing
Modular Workshop Computer. It is a no-Turing, performance-focused C1ZZL3
version, with two oscillator lanes, recipe wave banks, named sound presets, and
CZ patch import support. An optional 8mu v2 firmware adds hands-on performance
and envelope editing while retaining the same sound engine and Web MIDI editor.

## What It Does

- Plays two phase-distortion oscillator lanes.
- Gives each lane its own amplitude, phase-distortion, and pitch envelope.
- Saves named sound presets from the browser editor.
- Uses four recipe banks for simple, compound, resonant, and CZ-style sounds.
- Responds to USB MIDI notes and MIDI CC performance controls.
- Offers alternative firmware for 8mu v2 performance and custom-envelope editing.
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

| Bank | Sound character |
|---|---|
| 1 | Simple single-wave families. |
| 2 | Warmer compound pairings with double-sine support. |
| 3 | Brighter resonant/windowed pairings. |
| 4 | Odd/import-faithful CZ-style pairings for translated patches. |

Each oscillator recipe slot selects one entry from the current bank. Bank 1
contains the eight plain C1ZZL3 wave families; Banks 2–4 use fixed compound recipes.

| Slot | Bank 1: simple waves | Bank 2: warm compounds | Bank 3: resonant compounds | Bank 4: CZ-style pairings |
|---|---|---|---|---|
| 1 | `Saw` | `Double sine + Saw` | `Resonant saw window + Saw` | `Saw pulse + Double sine` |
| 2 | `Square` | `Double sine + Square` | `Resonant triangle window + Square` | `Resonant triangle window + Double sine` |
| 3 | `Narrow pulse` | `Saw + Double sine` | `Resonant trapezoid window + Narrow pulse` | `Saw pulse + Narrow pulse` |
| 4 | `Double sine` | `Square + Double sine` | `Resonant saw window + Saw pulse` | `Resonant trapezoid window + Double sine` |
| 5 | `Saw pulse` | `Narrow pulse + Double sine` | `Resonant triangle window + Saw pulse` | `Resonant saw window + Saw pulse` |
| 6 | `Resonant saw window` | `Saw pulse + Double sine` | `Resonant trapezoid window + Saw pulse` | `Narrow pulse + Resonant trapezoid window` |
| 7 | `Resonant triangle window` | `Resonant triangle window + Double sine` | `Resonant saw window + Narrow pulse` | `Square + Resonant saw window` |
| 8 | `Resonant trapezoid window` | `Resonant trapezoid window + Double sine` | `Resonant trapezoid window + Square` | `Resonant triangle window + Narrow pulse` |

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

## Optional 8mu v2 Firmware

Flash [Gnarly_C1ZZL3_8mu.uf2](uf2/Gnarly_C1ZZL3_8mu.uf2) for the Music Thing
8mu v2 option. This is alternative firmware; it retains the Gnarly sound engine
and Web MIDI editor and adds direct performance and envelope control from 8mu.

Import the eight bundled [8mu bank files](8mu_banks/README.md) one at a time with
the [16n Faderbank editor](https://16n-faderbank.github.io/editor/), selecting the
destination bank on the 8mu before each import. These controller banks are
separate from the four synthesiser recipe banks described above.

- **Bank 1:** regular Gnarly performance parameters. Buttons A–D send unused
  CCs 124–127 and do not play notes.
- **Banks 2–7:** faders 1–8 edit envelope stages 1–8 for Amp1, Amp2, PD1, PD2,
  Pitch1 and Pitch2 respectively. Button A selects level editing, B selects time
  editing, C saves the current custom envelope and D reverts the current lane.
- **Bank 8:** fader 1 selects the envelope/custom slot; faders 2–7 set Amp1, PD1,
  Pitch1, Pitch2, PD2 and Amp2 depths; fader 8 sets master depth.

Only saved custom sound-preset slots are editable. Select a saved custom slot
with bank 8 before editing a lane; factory envelopes remain read-only. Gesture
mappings are intentionally disabled. See the [main README](README.md#if-you-have-an-8mu)
and [bank guide](8mu_banks/README.md) for the matching firmware and setup details.

## Web MIDI Editor

Hosted editor path after Workshop deployment:

```text
https://computer.musicthing.co.uk/programs/101-gnarly-c1zzl3/web/index.html
```

The editor can load to RAM, save envelope-only changes, save full sound presets,
read saved card slots, read settings, send settings, and decode CZ `.syx`
patches through the matching Import Lab.

Use Chrome or another browser with Web MIDI and SysEx support.
