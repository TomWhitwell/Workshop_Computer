# Fragments

Fragments is a six-slot sampler and sequencer for the Music Thing Modular
Workshop System Computer and Workshop Computer.

Record short sounds into the six slots, then play them back with patterns,
shift, repeat division, reverse probability, playback modes, MIDI pitch, and
random CV outputs.

Each slot can hold up to 400 ms of stereo audio at 48 kHz. Samples can be
recorded from the module or imported with the included web editor.

## Downloads

- [fragments.uf2](UF2/fragments.uf2): standard 48 kHz firmware, up to 400 ms per slot.
- [fragments_24k.uf2](UF2/fragments_24k.uf2): alternate 24 kHz firmware, up to 800 ms per slot with a darker sound.

Use [web/fragments_librarian.html](web/fragments_librarian.html) for both
firmware versions. Select the matching firmware profile in the editor before
importing or sending samples.

## Quick Start

1. Patch audio into Audio In 1, Audio In 2, or both.
2. Move Z up.
3. Use Main to choose a slot.
4. Send a gate to Pulse In 2 to record into that slot.
5. Move Z to the middle.
6. Send a clock to Pulse In 1.
7. Use Main to choose a pattern, X to shift it, and Y to set repeat division.

LEDs show the selected slot while Z is up. In playback, they show the active
slot or variation.

## Controls

### Z Up: Slots

| Control | Function |
| --- | --- |
| Main | Selects slot 0-5 |
| X | Sets playback mode |
| Y | Sets reverse probability |
| Pulse In 2 | Records while high |

Playback modes, from low to high on X:

| Mode | Behavior |
| --- | --- |
| Loop | Repeats until the next sequencer step |
| One Shot | Plays once, then rests |
| Interrupt | Plays once, then returns to live input |
| Passthrough | Plays live input instead of the recording |

New or cleared slots default to One Shot with 0% reverse probability.

### Z Middle: Patterns

| Control | Function |
| --- | --- |
| Main | Selects one of 21 patterns |
| X | Shifts every slot number in the pattern, wrapping around |
| Y | Sets repeat division: x1, x2, x4, or x8 |
| Pulse In 1 | Advances the pattern |

Main, X, and Y use pickup behavior. Changing Z position will not immediately
jump to the physical knob value; the setting changes after the knob moves.

### Z Down: Reset, Clear, Save

| Gesture | Function |
| --- | --- |
| Tap Z down | Reset to the first pattern step |
| Hold Z down until all LEDs flash, release, tap once | Clear all samples |
| Hold Z down until all LEDs flash, release, tap twice | Save the current kit |

After the LEDs flash, the card waits one second for a tap. A single tap waits
briefly before clearing so the card can tell it apart from a double tap.

## Recording

- A standard slot records up to 400 ms.
- Variation mode records one longer sample up to 2.4 seconds.
- A recording must be at least 10 ms.
- Only patched audio inputs are recorded.
- If no audio input is patched, the slot is left unchanged.
- In standard mode, recording one channel preserves the other channel.
- Patched-but-silent audio can be recorded as silence.
- Recording does not stop the sequencer.

The monitor input setting controls when live input is mixed with playback:

| Mode | Behavior |
| --- | --- |
| Always | Live input is always mixed with the program output |
| When Armed | Live input is mixed while Z is up or while recording |
| When Recording | Live input is mixed only while recording |

The default monitor mode is When Armed.

## CV And Pulse

| Jack | Function |
| --- | --- |
| CV In 1 | Pattern shift |
| CV In 2 | Reverse probability |
| Pulse Out 1 | 10 ms pulse on every sequencer step |
| Pulse Out 2 | 10 ms pulse on the first index of the pattern |
| CV Out 1 | Configurable random voltage, stepped by default |
| CV Out 2 | Configurable random voltage, slewed by default |

CV In 1 and CV In 2 keep these assignments in every Z switch position. When a
CV input is patched, its matching knob becomes an attenuator: X for CV In 1 and
Y for CV In 2.

