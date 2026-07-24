# C1ZZL3

C1ZZL3 Rad firmware for the Cosmik C1ZZL3 Music Thing Modular Workshop
Computer card.

C1ZZL3 Rad is a dual phase-distortion synthesiser with browser-editable Amp1/Amp2,
PD1/PD2, and Pitch1/Pitch2 envelopes, USB MIDI device/host support, optional
Turing MIDI output, and a Turing machine mode with CV and pulse outputs. Turing
MIDI output defaults to off and must be enabled deliberately.

For the user-facing card guide, see:

```text
CARD_README.md
```

## Rad Build

Current Rad UF2:

```text
uf2/C1ZZL3.uf2
```

Checksum:

```text
fb3cb4d83497be6cfb94a56007120d15f310c184d813a1c8bb76fa304dd28733
```

This branch packages C1ZZL3 Rad, the hardware-tested protocol v9 full-dual-oscillator stable advanced version, as a separate release-84 proposal.

Rad v9 keeps the Turing machine hardware interface from Core while adding full dual-oscillator Web MIDI support: Amp1/Amp2, PD1/PD2, Pitch1/Pitch2, separate oscillator wave-family settings, slot names, saved sound-preset settings, card-to-editor readback, CZ Import Lab handoff, pitch envelopes, gate-held envelope looping with natural completion, high-PD audio smoothing, and rapid-retrigger oscillator phase continuity.

## Rad Feature Set

- Phase-distortion synth voice.
- Factory envelopes plus eight protected custom envelope slots.
- Rad Web MIDI editor with Amp1/Amp2, PD1/PD2, Pitch1/Pitch2, and separate oscillator wave-family settings.
- USB MIDI device mode for DAW/browser use.
- USB MIDI host mode for class-compliant controllers.
- MIDI notes with gate-held envelope sustain/release.
- MIDI CC control with knob pickup handoff.
- Turing machine audio, CV, pulse, and optional MIDI note output.
- Turing CV and pulse outputs continue running in synth mode.
- Settings readback from the card into the Web MIDI editor.
- Saved sound-preset readback from the card, including slot names, performance settings, and dual pitch envelope data.
- Ring, noise, MIDI channel, Turing range, and Turing MIDI settings persist;
  the baseline for Turing MIDI output is off.

## Controls

Switch middle: synth mode.

- Main: pitch
- X: phase distortion
- Y: waveform
- `CV In 1`: phase-distortion modulation
- `CV In 2`: waveform modulation
- `Pulse In 2`: gate-held envelope trigger/release and oscillator sync

Switch down from middle: performance edit and save.

- Main: oscillator 2 detune
- X: ring modulation
- Y: noise/grit

Switch up: Turing mode.

- Main: mutation/lock
- X: sequence length, 2 to 16 steps
- Y: internal clock speed
- `Pulse In 1`: external clock
- `CV Out 1`: stepped Turing CV
- `CV Out 2`: smoothed Turing CV
- `Pulse Out 1/2`: Turing pulses

## MIDI

MIDI CC controls on the selected input channel:

- `CC1`: phase distortion
- `CC20`: oscillator 2 detune
- `CC21`: ring modulation
- `CC22`: noise amount
- `CC23`: waveform
- `CC24`: Turing CV octave range, from 1 to 8 octaves

The physical knobs and MIDI CC controls share the same control values. After a
CC change, the related knob must be swept through the current value before it
takes over again.

## Web MIDI Editor

Hosted editor:

```text
https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/index.html
```

Local editor from this release folder:

```sh
python3 -m http.server 5173 --directory web
```

Open:

```text
http://localhost:5173
```

Use Chrome or another browser with Web MIDI and SysEx support.

## C1ZZL3 Import Lab

Hosted import lab:

```text
https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/import/index.html
```

Local import lab from this release folder:

```sh
python3 -m http.server 5174 --directory web/import
```

Open:

```text
http://localhost:5174
```

Use this page to decode Casio CZ `.syx` patches into C1ZZL3 drafts, then open
the result in Envelope Lab for final editing and sending.

Current Import Lab features:

