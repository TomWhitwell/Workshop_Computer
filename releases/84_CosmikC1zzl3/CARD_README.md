# C1ZZL3 Card Guide

C1ZZL3 is a phase-distortion synth card for the Music Thing Modular Workshop
Computer. It combines a playable digital synth voice, a Turing machine, USB
MIDI, and a browser editor for custom envelopes.

## What It Does

- Plays as a phase-distortion synth voice.
- Responds to USB MIDI notes from a DAW or class-compliant controller.
- Lets you design and save custom envelopes from a browser.
- Runs a Turing machine mode with stepped CV, smoothed CV, two pulse outputs,
  and optional MIDI note output.
- Keeps factory envelope presets safe; custom envelopes use separate slots.

## Main Synth Mode

Put the switch in the middle.

- Main sets pitch.
- X sets phase distortion.
- Y selects or morphs the waveform.
- `Audio/CV In 1` adds pitch at 1V/oct.
- `CV In 1` adds phase distortion.
- `CV In 2` adds wave control.
- `Pulse In 2` triggers the selected envelope, loops supported envelopes while
  the gate is high, exits the loop when the gate falls, and syncs the
  oscillators.

The Turing CV and pulse outputs continue running in synth mode, so they can be
used while playing the synth.

## Performance Edit

Hold the switch down from the middle position.

- Main sets oscillator 2 detune.
- X sets ring modulation.
- Y sets noise/grit.

Ring and noise start neutral after reset. If the card powers up with the switch
already down, ring/noise editing waits until the switch has first left down.

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
- `CC23`: waveform
- `CC24`: Turing CV octave range, from 1 to 8 octaves

Physical knobs use pickup after MIDI changes, so values do not jump until the
knob is swept through the current setting.

MIDI note-on behaves like a held gate. It triggers the selected envelope and
keeps loop-capable envelopes cycling until MIDI note-off lets the envelope
complete naturally.

## Envelope Lab

Hosted editor:

```text
https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/index.html
```

Use Chrome or another browser with Web MIDI and SysEx support. Press `MIDI`,
choose the C1ZZL3 output, then:

1. Pick a preset on the left, or add a custom one.
2. Choose `Amplitude` or `Phase Distortion` to focus on one lane at a time.
3. Drag points on the graph to change both level and timing.
4. Watch the point numbers. Matching numbers mean the stages are stacked at the same spot.
5. Use the tables below the graph for exact values when you want precise edits.
6. Use the action buttons on the right when you want to send, save, read, or reset.

Button quick reference:

- `Load RAM` to send a custom envelope until reset.
- `Load Envelope + Settings` to temporarily send the selected envelope and all
  current settings together.
- `Save Envelope` to write a custom envelope to the card.
- `Delete Envelope Slot` to clear a custom envelope slot.
- `Read Envelopes from Card` to load saved card envelopes into the editor.
- `Read Settings from Card` to load the current card settings into the editor.
- `Send Settings` to send performance settings until reset.
- `Reset Preset` to restore the selected preset to its factory value.

The card can save up to eight custom envelopes. Factory presets are not
overwritten. Temporary envelope and settings changes are lost at restart. Use
`Save Envelope` for the envelope, then hold the hardware switch down from the
middle position to save the current performance settings as the startup
baseline.

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

From the repository root:

```sh
cmake -S . -B build
cmake --build build
```

The built firmware will be at:

```text
build/C1ZZL3.uf2
```
