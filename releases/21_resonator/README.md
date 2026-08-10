# Resonator

A sympathetic resonator workshop card originally inspired by the Mutable Instruments Rings module and the tanpura.
It has four resonating Karplus-Strong strings that, when excited, create rich, harmonic textures.
It also has a range of options for pulse/CV I/O, including arpeggios, pitch detection and envelope followers.

## Description

The sympathetic resonator simulates the behavior of strings that vibrate in response to external excitation. When you feed an audio signal into the module, it excites four virtual strings tuned to different harmonic relationships. This creates a shimmering, resonant effect similar to the sound of sympathetic strings on instruments like the sitar or tanpura.

## How It Works

- Each string is implemented as a delay line with feedback
- A lowpass filter in the feedback path simulates damping (energy loss)
- The delay time determines the pitch of each string
- Input signals excite the strings, which then resonate at their tuned frequencies

## Features

- 18 chord and tanpura voicings with an editable, persistent progression
- Arpeggiator synced to the chord tempo (up / down / up-down / random / pedal / random walk), with optional continuous loop
- YIN pitch tracking with 1V/oct CV output
- Envelope followers, Schmitt-trigger and onset transient detectors, tap-tempo clock and clock divider
- Fully configurable CV, pulse, and audio I/O — saved on the module
- Stereo mid/side audio output
- Browser-based editor over USB (Web Serial)

## Controls

### Knobs
- **X Knob**: Base frequency (fundamental pitch) of the resonator
- **Y Knob**: Damping amount (higher = more resonance/longer decay)
- **Main Knob**: Dry/wet mix (0 = dry signal, full = wet resonator output)

### Switch
- **Up**: Tuning mode — only the fundamental string sounds
- **Middle**: Normal operation
- **Down (momentary)**: Advance to next chord in progression
- **Down (hold 3 seconds)**: Reset to factory defaults (all 18 chords)

## Inputs & Outputs

The Workshop System Computer has two of each jack. Most are configurable from the **i/o** tab of the web editor; the default mode is listed first. All settings persist on the module.

### Audio
- **Audio In 1 / In 2**: Excitation sources, summed to feed the strings. Pitch tracking listens to **Audio In 1 only**.
- **Audio Out 1**: Main resonator mix (the "mid" of a mid/side stereo pair with Audio Out 2).
- **Audio Out 2**: *audio* (side channel for stereo) · *resonator envelope* · *input envelope*. The envelope modes turn it into a CV output instead of audio.

### CV Inputs
- **CV In 1**: *1V/oct pitch* (the X knob becomes a ±1-octave fine tune / transpose when patched) · *arpeggio root only* (CV sets the pitch of the arpeggio/root/pitch CV outputs while the strings stay at the X-knob pitch).
- **CV In 2**: *damping* (adds to the Y knob) · *wet/dry mix* (adds to the Main knob).

### CV Outputs

Both outputs share the same modes (defaults: CV Out 1 = arpeggio pitch, CV Out 2 = input envelope):

