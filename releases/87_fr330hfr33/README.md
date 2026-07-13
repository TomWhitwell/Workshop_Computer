# Fr330hfr33

Fr330hfr33 is a hardware-tested acid bass instrument for the Music Thing
Modular Workshop Computer. It combines a saw or square oscillator, resonant
diode-style filter, accent, glide, distortion, USB MIDI, and a persistent
16-step sequencer.

## Current release

Version `0.9.3`:

```text
uf2/Fr330hfr33-v0.9.3.uf2
```

SHA-256:

```text
8124a6a0b34d9cf393a4ba5973f10973f22485d5a8b22695c7e0dd81f9d2f17d
```

The release has passed functional, USB disconnect/reconnect, persistence,
extended-running, processor-stress, Swing, pitch-bend, sequencer-rest,
sustain-pedal, Intensity, and editor workflow hardware tests.

### New in 0.9.3

- Internal and MIDI-clock Swing with a `1.00:1`–`2.00:1` ratio display
- Standard ±2-semitone MIDI pitch bend
- Simplified Gate and Legato choices in the Web editor
- Per-step Mute for explicit sequencer rests
- Mutate Pattern for restrained one-to-three-step variations
- MIDI CC64 sustain-pedal support
- Volatile MIDI CC20 Intensity performance macro

## Playing the card

The three knobs retain the same jobs in all modes:

- **Main:** filter cutoff
- **X:** resonance
- **Y:** envelope decay and glide time

Main has a wide performance sweep. The lower range is dark and plucky, while
the upper half continues opening progressively rather than becoming fully
bright around noon. The filter can be switched between 24 dB/octave four-pole
and 18 dB/octave three-pole responses.

X moves from gentle body and nasal emphasis into strong resonance and
self-oscillation. Y moves from short, immediate notes to longer envelopes and
more pronounced slides.

### Switch up — CV and MIDI

- `CV In 1`: 1V/oct pitch
- `Pulse In 1`: gate
- `Pulse In 2` high: manual glide
- USB MIDI notes take priority while held
- MIDI velocity 112–127 triggers accent
- The MIDI pitch-bend wheel bends the voice and Pitch CV Out by ±2 semitones
- MIDI CC64 supports a standard sustain pedal
- MIDI CC20 controls the temporary performance Intensity

Overlapping MIDI notes create 303-style legato slides without retriggering the
envelope. The most recently played held note has priority.

The sustain pedal holds released MIDI notes until the pedal is lifted. Intensity
progressively opens the filter, strengthens its contour, adds a restrained
amount of resonance, and increases drive when distortion is already enabled.
It does not switch distortion on, is not saved, and returns to zero whenever
the card starts.

Accent makes the note louder and brighter. Closely spaced accents accumulate
filter-sweep energy, so a run of accents can rise higher than an isolated
accent.

In USB host mode, MIDI CC1—usually sent by a MIDI controller's mod wheel—
controls cutoff. The Main knob uses pickup: it resumes control only after
crossing the current CC1 position, avoiding a jump. CC1 does not control cutoff
in USB device mode.

If USB MIDI is interrupted, held MIDI state is cleared and the gate closes.
USB reconnection or a new physical gate restores normal control.

### Switch middle — sequencer

The sequencer can run as a scale-aware generator or as an editable pattern of
1–16 steps. It uses the internal tempo unless it receives an external clock on
`Pulse In 2`; optional MIDI clock sync is also available. Swing ranges from
straight timing to a strong 2:1 shuffle for the internal and MIDI clocks.
External pulse timing is followed exactly and is not altered by Swing.

Editable patterns have separate lanes for:

- Note or mute
- Octave
- Accent
- Gate length
- Tie

Patterns support Initial Step, forward, reverse, and pendulum playback.
Individual lanes can be randomized, with note choices taken from the selected
scale.

**Mutate Pattern** changes only one to three steps—usually notes, but
occasionally gate, octave, accent, or tie—so it creates a nearby variation
rather than replacing the pattern. Muted steps remain muted.

Four pattern slots and the general editor settings persist across power
cycles. In sequencer mode LED 3 identifies the active slot:

- One pulse: slot 1
- Two pulses: slot 2
- Three pulses: slot 3
- Four pulses: slot 4

The Acidness control coordinates melodic movement, octave jumps, gate length,
accent density, and slides. **Acidify Pattern** applies that character to the
editable pattern while keeping the result fixed until edited again.

### Switch down — battery pull

Holding the switch down simulates pulling the batteries from a running
TB-303. Gate, pitch, filter, resonance, and both audio outputs collapse through
an audible dying tail. Releasing the switch restores power smoothly.

## Sound options

The Web MIDI editor provides:

- Saw or square oscillator
- 24 dB or 18 dB diode-style filter
- Distortion off, RAT-style hard clipping, or Tube Screamer-style soft clipping
- Distortion amount and tone
- Scale, root, base octave, range, accent probability, gate length, and glide
- Internal tempo, swing, MIDI channel, and MIDI clock sync
- Pattern lanes with explicit rests, playback direction, Initial Step,
  pendulum mode, and slots
- Acidness and Acidify Pattern
- Mutate Pattern for restrained one-to-three-step variations

RAT and Tube Screamer modes contrast most clearly with the saw. The square is
already strongly rail-shaped, so the two clipping styles can sound similar,
although their tone controls remain effective.

The editor is split into Sound and Sequencer pages. Connection controls,
status, and Apply remain at the top of both pages.

## Connections and indicators

Outputs:

- `Audio Out 1`: enveloped, filtered, and optionally distorted voice
- `Audio Out 2`: phase-aligned raw selected oscillator
- `CV Out 1`: calibrated 1V/oct pitch
- `Pulse Out 1`: current gate
- `CV Out 2` and `Pulse Out 2`: unused

LEDs:

- LEDs 1, 3, and 5: CV/MIDI, sequencer, and battery-pull modes
- LED 2: gate
- LED 4: accent
- LED 6: note brightness; fully lit in 18 dB filter mode

## Web MIDI editor

[Launch the hosted Fr330hfr33 editor](https://tomwhitwell.github.io/Workshop_Computer/programs/87-fr330hfr33/web/index.html)

Use Chrome or Edge with Web MIDI and SysEx enabled. Connect with a USB-C data
cable, close other applications using the MIDI port, select **Fr330hfr33**, and
press **Apply**.

To serve the editor locally:

```sh
python3 -m http.server 5173 --directory web
```

Then open:

```text
http://localhost:5173
```

## Building

Prerequisites:

- CMake
- Arm GNU toolchain (`arm-none-eabi-gcc`)
- Raspberry Pi Pico SDK

Set the Pico SDK path and build:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The generated firmware is:

```text
build/Fr330hfr33.uf2
```

If `pico-sdk` and `303bass` are sibling folders, setting `PICO_SDK_PATH` is
optional.

The Web MIDI SysEx format is documented in
[`protocol.md`](protocol.md).
