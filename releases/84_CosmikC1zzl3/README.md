# Cosmik C1ZZL3

Cosmik C1ZZL3 is a dual phase-distortion synth card for the Music Thing Modular
Workshop Computer. It combines a playable synth voice, a Turing machine with
CV and pulse outputs, USB MIDI, browser envelope editing, and Casio CZ patch import.
Turing MIDI output is optional and defaults off.

## Choose Your Firmware

| Firmware | Use it for |
|---|---|
| [Core 1.4](uf2/C1ZZL3_CORE.uf2) | Shared amplitude, PD, pitch envelopes, and waveform settings for both oscillators. |
| [RAD v9](uf2/C1ZZL3_RAD.uf2) | Six independent envelope lanes, separate oscillator waveforms, and named sound presets with saved settings. |

Flash one image at a time. Both use the same hardware controls, MIDI CC map,
Envelope Lab, and Import Lab. Checksums are in [SHA256SUMS.txt](uf2/SHA256SUMS.txt).
Both versions have been hardware tested.

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


## Controls

Switch middle: synth mode.

- Main: pitch
- X: phase distortion
- Y: waveform for both oscillators on Core; oscillator 1 waveform on RAD
- `Audio/CV In 1`: 1V/oct pitch modulation
- `CV In 1`: shared phase-distortion modulation
- `CV In 2`: waveform modulation for both oscillators
- `Pulse In 2`: gate-held envelope trigger/release and oscillator sync

On RAD, set oscillator 2 waveform separately in the browser editor. The base
PD amount remains shared, with independent PD1/PD2 envelopes.

Switch down from middle: performance edit and save.

- Main: oscillator 2 detune
- X: ring modulation
- Y: noise/grit

Hold down from middle for about eight seconds to save the current performance
settings as the global startup baseline. Ring and noise default to neutral;
saved settings restore at startup. If powered up with the switch down, leave
that position before using the performance controls.

Switch up: Turing mode.

- Main: mutation/lock
- X: sequence length, 2 to 16 steps
- Y: internal clock speed
- `Pulse In 1`: external clock
- `CV Out 1`: stepped Turing CV
- `CV Out 2`: smoothed Turing CV
- `Pulse Out 1/2`: Turing pulses

Audio outputs carry the self-playing oscillator voice in Turing mode. The
Turing CV and pulse outputs also continue running while playing in synth mode.
Y controls the internal clock; tap tempo is not available.

## USB MIDI

The card chooses its USB role at boot. Connected to a computer, it appears as
a USB MIDI device for DAW and browser use. Connected to a class-compliant USB
MIDI controller, it can run in USB MIDI host mode.

MIDI note-on acts as a held gate: it triggers the selected envelopes and keeps
loop-capable envelopes cycling until note-off lets them complete naturally.

MIDI CC controls on the selected input channel:

- `CC1`: phase distortion
- `CC20`: oscillator 2 detune
- `CC21`: ring modulation
- `CC22`: noise amount
- `CC23`: waveform for both oscillators on Core; oscillator 1 waveform on RAD
- `CC24`: Turing CV octave range, from 1 to 8 octaves

The physical knobs and MIDI CC controls share the same control values. After a
CC change, the related knob must be swept through the current value before it
takes over again.

## Web MIDI Editor

Use the shared [Envelope Lab](https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/index.html)
in Chrome or another browser with Web MIDI and SysEx support. Connect the card
by USB, enable MIDI, and select the card output. The editor checks the firmware
and saved slots automatically.

### Editing Core And RAD Envelopes

1. Pick a preset on the left, or add a custom one.
2. Choose `Amp 1 / Osc 1` or `PD 1 / Osc 1` for Core's amplitude or
   phase-distortion envelope; Core shares these lanes between both oscillators.
3. Use `Pitch 1 / Osc 1` below the main graph for Core's shared pitch
   envelope. On RAD, use Amp1/Amp2, PD1/PD2, and Pitch1/Pitch2 to edit all
   six oscillator lanes independently, and set each oscillator's waveform
   separately in the performance settings.
4. Drag points on the graphs to change both level and timing.
5. Watch the point numbers. When stages stack, only the highest number is shown.
6. Use the tables below the graphs for exact values when you want precise edits.
7. Connect Web MIDI. The editor automatically checks the card firmware type and saved envelope slots.
8. Use the action buttons when you want to send, save, refresh, or reset.

Button quick reference:

- `Load RAM`: send the envelope to the card until reset.
- `Load Sound Preset`: temporarily load the selected envelope and send
  all current settings in one action.
- `Save Envelope Only`: on Core, save the compatible shared envelope; on RAD,
  save all six envelope lanes and slot name while retaining the slot's saved settings.
- `Save Sound Preset`: on RAD, save six lanes, the slot name, and current
  performance settings together. On Core, fall back to a compatible envelope save.
- `Delete Envelope Slot`: clear the selected custom slot from card flash.
- `Read Envelopes from Card`: load occupied slots without overwriting changed
  local drafts; on RAD, also read slot names and attached performance settings.
- `Read Settings from Card`: pull the current performance settings into the editor.
- `Send Settings`: send the current performance settings to the card.
- `Reset Preset`: restore the selected preset to its factory value.

`Load RAM`, `Load Sound Preset`, and `Send Settings` are temporary. Use
`Save Envelope Only` to retain an envelope in flash. To make the current performance
settings the startup baseline, move the hardware switch from middle to down and
hold it until the card confirms the save.

On RAD, selecting a saved sound preset restores its attached performance
settings. The hardware save gesture sets the global startup baseline separately.

The card can save up to eight custom envelopes or RAD sound presets. Factory presets are not
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

## C1ZZL3 Import Lab

Open the shared [Import Lab](https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/import/index.html).

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

To select an envelope at startup, hold the switch down while powering up,
then release it and turn Main to choose. Move down and release to finish;
hold down for about eight seconds before releasing to save the selection as
the startup baseline. Audio is muted during selection.

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

## Building And Development

### Running The Editors Locally

From this release folder:

```sh
python3 -m http.server 5173 --directory web
```

Open [Envelope Lab](http://localhost:5173). For the standalone Import Lab:

```sh
python3 -m http.server 5174 --directory web/import
```

Open [Import Lab](http://localhost:5174). The shared editor detects Core and RAD
and sends the compatible envelope and settings format.

### Building Core 1.4

From this release folder:

```sh
cmake -S . -B build
cmake --build build
```

Output: `build/C1ZZL3.uf2`. The packaged download is named `uf2/C1ZZL3_CORE.uf2`.

### Building RAD v9

From this release folder:

```sh
cmake -S rad -B build-rad
cmake --build build-rad
```

Output: `build-rad/C1ZZL3_FULL_DUAL_OSCILLATORS.uf2`. The packaged download is
named `uf2/C1ZZL3_RAD.uf2`. RAD sources are in `rad/` and share lookup tables
and USB MIDI support files with Core.

### Implementation And Validation Notes

Both versions include lookup-table oscillators, the 192 MHz clock, settings
readback, CC/knob pickup, high-PD audio smoothing, and phase continuity on rapid
retriggers. CZ import maps DCW to PD, DCA to amplitude, and DCO to pitch.
RAD adds per-oscillator envelope and waveform editing, named slots, and
sound-preset settings readback. Turing MIDI defaults off in both versions.

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