| Mode | Output |
|------|--------|
| Arpeggio pitch | Rotating chord tones as 1V/oct (see [Arpeggio](#arpeggio)) |
| Root pitch | A selectable chord tone's pitch as 1V/oct (root by default; pick 1st–4th tone in the editor) |
| Resonator envelope | Resonator output amplitude as CV |
| Input envelope | Input amplitude as CV |
| Random S&H | New random voltage on each chord change |
| Pitch S&H | Samples the pitch CV on a Pulse In 1 trigger, holds until the next |
| Pitch track | YIN pitch detection (Audio In 1) as 1V/oct |

**1V/oct:** All pitch CVs — pitch track, root, arp, pitch S&H, and CV In 1 — share one anchor, **middle C (C4) = 0 V**, at the standard 1 V/octave. So every pitch output reads the same voltage for a given note, and pitch track patched back into CV In 1 (self-tracking) or into another resonator's CV In 1 tracks at the correct octave (the receiving X knob acts as a ±1-octave transpose). Because CV In 1 is centered on middle C, the full C1–C7 string range spans roughly ±3 V: a bipolar source reaches the whole range, while a unipolar 0 V+ source covers middle C and up.

### Pulse Inputs
- **Pulse In 1**: *noise burst / pluck* · *advance chord* · *reset to first chord*.
- **Pulse In 2**: *advance chord* · *advance arp step* · *noise burst / pluck* · *reset to first chord*.

### Pulse Outputs
- **Pulse Out 1**: *audio trigger* (Schmitt trigger on input transients) · *tap tempo clock* · *arpeggio clock* · *onset detect*.
- **Pulse Out 2**: *chord change trigger* · *tap tempo clock* · *audio trigger* · *arpeggio clock* · *clock divider* (divides Pulse In 2 by 2/3/4/8) · *onset detect*.

The tap-tempo clock free-runs at the rate of your last two chord changes, while the chord-change trigger fires once per actual change. Onset detect responds to note attacks in sustained or layered audio (~100 ms minimum spacing).

### Arpeggio
When a CV output is set to *arpeggio pitch*, it steps through the current chord's tones, locked to the chord-change tempo. The pattern (up / down / up-down / random / **pedal** — root between each tone / **random walk** — steps ±1 tone) and the number of steps per chord (1, 2, 4, or 8) are set in the editor and shared by both CV outputs. By default it plays one sweep per chord and holds; enable **loop** to keep it cycling continuously on a held chord.

## Chord Modes

18 chord voicings are available:

- **HARMONIC**: Harmonic series - 1:1, 2:1, 3:1, 4:1
- **FIFTH**: Stacked fifths - 1:1, 3:2, 2:1, 3:1
- **MAJOR**: Major triad - 1:1, 5:4, 3:2, 2:1
- **MAJOR6**: Major 6th - 1:1, 5:4, 3:2, 5:3
- **MAJOR7**: Major 7th - 1:1, 5:4, 3:2, 15:8
- **DOM7**: Dominant 7th - 1:1, 5:4, 3:2, 9:5
- **ADD9**: Major add 9 - 1:1, 5:4, 3:2, 9:4
- **MAJOR10**: Major 10th - 1:1, 5:4, 3:2, 5:2
- **MINOR**: Minor triad - 1:1, 6:5, 3:2, 2:1
- **MINOR7**: Minor 7th - 1:1, 6:5, 3:2, 9:5
- **MIN9**: Minor add 9 - 1:1, 6:5, 3:2, 9:4
- **DIM**: Diminished - 1:1, 6:5, 36:25, 3:2
- **SUS2**: Suspended 2nd - 1:1, 9:8, 3:2, 2:1
- **SUS4**: Suspended 4th - 1:1, 4:3, 3:2, 2:1

### Tanpura Tunings
- **TANPURA PA**: Sa, Pa, Sa', Sa'' - 1:1, 3:2, 2:1, 4:1
- **TANPURA MA**: Sa, Ma, Sa', Sa'' - 1:1, 4:3, 2:1, 4:1
- **TANPURA NI**: Sa, Ni, Sa', Sa'' - 1:1, 15:8, 2:1, 4:1
- **TANPURA NI KOMAL**: Sa, ni, Sa', Sa'' - 1:1, 9:5, 2:1, 4:1

## Web Editor

A browser-based editor with two tabs: **chords** for building the progression, and **i/o** for configuring the inputs and outputs above.

### Using the Editor
1. Connect your Resonator via USB
2. Open the editor at [johaneklund.io/resonator](https://johaneklund.io/resonator)
3. Click **Connect USB** and select `Pico` (may appear as `ttyACM0` on Linux)
4. On the **chords** tab, drag chords from the palette to build your progression, then click **Send to Device**
5. On the **i/o** tab, choose modes for each jack, then click **Save to device**

Changes persist on the module even after power off.

### Requirements
- Chrome or Edge browser (Web Serial API required)
- USB connection to Resonator

## LED Indicators

The 6 LEDs show the current position in the chord progression (0-17). Single LEDs indicate positions 0-5, LED pairs indicate positions 6-17.

## Building

```bash
mkdir build && cd build
cmake ..
make
```

This generates `resonator.uf2` in the `build/` directory.

## Flashing

1. Hold BOOTSEL on the Pico while connecting USB
2. Copy `resonator.uf2` to the mounted drive
3. The Pico will reboot automatically

## References

- [Mutable Instruments Rings Source Code](https://github.com/pichenettes/eurorack/tree/master/rings)
- [Tanpura](https://en.wikipedia.org/wiki/Tanpura)
- [Karplus-Strong Synthesis](https://en.wikipedia.org/wiki/Karplus%E2%80%93Strong_string_synthesis)