The CV inputs automatically work with unipolar signals. If an input crosses
below 0 V, it switches to bipolar behavior so a roughly +/-6 V signal can sweep
the full control range.

The web editor can configure the CV outputs' voltage range, quantization,
clock division, slew time, and CV Out 2 coupling.

## USB MIDI

Fragments appears as a USB MIDI device over USB-C. Depending on the card and
operating system, it may appear as Fragments, MTMComputer, or a Music Thing MIDI
card name.

| MIDI | Function |
| --- | --- |
| Notes | Change playback speed for all slots together |
| C4 / note 60 | Normal speed |
| Pitch bend | +/-12 semitones |
| CC 16 | Main knob |
| CC 17 | X knob |
| CC 18 | Y knob |

Note Off has no assigned behavior. Moving a physical knob takes control back
from its MIDI CC value.

## Variation Mode

Hold Z down while rebooting the Computer with its boot/reset button to start in
Variation mode for that session. Reboot normally to return to standard mode.

Variation mode uses the six slot buffers as one longer sample. Patterns then
play that one sample six different ways:

| Pattern Value | Variation |
| --- | --- |
| 0 | Normal speed |
| 1 | 2x speed, one octave up |
| 2 | 3x speed, one octave and a fifth up |
| 3 | Reverse |
| 4 | Reverse at 2x speed |
| 5 | 0.5x speed, one octave down |

In Variation mode:

- Pulse In 2 records the long sample.
- Main with Z up controls pitch over +/-24 semitones, with sweet spots at
  useful intervals.
- X with Z up sets the global playback mode.
- Y with Z up sets reverse probability.
- CV In 1 still controls variation shift.
- CV In 2 still controls reverse probability.

## Web Editor

Open [web/fragments_librarian.html](web/fragments_librarian.html) in Chrome or
Edge.

1. Select **Connect MIDI** and allow SysEx access.
2. Choose the Fragments MIDI input and output.
3. Select **Ping Card** to confirm the connection.

The web editor can:

- Load, trim, preview, normalize, copy, and paste samples.
- Send one slot or all loaded samples to the card.
- Save and load kits.
- Edit and send pattern banks from CSV.
- Control playback speed and the Main, X, and Y knobs over MIDI.
- Configure monitor input and CV output behavior.

Supported audio import depends on the browser, but WAV, AIFF/AIFC PCM, MP3, and
M4A are supported in common browsers.

## Pattern CSV

Each CSV row is one pattern. Each value is a slot number from 0 through 5. Rows
may contain 1-16 steps. The row number determines the pattern number.

```csv
0
0,1
0,2,4,2
5,4,3,2,1,0
```

The web editor accepts up to 21 rows. Sending fewer rows replaces only those
patterns. Save the kit afterward to keep the new pattern bank across power
cycles.

The factory bank is available at
[web/fragments_factory_patterns.csv](web/fragments_factory_patterns.csv).

## Saved Kits

A saved kit includes:

- Audio slots and channel assignments.
- Slot playback modes and reverse probability.
- The 21-pattern bank.
- CV output settings.
- Monitor input mode.

Pattern selection, shift, division, MIDI note, pitch bend, and current
sequencer position are performance controls and are not saved.

## Building

Requirements:

- Raspberry Pi Pico SDK
- CMake
- ARM embedded GCC toolchain

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build_local -DCMAKE_BUILD_TYPE=Release
cmake --build build_local -j4
```

The flashable file is `build_local/fragments.uf2`.

The included source builds the standard 48 kHz firmware. The 24 kHz source
variant is included as `src/fragments_24k.cpp` and
`src/usb_descriptors_24k.c` for reference.

To flash the card, hold BOOTSEL while connecting USB-C, then copy the UF2 file
to the mounted `RPI-RP2` drive.

## License

Fragments is licensed under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International License. See
[LICENSE.md](LICENSE.md).
