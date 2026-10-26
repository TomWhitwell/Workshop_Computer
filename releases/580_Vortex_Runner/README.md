# Vortex Runner

Vortex Runner is a CS-80-inspired monophonic synth voice for the Workshop
Computer. It plays one shared note and gate through two separately coloured
outputs: **Audio Out 1 is Voice A; Audio Out 2 is Voice B**. Detune Voice B
for width, or give each output its own oscillator mix and filter settings.

Both editions include 12 dB/octave high-pass and low-pass filters, amp and
filter envelopes, portamento, ring modulation, 31 factory presets, and eight
named saved presets. This is an independent instrument inspired by the
CS-80's musical gestures, with no Yamaha affiliation.

## Choose Your Firmware

Flash **one** edition. Version **0.1.2 is Beta**: both binaries compile and
pass MIDI edition checks, but still require hardware verification.

| Edition | Download | Choose this for |
|---|---|---|
| Standard | [Vortex_Runner_0.1.2.uf2](uf2/Vortex_Runner_0.1.2.uf2) | Panel and CV/gate playing, ordinary USB MIDI keyboards/controllers, and the Web MIDI editor. |
| 8mu | [Vortex_Runner_8mu_0.1.2.uf2](uf2/Vortex_Runner_8mu_0.1.2.uf2) | The same synth and editor, plus the 8mu layers, fader pickup and LED feedback described below. |

