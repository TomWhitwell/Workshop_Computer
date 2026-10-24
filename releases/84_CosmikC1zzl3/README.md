# C1ZZL3

Stable production firmware for the Cosmik C1ZZL3 Music Thing Modular Workshop
Computer card.

C1ZZL3 is a dual phase-distortion synthesiser with browser-editable amplitude,
phase-distortion, and pitch envelopes, USB MIDI device/host support, optional
Turing MIDI output, and a Turing machine mode with CV and pulse outputs. Turing
MIDI output defaults to off and must be enabled deliberately.

For the user-facing card guide, see:

```text
CARD_README.md
```

## Stable Build

Current stable UF2:

```text
uf2/C1ZZL3_CORE.uf2
```

Checksum:

```text
47383c4ff54160a4d8fbfd5ee799b42f142def7ddd0a95de47eeb97d8617a6b0
```

This is hardware-tested production release 1.4.

Release 1.4 works with Envelope Lab and C1ZZL3 Import Lab and includes Web MIDI
PD, detune, eight waveform families, card-to-editor envelope readback, browser
CZ patch import handoff, pitch envelopes, gate-held envelope looping with
natural completion on gate/note release, corrected CZ DCW-to-PD,
DCA-to-amplitude, and DCO-to-pitch import mapping, high-PD audio smoothing, and
rapid-retrigger oscillator phase continuity.

## Alternate RAD Build

This release also includes the alternate C1ZZL3 RAD v9 firmware:

```text
uf2/C1ZZL3_RAD.uf2
```

Checksum:

```text
a425adb8f71686a3cd85cec2b4368a5a5fb61b421e196abc5a25cdcbd91eb0b0
```

RAD is an alternative firmware image, not an update layered on top of Core 1.4;
flash one version at a time. It retains the same physical controls, Turing
machine interface, USB MIDI modes, and shared hosted Web MIDI editor and Import
Lab. RAD v9 adds independently editable Amp1/Amp2, PD1/PD2, and Pitch1/Pitch2
envelopes, separate oscillator waveform-family settings, named sound-preset
slots, saved sound-preset settings, and editor readback for those settings.

## Core And RAD Comparison

| Feature | Core 1.4 | RAD v9 |
|---|---|---|
| Firmware to flash | `uf2/C1ZZL3_CORE.uf2` | `uf2/C1ZZL3_RAD.uf2` |
| Amplitude envelopes | One shared envelope for both oscillators | Independent Amp1 and Amp2 envelopes |
| Phase-distortion envelopes | One shared envelope for both oscillators | Independent PD1 and PD2 envelopes |
| Pitch envelopes | One shared envelope for both oscillators | Independent Pitch1 and Pitch2 envelopes |
| Waveform settings | Shared between both oscillators | Separate waveform settings for each oscillator |
| Panel Y knob / CC23 | Controls both oscillators' waveform | Controls oscillator 1 waveform; set oscillator 2 in the editor |
| Base phase-distortion amount | Shared between both oscillators | Shared between both oscillators; PD envelopes remain independent |
| Custom card slots | Eight custom envelope slots | Eight named sound-preset slots with envelopes and performance settings |
| Save Envelope Only | Saves compatible shared envelope data | Saves six envelope lanes and slot name, retaining the slot's saved settings |
| Save Sound Preset | Falls back to a compatible envelope save | Saves six envelope lanes, slot name, and current performance settings together |
| Card readback | Saved envelopes and current settings | Saved envelopes, slot names, per-slot performance settings, and current settings |
| Turing machine | Audio, CV, pulse, and optional MIDI output | Same as Core |
| Physical controls and MIDI CC map | Synth, performance edit, and Turing controls | Same layout and CC numbers as Core, with the waveform distinction above |
| Browser tools | Shared Envelope Lab and CZ Import Lab | Same tools, with independent oscillator editing and sound-preset support |
| Source-build output | `build/C1ZZL3.uf2` | `build/C1ZZL3_FULL_DUAL_OSCILLATORS.uf2` |

Use the [RAD README](rad/README.md) and [RAD card guide](rad/CARD_README.md)
for its editing, sound-preset saving, and source-build instructions.

Unlike Gnarly, both Core and RAD retain Turing mode, use detune/ring/noise on
the switch-down page, and have no recipe banks or oscillator 2 panel page.

## Core 1.4 Feature Set

- Phase-distortion synth voice.
- Factory envelopes plus eight protected custom envelope slots.
- Web MIDI envelope editor with amplitude, phase-distortion, and pitch lanes.
- USB MIDI device mode for DAW/browser use.
- USB MIDI host mode for class-compliant controllers.
- MIDI notes with gate-held envelope sustain/release.
- MIDI CC control with knob pickup handoff.
- Turing machine audio, CV, pulse, and optional MIDI note output.
- Turing CV and pulse outputs continue running in synth mode.
- Settings readback from the card into the Web MIDI editor.
- Saved envelope readback from the card, including pitch envelope data.
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
2. Choose `Amp 1 / Osc 1` or `PD 1 / Osc 1` for Core's amplitude or
   phase-distortion envelope; Core shares these lanes between both oscillators.
3. Use `Pitch 1 / Osc 1` below the main graph for Core's shared pitch
   envelope. Independent oscillator 2 lanes require RAD firmware.
4. Drag points on the graphs to change both level and timing.
5. Watch the point numbers. When stages stack, only the highest number is shown.
6. Use the tables below the graphs for exact values when you want precise edits.
7. Connect Web MIDI. The editor automatically checks the card firmware type and saved envelope slots.
8. Use the action buttons when you want to send, save, refresh, or reset.

Button quick reference:

- `Load RAM`: send the envelope to the card until reset.
- `Load Sound Preset`: temporarily load the selected envelope and send
  all current settings in one action.
- `Save Envelope Only`: store the selected compatible custom envelope in flash.
- `Save Sound Preset`: on Core, falls back to a compatible envelope save;
  per-slot names and performance settings require RAD.
- `Delete Envelope Slot`: clear the selected custom slot from card flash.
- `Read Envelopes from Card`: load occupied card slots into the editor without
  overwriting changed local drafts.
- `Read Settings from Card`: pull the current performance settings into the editor.
- `Send Settings`: send the current performance settings to the card.
- `Reset Preset`: restore the selected preset to its factory value.

`Load RAM`, `Load Sound Preset`, and `Send Settings` are temporary. Use
`Save Envelope Only` to retain an envelope in flash. To make the current performance
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

From this firmware source folder:

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

USB MIDI host support includes the MIT-licensed rppicomidi files, copyright
2023 rppicomidi. Their copyright and licence notices are retained in the
corresponding source files.

## Firmware distribution notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
copyright and license texts. Keep the accompanying `NOTICE.txt` with
each UF2 when redistributing it.
