# C1ZZL3 RAD Card Guide

C1ZZL3 is a phase-distortion synth card for the Music Thing Modular Workshop
Computer. It combines a playable digital synth voice, a Turing machine, USB
MIDI, and a browser editor for custom amplitude, phase-distortion, and pitch
envelopes.

This guide covers RAD v9. Flash [C1ZZL3_RAD.uf2](../uf2/C1ZZL3_RAD.uf2).
RAD retains Core 1.4's panel and Turing controls and adds independent Amp1/Amp2,
PD1/PD2, and Pitch1/Pitch2 envelopes, separate oscillator waveform settings,
and eight named sound-preset slots with saved performance settings.

The base PD amount remains shared between oscillators; the PD envelopes are
independent. CV In 1 modulates the shared PD amount and CV In 2 modulates both
waveform controls. RAD has no Gnarly recipe banks or oscillator 2 panel page.

## What It Does

- Plays as a phase-distortion synth voice.
- Responds to USB MIDI notes from a DAW or class-compliant controller.
- Lets you design and save custom amplitude, phase-distortion, and pitch
  envelopes from a browser.
- Runs a Turing machine mode with stepped CV, smoothed CV, two pulse outputs,
  and optional MIDI note output.
- Keeps factory envelope presets safe; custom envelopes use separate slots.

## Main Synth Mode

Put the switch in the middle.

- Main sets pitch.
- X sets phase distortion.
- Y selects or morphs oscillator 1's waveform; set oscillator 2's waveform
  separately in the browser editor.
- `Audio/CV In 1` adds pitch at 1V/oct.
- `CV In 1` adds phase distortion.
- `CV In 2` adds wave control.
- `Pulse In 2` triggers the selected envelope, loops supported envelopes while
  the gate is high, exits the loop when the gate falls, and syncs the
  oscillators when the envelope starts from inactive.

The Turing CV and pulse outputs continue running in synth mode, so they can be
used while playing the synth.

## Performance Edit

Hold the switch down from the middle position.

- Main sets oscillator 2 detune.
- X sets ring modulation.
- Y sets noise/grit.

Ring and noise default to neutral; saved performance settings restore at startup.
If the card powers up with the switch already down, ring/noise editing waits
until the switch has first left down.

Hold the switch down from the middle position to save the current performance
settings.

## Turing Mode

Put the switch up.

- Main controls mutation.
- X controls sequence length.
- Y controls clock speed.
- `Pulse In 1` can clock the Turing machine externally.
- `CV Out 1` outputs stepped Turing CV.
- `CV Out 2` outputs smoothed Turing CV.
- `Pulse Out 1` and `Pulse Out 2` output Turing pulses.
- Audio outputs carry the self-playing Turing oscillator voice.

Tap tempo has been removed. Y is the internal Turing clock-speed control.
Turing MIDI output is optional and defaults off.

## USB MIDI

The card chooses its USB role at boot.

- Connected to a computer, it appears as a USB MIDI device for DAW and browser
  use.
- Connected to a class-compliant USB MIDI controller, it can run in USB MIDI
  host mode.

MIDI CC controls on the selected input channel:

- `CC1`: phase distortion
- `CC20`: oscillator 2 detune
- `CC21`: ring modulation
- `CC22`: noise amount
- `CC23`: oscillator 1 waveform
- `CC24`: Turing CV octave range, from 1 to 8 octaves

Physical knobs use pickup after MIDI changes, so values do not jump until the
knob is swept through the current setting.

MIDI note-on behaves like a held gate. It triggers the selected envelope and
keeps loop-capable envelopes cycling until MIDI note-off lets the envelope
complete naturally.

## Web MIDI Editor

Hosted editor:

```text
https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/index.html
```

Use Chrome or another browser with Web MIDI and SysEx support. Press `MIDI`,
choose the C1ZZL3 output, then:

1. Pick a preset on the left, or add a custom one.
2. Choose `Amp 1 / Osc 1`, `Amp 2 / Osc 2`, `PD 1 / Osc 1`, or
   `PD 2 / Osc 2` to edit the corresponding main graph lane.
3. Use `Pitch 1 / Osc 1` and `Pitch 2 / Osc 2` below the main graph
   to edit each oscillator's pitch envelope.
4. Drag points on the graphs to change both level and timing.
5. Watch the point numbers. When stages stack, only the highest number is shown.
6. Use the tables below the graphs for exact values when you want precise edits.
7. Use the action buttons when you want to send, save, read, or reset.

Button quick reference:

- `Load RAM` to send a custom envelope until reset.
- `Load Sound Preset` to temporarily send the selected envelope and all
  current settings together.
- `Save Envelope Only` to write the six envelope lanes and slot name while
  keeping the slot's saved performance settings.
- `Save Sound Preset` to save the six lanes, name, and current performance
  settings together.
- `Delete Envelope Slot` to clear a custom envelope slot.
- `Read Envelopes from Card` to load saved card envelopes into the editor.
- `Read Settings from Card` to load the current card settings into the editor.
- `Send Settings` to send performance settings until reset.
- `Reset Preset` to restore the selected preset to its factory value.

The card can save up to eight named custom sound presets. Factory presets are
not overwritten. Temporary changes are lost at restart. Use `Save Envelope Only`
for lane edits or `Save Sound Preset` for lanes, name, and performance settings.
Selecting a saved sound preset restores its settings. Hold the hardware switch
down from the middle position to save the current performance settings as the
global startup baseline.

## C1ZZL3 Import Lab

Hosted import lab:

```text
https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/import/index.html
```

Use Import Lab to decode Casio CZ `.syx` patches into pitch-aware C1ZZL3 drafts,
then open the result in Envelope Lab for final editing and sending.

## Envelope Presets

Factory presets:

0. Off
1. Pluck
2. Double pluck
3. Bounce
4. Bell
5. Brass
6. Strings
7. Reverse swell
8. Evolving digital

Saved custom envelopes appear after the factory presets during startup envelope
selection. Custom slots light LED 6 and use LEDs 1-3 for the slot number.

## LED Feedback

In synth mode:

- LED 1 shows phase-distortion amount.
- LED 2 shows waveform position.
- LED 3 shows oscillator 2 level.
- LED 4 shows ring modulation amount.
- LED 5 shows noise amount.
- LED 6 lights while the switch is held down.

In Turing mode:

- LEDs 1-3 show low bits of the Turing pattern.
- LED 4 follows Pulse In 1.
- LED 5 shows sequence length.
- LED 6 flashes on each Turing clock step.
- Turning X briefly displays sequence length as binary on the LEDs.

In startup envelope selection:

- Factory presets use the binary LED display.
- Custom slots light LED 6 and use LEDs 1-3 for the custom slot number.

## Build UF2

From this firmware source folder:

```sh
cmake -S . -B build
cmake --build build
```

The built firmware will be at:

```text
build/C1ZZL3_FULL_DUAL_OSCILLATORS.uf2
```