Checksums are in [uf2/SHA256SUMS.txt](uf2/SHA256SUMS.txt). The catalogue lists
only these two editions; earlier binaries are retained in the
[firmware archive](uf2/archive/README.md) for recovery. Complete source for both is included; see
[Building Both Editions](#building-both-editions) at the end of this guide.

## Quick Start

1. Flash the chosen UF2 to a program card using the normal Workshop Computer
   BOOTSEL workflow, then insert the card.
2. Connect Audio Outs 1 and 2 to two mixer channels. Pan them left and right
   to hear the A/B differences, or use either output on its own.
3. For panel auditioning, leave USB and Pulse In 1 unpatched and start with
   the switch in Middle. The voice drones without a gate/controller. With
   no saved startup slot, the fallback sound is **Doctor Who Theme**.
4. For CV playing, connect pitch to **Audio/CV In 1** and gate to **Pulse In 1**.
   The editor selects the pitch range: `-3 V to +3 V, C4 at 0 V` or
   `0 V to +5 V, C4 at +3 V`.
5. For a USB MIDI keyboard, connect it before powering/resetting the Computer
   so USB boots in host mode. With USB MIDI connected and no external gate,
   play a MIDI note to hear the voice.
6. For an 8mu, use the **8mu edition** and follow [Using The 8mu](#using-the-8mu).
   For browser editing, connect the Computer directly to your computer instead;
   see [Web MIDI Editor And Saving](#web-midi-editor-and-saving).

## Panel Controls

A short Down tap selects whether the panel edits **Voice A or Voice B**.
Workshop LEDs 1 and 2 identify the selected voice. Switch pages and voice
changes use soft pickup: move a knob near its stored value before it takes
control, so changing pages does not jump the sound.

| Switch position | Main | X | Y |
|---|---|---|---|
| Middle: oscillator page | Shared base-pitch offset | Selected voice's pulse width | Selected voice's PWM amount |
| Up: filter page | Selected voice's LP cutoff | Selected voice's HP cutoff | Selected voice's resonance |
| Down, held: performance page | Voice B detune | Ring-modulation amount | Vibrato / LFO-to-pitch depth |

A brief Down tap changes A/B focus; holding Down exposes the performance
controls. Holding it for about **two seconds** opens the preset browser.

The source mixer, separate A/B output levels, envelope shapes, CV policy and
additional LFO destinations are available in the Web editor. Saw, pulse, sine
and noise levels add together. Each voice has its own HP/LP filter settings;
resonance affects both filter paths and is bounded below self-oscillation.
Pitch, gate, amp/filter envelopes, portamento, expression and LFO timing are
shared between A and B.

### Panel LEDs

LED numbers here are **1–6**, left-to-right, top-to-bottom: `1 2 / 3 4 / 5 6`.

| LEDs | Normal panel operation |
|---|---|
| 1 / 2 | Voice A / Voice B selection; the selected voice is brighter. |
| 3 / 4 | Stored X value / X pickup status: dim while waiting, bright after pickup. |
| 5 / 6 | Stored Y value / Y pickup status. LED 5 also indicates a long Down hold. |

With an identified 8mu attached, these become the fader-feedback display
[described below](#8mu-led-feedback). Preset browsing temporarily takes priority.

## Inputs And Outputs

| Jack | Function |
|---|---|
| Audio/CV In 1 | 1 V/oct pitch, with the range selected in the Web editor. |
| Audio In 2 | Reserved; there is no external-audio processing path in this release. |
| CV In 1 / CV In 2 | Filter and expression modulation, selected as a pair in the editor; see below. |
| Pulse In 1 | Amp/filter gate. When patched, it takes priority over MIDI gate. |
| Pulse In 2 | Unused. |
| Audio Out 1 / Audio Out 2 | Voice A / Voice B. |
| CV Out 1 / CV Out 2 | Unused. |
| Pulse Out 1 / Pulse Out 2 | Unused. |

The editor's CV pairing options are:

| CV mode | CV In 1 | CV In 2 |
|---|---|---|
| Contour Sweep + Expression | Moves HP and LP cutoff together | Adds LP brightness and resonance |
| High-Pass Focus + Brightness | Moves HP cutoff | Adds LP brightness |
| Low-Pass Brightness + Resonance | Moves LP cutoff | Changes resonance |

MIDI notes use absolute pitch while active; the panel base-pitch offset does
not transpose them. Pitch bend is ±2 semitones. After a MIDI release tail,
pitch returns to the CV/manual source. USB MIDI host mode and browser/device
mode are chosen at boot; reconnect and reset when changing between them.

## Preset Browser

The hardware browser offers **31 factory presets followed by occupied saved
slots**. Empty saved slots are skipped. It works with either edition and with
an 8mu connected.

1. Hold Down during startup, or hold Down for about two seconds during play.
2. Turn Main to choose a preset. The six panel LEDs show its one-based number
   in binary, with LED 1 as the low bit.
3. Release Down to arm the selection, then press Down again to load it.

Loading a saved slot also makes it the startup slot. Loading a factory preset
does not change the saved startup choice. Browsing recalls presets; it does
not save the sound you have been editing over an existing slot.

Factory presets are immutable and initially load A/B in unison. Saved slots
can retain separate A/B colours and include the pitch-CV range. With no saved
startup slot, the fallback is Doctor Who Theme, including its portamento.

## Using The 8mu

Use **Vortex_Runner_8mu_0.1.2.uf2**. Connect the 8mu directly to a Rev 1.1
Workshop Computer's USB port **before powering/resetting** the Computer.
The helper identifies the 8mu and loads its default controller mapping for
this session; separate Gnarly bank JSON files are not used. Identification
selects controller bank 1 and temporarily locks its bank buttons until the
8mu is power-cycled. Use **A–D** to select Vortex Runner layers instead.

The 8mu changes sound parameters; it does not supply playable notes or gates.
For pitched playing, provide pitch on Audio/CV In 1 and gate on Pulse In 1.
With only an 8mu connected and no gate source, MIDI mode waits for a note.

The 8mu and Web MIDI editor cannot operate together because they use opposite
roles on the same USB port. Prepare and save your presets in an editor session,
then reconnect/reset with the 8mu attached for performance.

### 8mu Button And Fader Table

Press a button once to select its layer. The corresponding **8mu LED 1–4**
lights. Each column below is a physical fader, numbered left-to-right.

| Button / layer | Fader 1 | Fader 2 | Fader 3 | Fader 4 | Fader 5 | Fader 6 | Fader 7 | Fader 8 |
|---|---|---|---|---|---|---|---|---|
| **A / LED 1: Tone** | Saw level | Pulse level | Sine level | Noise level | Pulse width | PWM amount | LP cutoff | Resonance |
| **B / LED 2: Amp envelope** | Amp attack | Amp decay | Amp sustain | Amp release | Output level | Expression depth | Portamento | Vibrato depth |
| **C / LED 3: Filter envelope** | Filter attack | Filter decay | Filter sustain | Filter release | HP cutoff | Resonance | LFO-to-filter depth | LFO-to-amp depth |
| **D / LED 4: Performance** | Voice B detune | Ring amount | Ring speed | LFO rate | Vibrato depth | LFO-to-PWM depth | LFO-to-filter depth | LFO-to-amp depth |

Tone controls set the corresponding A/B parameters together. The envelopes
and modulation controls are shared; D's first fader specifically detunes
Voice B. Use the panel or editor for separate A/B tone editing.

**A–D select layers; they do not save, revert, or select presets.** In
particular, C selects the filter-envelope layer. Controller edits are live
RAM changes and are not written to flash by these buttons. Recalling a preset
or resetting can replace those edits. There is no 8mu preset-save action in
this firmware; use the Web editor to create persistent presets in advance.

Each layer-button press re-arms pickup for that layer's faders. Move a fader
slowly near the stored parameter value until it captures; after capture it
follows normally. Pickup is proximity-based, so a very fast sweep can skip
the capture window. Fader movement still updates the feedback display while
waiting, so you can tell which fader needs pickup.

### 8mu LED Feedback

The 8mu's own LEDs indicate its selected layer. The **Workshop Computer**
LEDs show the most recently moved fader:

| Workshop LEDs | Meaning |
|---|---|
| 1–3 | Fader number in binary: `000` = fader 1 through `111` = fader 8; LED 1 is the low bit. |
| 4 | Pickup status: dim before capture, bright after capture. |
| 5–6 | Physical position: LED 5 covers the lower half of travel; LED 6 covers the upper half. |

These are different displays: Vortex Runner does **not** use Gnarly's
per-slider dim/full pickup LEDs on the 8mu. Unplugging the 8mu restores the
normal panel display. While the preset browser is open, the Workshop LEDs
show the preset number instead. To browse with the 8mu attached, use the
Workshop switch and Main knob as described in [Preset Browser](#preset-browser).

If an A–D button changes pitch or retriggers a held CV note, check that you
flashed the current 8mu edition. Layer button notes should not enter the
synth-note path once the 8mu is identified.

## Web MIDI Editor And Saving

Both editions use the same [Web MIDI editor](web/index.html). Connect the
Workshop Computer directly to your computer with a USB data cable and reset
into USB device mode. Use a browser with Web MIDI and SysEx support, such as
Chrome or Edge, and choose **Connect MIDI** in the editor.

If serving the editor locally, run this from the card folder:

```sh
python3 -m http.server 5177 --directory web
```

Open `http://localhost:5177`. Editing A/B source levels, pulse/PWM, filters and
output levels is separate; envelopes, glide, expression, LFO and ring settings
belong to the shared patch.

| Editor action | What it does |
|---|---|
| Apply Settings | Sends the displayed patch to the live card without saving it. |
| Read Patch in slot | Despite the button label, reads the **current live patch** into the editor. |
| Refresh Slots | Reads the saved-slot map and names. |
| Load Slot | Reads/recalls the selected saved slot. |
| Save Settings | Applies and saves the displayed patch and name in the selected card slot. Wait for verification. |
| Set Start Slot | Chooses an already saved slot for startup. |
| Delete Preset | Deletes the selected saved card slot. |

To keep a sound, select one of eight card slots, enter a name, and use
**Save Settings**. The first saved slot becomes the startup slot automatically;
use **Set Start Slot** to change it. Names can be up to 24 characters. A local
browser preset and an applied live patch are not a flash save.

Factory/example choices preserve the editor's current pitch-CV policy when
loaded. Saved card slots are complete snapshots and restore their own CV range.

## MIDI CC Reference

Standard USB MIDI notes, pitch bend and CCs are available in both editions.
In the 8mu edition, identified 8mu faders use the layer table above instead
of their ordinary CC34–41 interpretation.

| CC | Control |
|---|---|
| 1 | Vibrato / LFO-to-pitch depth |
| 20–27 | Saw, pulse, sine, noise, pulse width, PWM, LP cutoff, resonance |
| 28–31 | HP cutoff, ring amount, ring speed, portamento |
| 32–35 | Linked amp/filter attack, decay, sustain, release offsets |
| 2 or 11 | Expression depth |
| 7 | Output level |
| 76 | LFO rate |
| 77 or 93 | LFO-to-PWM depth |
| 91 | LFO-to-filter depth |
| 92 | LFO-to-amp depth |

CC32–35 offset the amp and filter envelopes together while preserving their
relative settings; the editor and the 8mu's B/C layers edit them separately.
Ordinary keyboard notes, including C2/C3/C4/C5, remain playable in the standard
edition without 8mu note interception.

## Firmware Development

All instructions above describe using the card. The following sections are for
building or modifying it yourself. Both editions share [CS80.cpp](CS80.cpp),
[CS80_LUT.cpp](CS80_LUT.cpp), the USB support files and one Web MIDI editor.
Only 8mu integration is conditional; the synth and saved-preset layouts match.

### Architecture And Protocol

- RP2040 at 192 MHz; copy-to-RAM firmware.
- Audio runs on core 0. USB MIDI and Web MIDI processing run on core 1.
- Complete patches cross through a bounded lockless handoff; rapid editor
  updates coalesce to the newest patch before the next 64-sample control tick.
  Readback snapshots cross through a separate queue.
- Web MIDI manufacturer `0x7d`, wire ID `CS80`, protocol version `10`.
- Commands cover apply/read patch, save/read/delete slot, slot map and startup
  slot selection. The payload includes separate A/B tone settings and shared
  envelope, CV, LFO and performance settings.
- The catalogue's explicit `uf2` list in [info.yaml](info.yaml) selects only the
  two current editions. Preserve historical binaries without adding them to
  that public list. Local test builds belong in ignored `test-uf2/`.

## Building Both Editions

Install the Pico SDK, CMake, Arm embedded toolchain, and picotool, and set
`PICO_SDK_PATH` to your SDK checkout. Run from this card folder:

```sh
cmake -S . -B build -DPICO_NO_PICOTOOL=0 -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

This builds both `build/Vortex_Runner_0.1.2.uf2` and
`build/Vortex_Runner_8mu_0.1.2.uf2`. To build just one edition:

```sh
cmake --build build --target VortexRunner -j2
cmake --build build --target VortexRunner8mu -j2
```

Both targets compile [CS80.cpp](CS80.cpp), [CS80_LUT.cpp](CS80_LUT.cpp), and the
included USB support files. Only the 8mu target includes [EightMU.h](EightMU.h).
No separately fetched or private source is needed beyond the Pico SDK and its
normal dependencies. Preset layouts and Web MIDI protocol v10 are unchanged. Both targets reserve a
16 KiB USB/MIDI stack for nested Web MIDI save handling and a 4 KiB main stack
for audio-side preset selection; the former default 2 KiB stacks were smaller
than the measured save/browser frames.

If configuring with `-DPICO_NO_PICOTOOL=1`, the SDK emits ELF/BIN files without
UF2s. Convert both ELF files using an installed picotool:

```sh
picotool uf2 convert build/Vortex_Runner_0.1.2.elf build/Vortex_Runner_0.1.2.uf2 --family 0xe48bff56 --platform rp2040
picotool uf2 convert build/Vortex_Runner_8mu_0.1.2.elf build/Vortex_Runner_8mu_0.1.2.uf2 --family 0xe48bff56 --platform rp2040
```

Run the host-side MIDI edition regression checks with:

```sh
python3 tests/midi_editions_test.py
```

On hardware, check panel/CV playing, ordinary USB MIDI notes and CCs, shared
Web MIDI patch/slot readback and save/reload for each edition. In the 8mu
edition, additionally check A-D layer changes during held CV notes, fader
pickup, controller LEDs, and the hardware preset browser. Neither new binary
has been hardware-verified yet.

## References

Cards 84 and 101 are the main local references for the Web MIDI/editor pattern,
192 MHz build setup, copy-to-RAM configuration, dual-core USB MIDI worker, flash
storage, and complex synth-card release structure:

- `releases/84_CosmikC1zzl3`
- `releases/101_Gnarly_C1ZZL3`

## Attribution And License Notes

Vortex Runner firmware is by Adrian Vos and is released under the MIT License.
See `LICENSE`.

The included `computercard.h` file is ComputerCard by Chris Johnson, version
0.3.0, also MIT licensed. It is copied from:

```text
Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h
```

The USB MIDI host helper files `usb_midi_host.*` and
`usb_midi_host_app_driver.c` are copied from the local C1ZZL3 cards and retain
the upstream rppicomidi MIT copyright notices in the source files. The TinyUSB
descriptor/config pattern is adapted from Adrian Vos's C1ZZL3 cards for this
card's Web MIDI device interface.

The included `pico_sdk_import.cmake` file is the standard Raspberry Pi Pico SDK
import helper. It carries the Raspberry Pi (Trading) Ltd. BSD-style notice in
the file itself.

## Firmware distribution notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
copyright and license texts. Keep the accompanying `NOTICE.txt` with
each UF2 when redistributing it.
