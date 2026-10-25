# C1ZZL3 Gnarly

C1ZZL3 Gnarly is a dual-oscillator phase-distortion synth card for the Music
Thing Modular Workshop Computer.

Gnarly provides six-lane envelope editing, named sound presets, independent
base phase-distortion amounts, an oscillator 2 interval control, and four recipe
banks. Dedicated oscillator pages and a performance page put sound editing,
ring modulation, and noise/grit on the hardware controls.

Card 84 Cosmik C1ZZL3 is a simpler version of the phase-distortion synth engine.
See the [Core/RAD comparison](../84_CosmikC1zzl3/README.md#core-and-rad-comparison)
for those firmware choices.

## Choose Your Firmware

| Firmware | Use it for |
|---|---|
| [Standard Gnarly](uf2/C1ZZL3_GNARLY_V11.uf2) | Panel and MIDI performance with browser envelope editing. |
| [Gnarly for 8mu v2](uf2/Gnarly_C1ZZL3_8mu.uf2) | The same sound engine and browser editor, plus 8mu performance and envelope editing. |

Flash one of these UF2 files, not both. Their SHA-256 checksums are in
[checksum file](uf2/SHA256SUMS.txt).

## What It Does

- Runs two phase-distortion oscillator lanes.
- Uses separate Amp1/Amp2, PD1/PD2, and Pitch1/Pitch2 envelopes.
- Supports named sound presets saved on the card.
- Saves either envelope-only data or full sound presets with performance
  settings.
- Uses four recipe wave banks for simple and CZ-like oscillator pairings.
- Reads and writes settings through the Gnarly Web MIDI Lab.
- Imports Casio CZ `.syx` patches through the shared C1ZZL3 Import Lab.
- Provides dedicated oscillator editing and performance control pages.

## Hardware Controls

Switch middle: oscillator 1 page.

- Main: shared pitch
- X: oscillator 1 phase distortion
- Y: oscillator 1 recipe slot in the selected bank

Switch up: oscillator 2 page.

- Main: oscillator 2 base interval, -24 to +24 semitones, centred at unison
- X: oscillator 2 phase distortion
- Y: oscillator 2 recipe slot in the selected bank

Switch down hold: performance and bank page.

- Main: recipe bank
- X: ring modulation
- Y: noise/grit

Hold the switch down from the middle position for about eight seconds to save
the current performance settings as the startup baseline. If powered up with
the switch down, leave that position before using the performance controls.

The modulation inputs affect both oscillators on every edit page:

- `Audio/CV In 1`: shared 1V/oct pitch modulation.
- `CV In 1`: adds to both oscillator PD amounts.
- `CV In 2`: adds to both oscillator recipe-slot/wave controls.
- `Pulse In 2`: triggers the selected envelopes. Held gates keep loop-capable
  envelopes cycling; gate-off lets them complete naturally.

Audio Out 1 carries oscillator 1 and Audio Out 2 carries oscillator 2.
`Pulse In 1` is unused. CV Out 1/2 and Pulse Out 1/2 are held low.

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

Banks 2–4 use fixed blend amounts chosen for each recipe. Some recipes favour
the first waveform, some are equal 50/50 mixes, and some favour the second.
For example, Bank 3 slot 8 is approximately 31% Resonant trapezoid window
and 69% Square. Bank 1 morphs between the eight plain waveform families.

On the switch-down page, LEDs 1 and 2 show the selected recipe bank:

- Bank 1: LEDs 1 and 2 off.
- Bank 2: LED 1 on.
- Bank 3: LED 2 on.
- Bank 4: LEDs 1 and 2 on.

LED 3 shows oscillator 2 interval/spread as brightness increasing away from
unison. LEDs 4 and 5 show ring modulation and noise/grit. On the oscillator
pages, LEDs 1 and 2 show that oscillator's PD and waveform/recipe position.

**LED 6 has a different purpose in each firmware:**

| Firmware | LED 6 during normal operation |
|---|---|
| Standard Gnarly | Shows the panel page: half brightness with the switch in the middle, off with it up, and full brightness while held down. |
| Gnarly 8mu | Shows slider editing: fully on for levels (A), blinking twice per second for times (B). The switch position does not change it. |

In the 8mu firmware, a fully lit LED 6 is normal at startup because level
editing is the default. It should start blinking after B sends note 121.

## USB MIDI

The card chooses its USB role at boot. Connected to a computer, it appears as
a USB MIDI device for DAW and browser use. Connected to a class-compliant USB
MIDI controller, it can run in USB MIDI host mode.

MIDI note-on triggers the selected envelopes as a held gate. Loop-capable
envelopes cycle until note-off lets them complete naturally.


Gnarly uses `CC20` to `CC27` as an eight-knob performance block. `CC1` is also
kept as oscillator 1 phase distortion so a mod wheel remains useful.

- `CC1`: oscillator 1 phase distortion, mod-wheel friendly.
- `CC20`: oscillator 1 recipe slot.
- `CC21`: oscillator 2 recipe slot.
- `CC22`: ring modulation amount.
- `CC23`: recipe bank.
- `CC24`: oscillator 2 interval/spread.
- `CC25`: oscillator 2 phase distortion.
- `CC26`: noise/grit amount.
- `CC27`: oscillator 1 phase distortion, for eight-knob controllers.

## If You Have An 8mu

`Gnarly_C1ZZL3_8mu.uf2` is an alternative Gnarly firmware that exposes the
envelope lanes on a Music Thing 8mu v2. It retains the same Gnarly sound engine
and Web MIDI editor, but receives its controls from the 8mu instead of using
only the editor.

The JSON bank files are in [8mu_banks/](8mu_banks/). Import them one at a
time with the [16n Faderbank editor](https://16n-faderbank.github.io/editor/),
selecting the matching destination bank on the 8mu before each import. The
supplied USB mappings send on MIDI channel 1; set Gnarly's MIDI input channel
to match.

Use the current 8mu UF2 together with these bank files. Re-import all eight
banks after updating, choosing **Update controller** after each import. The
buttons use **Keyboard** mode, MIDI channel **1**, and velocity **127**.
The same button mappings apply in every envelope bank, including bank 8:

| Button | Keyboard note number in banks 2–8 | Action |
|---|---:|---|
| A | 120 | Select stage-level editing; LED 6 stays fully on. |
| B | 121 | Select stage-time editing; LED 6 blinks twice per second. |
| C | 122 | Save all six lanes and current performance settings in the selected custom slot. |
| D | 123 | Revert all six lanes to their saved values. |

Use numeric note numbers; octave labels differ between editors. Gnarly consumes
these notes as edit commands, so they do not play synth pitches. Commands act
on note-on with nonzero velocity; releasing a button does not undo the command
or change the selected edit mode. CC120–123 remain supported for other
controllers. Notes 120–127 are reserved for button control in the 8mu firmware;
ordinary MIDI notes below 120 continue to play.

The eight controller banks assign sliders as follows. Amp means amplitude,
PD means phase distortion, and Pitch means pitch-envelope offset. Controller
banks are separate from the synth's four recipe banks.

| 8mu bank | Slider 1 | Slider 2 | Slider 3 | Slider 4 | Slider 5 | Slider 6 | Slider 7 | Slider 8 |
|---|---|---|---|---|---|---|---|---|
| 1: Performance | Osc 1 recipe slot | Osc 2 recipe slot | Ring amount | Recipe bank | Osc 2 interval | Osc 2 PD amount | Noise/grit | Osc 1 PD amount |
| 2: Amp1 | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Stage 5 | Stage 6 | Stage 7 | Stage 8 |
| 3: Amp2 | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Stage 5 | Stage 6 | Stage 7 | Stage 8 |
| 4: PD1 | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Stage 5 | Stage 6 | Stage 7 | Stage 8 |
| 5: PD2 | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Stage 5 | Stage 6 | Stage 7 | Stage 8 |
| 6: Pitch1 | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Stage 5 | Stage 6 | Stage 7 | Stage 8 |
| 7: Pitch2 | Stage 1 | Stage 2 | Stage 3 | Stage 4 | Stage 5 | Stage 6 | Stage 7 | Stage 8 |
| 8: Envelope utility | Envelope/custom-slot selection | Amp1 depth | PD1 depth | Pitch1 depth | Pitch2 depth | PD2 depth | Amp2 depth | Master depth |

In banks 2–7, each slider edits either the **level** or the **time** of its
numbered stage in that bank's envelope lane. Press A for levels or B for times;
the selected edit mode carries across lane banks. These sliders set stage
values directly when moved.

| Controller bank | Button A | Button B | Button C | Button D |
|---|---|---|---|---|
| 1 | Unused | Unused | Unused | Unused |
| 2–7 | Edit stage levels | Edit stage times | Save all six lanes and current performance settings in the selected custom slot | Revert all six lanes to their last saved values |
| 8 | Select level edit mode for banks 2–7 | Select time edit mode for banks 2–7 | Same save action as banks 2–7 | Same revert action as banks 2–7 |

Bank 1 sends A = note 124, B = note 125, C = note 126, and D = note 127.
The 8mu firmware ignores these notes, so the buttons do not play sounds or
change the edit mode. In bank 8, A/B select the stage edit mode for banks 2–7;
they do not change the utility sliders' functions.

**Workshop Computer LED 6 shows the selected slider edit mode:**

| Selection | LED 6 |
|---|---|
| A: stage levels | Steady on |
| B: stage times | Blinks twice per second |

Press once; holding A or B is unnecessary. Level mode is selected at startup.
The indication stays visible across controller banks and panel pages, showing
which mode banks 2–7 will use. Startup envelope selection and the brief save
confirmation display temporarily take priority over this indicator.

Keyboard-mode A/B selection, the matching LED 6 indication, and audible
operation passed hardware testing on 25 October 2026.

### Selecting And Editing A Sound

1. Save a custom sound-preset slot from the Gnarly browser editor first.
2. Select controller bank 8 and move slider 1 to choose that saved custom slot.
   The slider steps through factory presets followed by occupied custom slots;
   factory presets are available to play but remain read-only.
3. Select bank 2–7 for the envelope lane you want to edit.
4. Press A to edit levels or B to edit times, then move sliders 1–8 to edit
   the corresponding stages. In level mode, Amp/PD run from minimum to maximum;
   pitch levels run from negative offset through zero at the centre to positive
   offset. In time mode, each slider sets its stage duration from approximately
   zero to four seconds.
5. Press C to save all six lanes and current performance settings in the
   selected custom slot. Press D to discard unsaved stage edits across all six
   lanes and restore their saved values; D does not restore performance settings.

Bank 8 depth sliders scale the existing envelopes rather than editing stage
points. Amp depth changes envelope-controlled output level, PD depth changes
the envelope's added phase distortion, and pitch depth changes the excursion
around zero pitch offset. Master depth scales all six lane depths together.
Depths default to full at startup and act as temporary performance controls;
they are not stored by button C. In envelope Off mode, amplitude depth does
not attenuate the continuous oscillator output.

Stage editing, save, and revert require a saved custom slot. Gestures are
disabled in all supplied banks to avoid unintentional edits.


## Web MIDI Editor

Hosted editor path after Workshop deployment:

```text
https://computer.musicthing.co.uk/programs/101-gnarly-c1zzl3/web/index.html
```

Use Chrome or another browser with Web MIDI and SysEx support. Connect the
card by USB, enable MIDI in the editor, and choose the card's output. The editor
automatically checks card settings and saved slots to identify the firmware.

1. Pick a preset or create a custom one and choose its card slot.
2. Edit Amp1/Amp2 and PD1/PD2 in the main graph, and Pitch1/Pitch2 in the pitch
   graph. Drag points for level/time changes or use the stage tables for exact values.
3. Use `Load RAM` to audition envelope edits or `Load Sound Preset` to load the
   envelope and its settings temporarily.
4. Use `Save Envelope Only` to save the lanes and name while retaining the
   slot's saved performance settings, or `Save Sound Preset` to save the lanes,
   name, and current performance settings together.

| Editor action | Result |
|---|---|
| Load RAM | Loads envelope changes until restart. |
| Load Sound Preset | Loads the envelope and its settings until restart. |
| Save Envelope Only | Saves envelope lanes and slot name in card flash. |
| Save Sound Preset | Saves envelope lanes, name, and performance settings in card flash. |
| Read Envelopes from Card | Reads saved slots, names, and attached settings. |
| Read Settings from Card | Reads the current performance settings. |
| Send Settings | Applies performance settings temporarily. |
| Delete Envelope Slot | Clears the selected custom slot in card flash. |
| Reset Preset | Restores the selected editor preset to its factory value. |

There are eight custom slots; factory envelopes remain protected. Temporary
loads are lost at restart. Selecting a saved sound preset restores its attached
settings. Use the hardware save gesture to set the global startup baseline.

### Selecting An Envelope At Startup

Hold the switch down while powering up to enter envelope selection, then
release it. Turn Main to choose a factory envelope or occupied custom slot.
Move the switch down and release it to finish; hold it down for about eight
seconds before releasing to save the selection as the startup baseline.
Audio is muted during selection. Factory presets use the binary LED display;
custom slots light LED 6 and use LEDs 1–3 for the slot number.

## C1ZZL3 Import Lab

Hosted import lab path after Workshop deployment:

```text
https://computer.musicthing.co.uk/programs/101-gnarly-c1zzl3/web/import/index.html
```

Use this page to decode Casio CZ `.syx` patches into C1ZZL3 drafts with separate
oscillator envelopes, pitch lanes, oscillator wave choices, performance values,
and recipe-bank-compatible settings.

## Building And Development

### Running The Editors Locally

From this release folder, start either editor with a local HTTP server:

```sh
python3 -m http.server 5177 --directory web
```

Open [Envelope Lab](http://localhost:5177). For the standalone Import Lab:

```sh
python3 -m http.server 5178 --directory web/import
```

Open [Import Lab](http://localhost:5178).

### MIDI CC Checks

The C1ZZL3 Envelope Lab has a hidden Developer-mode MIDI CC Test Suite for checking these messages without a hardware controller. It sends individual CC values, a neutral reset, and sweep tests through the selected Web MIDI output.

### Building Firmware

The alternative 8mu source is self-contained in `8mu/`. It can be developed
independently of the standard source. From this release folder:

```sh
cmake -S . -B build -DPICO_NO_PICOTOOL=1
cmake --build build -j2
```

The build creates:

```text
build/C1ZZL3_GNARLY_V11.uf2
```

To build the alternative 8mu firmware:

```sh
cmake -S 8mu -B build-8mu -DPICO_NO_PICOTOOL=1
cmake --build build-8mu -j2
```

This creates `build-8mu/Gnarly_C1ZZL3_8mu.uf2`.

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
