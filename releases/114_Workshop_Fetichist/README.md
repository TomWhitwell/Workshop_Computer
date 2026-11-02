# Workshop Fetichist

**Workshop Fetichist** is a harsh, semi-random drum machine and noise processor for the Music Thing Modular Workshop Computer (hardware revision 1.1). Inspired by the Body Synths Metal Fetishist, it combines a looping random CV sequencer, probability-based trigger skipping, white noise, external audio processing and two aggressive effects.

The sequencer can also be used on its own as a source of random control voltage and rhythmic gates.

- Firmware: **v1.0.0**
- Creator: **ikkini**
- Source code: [github.com/ikkini1/workshop-fetichist](https://github.com/ikkini1/workshop-fetichist)

## Installation

1. Download the `.uf2` firmware file from this release folder.
2. Connect the Workshop Computer to your computer in bootloader mode.
3. Copy the UF2 file to the mounted **RPI-RP2** drive.
4. Wait for the board to reboot, then insert/load the Program Card as usual.

The firmware is designed for Workshop Computer hardware revision 1.1.

## Quick start

1. Set the Z switch to **Middle (Play)**.
2. Leave Pulse In 1 unpatched to use the internal clock.
3. Turn Main fully counter-clockwise for a new random CV value on every step.
4. Turn Y to introduce skipped steps. Played steps appear at Pulse Out 1; skipped steps appear at Pulse Out 2.
5. Patch CV Out 1 to a destination for the random sequence.
6. Patch Audio Out 1 to your mixer. With no external audio connected, the output is the internal white noise shaped by the crossfader and effects.
7. Move Z to **Down** to adjust the effects and internal tempo while the sequencer keeps running.

## Controls

### Z switch

- **Up — Stop:** stops the sequencer, clears the trigger outputs and envelope.
- **Middle — Play:** runs the sequencer. Main controls random/loop behaviour, X controls the crossfader CV amount and Y controls skip probability.
- **Down — Effects / Tempo:** the sequencer continues running. Main controls effect dry/wet, X blends Rust and Corrosion, and Y sets the internal tempo from 20 to 300 BPM.

The effects settings are retained when leaving and returning to Down. The sequencer controls are kept separate from the effects controls, with pickup when returning to Play.

### Main

In Play mode:

- Fully counter-clockwise: generate a fresh random value at every step.
- Above the minimum position: create a random loop between 2 and 32 steps. Changing the requested loop length creates a new random loop.

In Down mode, Main adjusts the effects dry/wet amount.

### X

In Play mode, X attenuates the sequencer CV used internally to control the crossfader. This lets the sequence move the mix between the external audio input and white noise.

In Down mode, X blends the two effects: Rust at one end, Corrosion at the other.

### Y

In Play mode, Y sets the probability of skipping a step, from 0 to 95%.

In Down mode, Y sets the internal tempo from 20 to 300 BPM. The skip setting is preserved while adjusting tempo.

## Clocking

The clock source priority is:

1. USB MIDI Clock
2. Pulse In 1
3. Internal clock

USB MIDI Clock follows standard MIDI timing (24 clock ticks per quarter note). The sequencer advances every 6 MIDI ticks in normal speed and every 3 ticks when double speed is enabled.

With no MIDI clock and no cable in Pulse In 1, the internal clock runs at the tempo set by Y in Down mode (20–300 BPM).

- **Pulse In 1:** external clock input. One incoming pulse is treated as a quarter note; the sequencer generates four steps per pulse interval.
- **Pulse In 2:** gate input for double speed. A high gate doubles the step rate.

## Inputs

| Input | Function |
| --- | --- |
| Audio In 1 | External audio input, summed with Audio In 2. |
| Audio In 2 | Second external audio input, summed with Audio In 1. The white-noise generator remains active independently. |
| CV In 1 | Controls the main audio VCA. If unpatched, the VCA is fully open. |
| CV In 2 | Overrides the internal crossfader CV. |
| Pulse In 1 | External clock input. |
| Pulse In 2 | Gate input for double-speed operation. |

## Outputs

| Output | Function |
| --- | --- |
| Audio Out 1 | Main output: external audio / white-noise crossfade, followed by effects and the VCA. |
| Audio Out 2 | Independent white-noise output, taken before the crossfader and VCA. |
| CV Out 1 | Sequencer CV, updated on every step, including skipped steps; range 0–5 V. |
| CV Out 2 | Exponential AD envelope, triggered by played steps; range 0–5 V. |
| Pulse Out 1 | 5 ms trigger for a played step. |
| Pulse Out 2 | 5 ms trigger for a skipped step. |

## Sequencer behaviour

Each step generates a random value. At the minimum Main setting, a new value is generated at every step. Turning Main up selects a fixed random loop from 2 to 32 steps; selecting a different loop length generates a new loop.

Y controls the probability of skipping a step, up to 95%. A played step sends a trigger to Pulse Out 1 and triggers the AD envelope on CV Out 2. A skipped step sends a trigger to Pulse Out 2 and does not trigger the envelope.

CV Out 1 is updated on every step, whether the step is played or skipped.

## Audio path

Audio In 1 and Audio In 2 are summed, with each input reduced by 30% before summing. A white-noise source runs independently and is also available directly at Audio Out 2.

The crossfader blends the external audio mix with white noise:

- With CV In 2 unpatched, the sequencer CV attenuated by X controls the crossfade.
- With CV In 2 patched, the external CV takes control of the crossfade.

The crossfaded signal passes through the effects section and then the VCA. CV In 1 controls the VCA; when it is not patched, the VCA remains fully open.

## Effects

The effects are adjusted in Z Down mode:

- **Rust:** bitcrushing, downsampling, sample jitter and hard clipping.
- **Corrosion:** aggressive saturation.
- **Main:** dry/wet amount.
- **X:** blend between Rust and Corrosion.
- **Y:** internal tempo.

The effects are placed after the crossfader and before the VCA.

## Source code and licence

The complete source code and build instructions are available at [github.com/ikkini1/workshop-fetichist](https://github.com/ikkini1/workshop-fetichist).

Workshop Fetichist is distributed under the MIT License. See the [LICENSE](https://github.com/ikkini1/workshop-fetichist/blob/main/LICENSE) file in the source repository.