- Light and dark mode toggle matching the Envelope Lab palette.
- Larger `C1ZZL3 Import Lab` header and a clearer guided import workflow.
- Drag-and-drop or file-picker import for Casio CZ `.syx` files.
- Browser-side validation, patch summary, decoded data, and draft mapping.
- CZ frame awareness for common patch-send SysEx files, including command,
  location, channel, selected data offset, and payload candidates.
- CZ envelope assignment: choose merged, line 1, or line 2 mapping for CZ DCA
  amplitude and DCW phase-distortion envelopes; CZ DCO pitch envelopes map to
  the pitch lane.
- Draft handoff into Envelope Lab in a new tab for final editing and card send.
- Separate import page so CZ translation and envelope editing stay distinct.

Import Lab flow:

1. Open `C1ZZL3 Import Lab`.
2. Drop in or choose a Casio CZ patch file.
3. Review the validation, decoded summary, warnings, and mapped draft.
4. Use `Open In Envelope Lab` to carry the draft into the main editor.
5. In Envelope Lab, review the result, adjust if needed, and send or save it.

## How To Use The Editor

1. Pick a preset on the left, or add a custom one.
2. Choose `Amplitude` or `Phase Distortion` to focus the main graph lane.
3. Use the Pitch Envelope graph below it to adjust pitch movement.
4. Drag points on the graphs to change both level and timing.
5. Watch the point numbers. When stages stack, only the highest number is shown.
6. Use the tables below the graphs for exact values when you want precise edits.
7. Connect Web MIDI. The editor automatically checks the card firmware type and saved envelope slots.
8. Use the action buttons when you want to send, save, refresh, or reset.

Button quick reference:

- `Load RAM`: send the envelope to the card until reset.
- `Load Envelope + Settings`: temporarily load the selected envelope and send
  all current settings in one action.
- `Save Envelope`: store the selected custom envelope in flash.
- `Delete Envelope Slot`: clear the selected custom slot from card flash.
- `Read Envelopes from Card`: load occupied card slots into the editor without
  overwriting changed local drafts.
- `Read Settings from Card`: pull the current performance settings into the editor.
- `Send Settings`: send the current performance settings to the card.
- `Reset Preset`: restore the selected preset to its factory value.

`Load RAM`, `Load Envelope + Settings`, and `Send Settings` are temporary. Use
`Save Envelope` to retain an envelope in flash. To make the current performance
settings the startup baseline, move the hardware switch from middle to down and
hold it until the card confirms the save.

The card can save up to eight custom envelopes. Factory presets are not
overwritten. Custom presets are labelled `Local only`, `Saved - slot N`, or
`Changed - slot N`. Envelope readback confirms which custom slots are occupied
and verifies saves and deletions when supported by the firmware. The editor also runs a quiet settings/envelope check automatically when MIDI connects or ports change, so the firmware type is visible without first pressing the read buttons.

Envelope behaviour:

- Pulse In 2 and MIDI note-on trigger the selected envelope and sync the
  oscillators when the envelope starts from inactive.
- While the gate or MIDI note is held, loop-capable envelopes cycle their middle
  stages.
- A short trigger runs the envelope through to completion.
- Pulse In 2 gate-off or MIDI note-off exits the loop and lets the envelope
  complete naturally from its current point.
- Turing-triggered envelopes continue to run through without waiting for a gate
  release.
- Rapid retriggers keep oscillator phase continuous while the envelope is active
  to reduce clicks.

## Build

```sh
cmake -S . -B build
cmake --build build
```

The built UF2 will be:

```text
build/C1ZZL3.uf2
```

The production source build currently reports:

```text
FLASH: 146736 B
RAM: 155644 B
```

## Stability Notes

The stable version includes the lookup-table oscillator optimisation, 192 MHz
clock, optional Turing MIDI output, settings readback, and full CC/knob pickup
handoff. Turing MIDI output defaults off across current builds with Turing MIDI
support, and should be enabled deliberately when needed. Tap tempo remains
removed; Y is the Turing internal clock control.

## License Notes

This project is released under the MIT License. The included `computercard.h`
hardware helper is ComputerCard by Chris Johnson and is also MIT licensed; keep
its MIT notice present when copying firmware files into releases or experiments.
