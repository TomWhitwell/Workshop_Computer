# 18 Chord Organ-ish

Chord Organ-ish for the Music Thing Modular Workshop Computer. Inspired by the behaviour of the standalone Chord Organ module: 16 chords, up to 8 voices per chord, 1V/oct root control, and glide.

## Controls

| Input | Function |
|-------|----------|
| **Main knob** | Chord selection (0–15). Summed with CV 1. |
| **CV 1** | Chord CV (0V to +5V): added to Main knob for voltage-controlled chord selection. Negative voltages ignored. |
| **X knob** | Root transpose (semitones). |
| **Y knob** | Progression pattern selection (0–8). |
| **CV 2** | Root note (1V/oct). |
| **Audio 1** | VCA control: 0V to +5V controls output volume (0% to 100%). Full volume when disconnected. |
| **Pulse 1** | Trigger: advances progression step and retriggers chord/reset pulse. |
| **Pulse 2** | Cycle waveform (sine → triangle → square → saw) on each trigger. |
| **Switch Up** | Enable glide (portamento). |
| **Switch Middle** | Disable glide. |
| **Switch Down** | Cycle waveform (sine → triangle → square → saw). |

## Outputs

| Output | Function |
|--------|----------|
| **Audio 1 & 2** | Mixed chord (same on both). |
| **CV 1** | Highest note in current chord (1V/oct, calibrated). |
| **CV 2** | Sequenced root note from selected progression (1V/oct, calibrated). |
| **Pulse 1** | Brief pulse on chord or root change (or Pulse 1 in rising edge). |
| **LEDs 0–3** | Chord index in binary (0–15). |
| **LED 4** | Reset indicator (on while reset pulse is high). |
| **LED 5** | Waveform index (brightness). |

## Chord list (built-in)

1. Major  
2. Minor  
3. Major 7th  
4. Minor 7th  
5. Major 9th  
6. Minor 9th  
7. Suspended 4th  
8. Power 5th  
9. Power 4th  
10. Major 6th  
11. Minor 6th  
12. Diminished  
13. Augmented  
14. Root
15. Sub octave
16. 2 up 1 down octaves

## Progression Patterns

The chord progression sequencer outputs root notes via CV Out 2. Select patterns with Knob Y, advance steps with Pulse In 1.

0. **I-IV-V-I** - Classic pop/rock progression (C-F-G-C)
1. **I-V-vi-IV** - Popular progression (C-G-Am-F)
2. **ii-V-I** - Jazz turnaround (Dm-G-C)
3. **I-vi-IV-V** - 50s progression (C-Am-F-G)
4. **I-IV-I-V** - Blues progression (C-F-C-G)
5. **vi-IV-I-V** - Sensitive progression (Am-F-C-G)
6. **I-bVII-IV-I** - Mixolydian (C-Bb-F-C)
7. **Chromatic** - Ascending chromatic scale (C-C#-D-D#-E-F-F#-G)
8. **Circle of 5ths** - Circle of fifths sequence (C-G-D-A-E-B-F#-C#)

**Note:** Root offsets are relative to the current root (CV In 2 + Knob X). Chord type is still controlled manually via Main Knob.

## Build

Requires Pico SDK and (optionally) `PICO_SDK_PATH` set.

**Toolchain:** Arm GCC **15.x** can fail with `cannot find -lg` when linking the Pico SDK boot stage. Use one of:

- **Arm GNU Toolchain 13.2 or 14.2** from the main downloads page (older releases) — these usually still ship the expected newlib layout.

Install one of the above, put its `bin` directory first in your `PATH`, then build.

```bash
cd 18_chord_organ
mkdir build && cd build
cmake ..
make
```

Copy `chord_organ.uf2` from the Build folder to the Workshop Computer when in bootloader mode.

## Firmware

- **Platform:** Pico SDK (C++), ComputerCard
- **Sample rate:** 48 kHz
- **Features:** Glide (50 ms), 4 waveforms, 9 progression patterns
