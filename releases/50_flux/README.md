# Flux

Effects Processor, Synthesizer and Utility Card for the Music Thing Workshop Computer

It started as a port of PiPicoFX (https://github.com/StoneRose35/cortexguitarfx/tree/rp2040_v1). I modified it to be in stereo and added more effects (Shimmer Reverb, Lossy Audio, Resonator, etc.). Then I realized there was another core on the Pico, so I added a synthesizer, and then another one and another one, and some samplers. I also ported a part of AMY (https://github.com/shorepine/amy/tree/main) (piano and FM synth). Now I only had to figure out what to do with the CV and pulse outs - they got configurable MIDI to CV, an LFO two sequencers, and a port of Mutable Instruments Grids.

This is a bit of a packed card and that doesn't make the UI very straight forward, so I included optional voice annoucements. To simplify it, the card can also be locked, so the effects and synths can't be changed. This together with a lot of other settings, Sample Management, Midi mapping (yes Flux is 4 voice polyphonic over midi) and the output config can be done in the web UI:
https://vincentmaurer.de/flux/flux_manager.html


## Basic Operation

The switch controls what the knobs do:

| Switch Position | Mode | Main Knob | Knob X | Knob Y |
| :--- | :--- | :--- | :--- | :--- |
| **UP** | **Synth** | Envelope (Decay) | Pitch | Timbre / Color |
| **MIDDLE** | **Effect** | Wet/ Dry | Parameter 1 (Time, Decay, etc) | Parameter 2 (Tone, Feedback etc) |
| **DOWN (Hold)** | **Select Effect/ Synth** | Change Effect | Change Synth | Extra Synth Parameter  |
| **DOUBLE DOWN (Hold)** | **Performance** | Internal Tempo | Parameter 1 (Sequence A Length, Randomness) | Parameter 2  (Sequence B Length, Randomness)|

Audio 1 & 2 In: Either audio (mono or stereo) in or synth pitch and timbre/ secondary pitch/ external audio
CV 1 & 2 In: Effects X and Y parameter modulation
Pulse 1 & 2 In: Synth Gate and clock source for utilities

Audio ! & 2 Out: Stereo audio
CV 1 & 2 Out: Cofigurable
Pulse 1 & 2 Out: Cofigurable

---

## Synth Engines
Most synths use: **X = Pitch**, **Y = Timbre**, **Main = Envelope**.
Pulse 1 (and optionally 2) trigger the synth. Can also be polyphonically triggered with midi.

0. **External Input**: Passthrough external audio input.
1. **Wavetable**: Morphing Triangle ➔ Saw ➔ Square ➔ Pulse.
2. **Virtual Analog**: Osc stack through a resonant filter. **Y** = Cutoff.
3. **Strings**: Karplus-Strong physical modeling. **Y** = Stiffness/Bowing.
4. **Piano**: Additive synthesis spectral piano. **Y** = Brightness.
5. **Modal**: Metallic bells and percussion. **Y** = Harmonic structure.
6. **FM Synth**: 6-op DX7 style engine. **Main** = Env Speed, **Y** = Mod Depth.
7. **Noise**: White noise through a LPG. **Y** = Filter.
8. **Sampler One-Shot**: Triggered samples from flash. **Main** = Decay, **X** = Pitch, **Y** = Sample Select.
9. **Sampler Loop**: Looping samples. **Main** = Decay, **X** = Pitch, **Y** = Sample Select.
10. **Sampler Player**: Looping Player. **Main** = Length/Dir, **X** = Pitch, **Y** = Start.
11. **Sampler Drums**: 4-voice sample kit. Trigger via Pulse 1/2 or Audio L/R. **Y** = Kit select.
12. **Granular**: Shared grain pool. **Main** = Density, **X** = Pitch, **Y** = Position.
13. **Drum Synth**: Synthesized drum sounds. **Main** = Kick, **X** = Snare, **Y** = OH/ CH.


---

## Effects
Most effects map as: **Knob 1 (Main) = Mix/Blend**, **Knob 2 (X) = Param 1**, **Knob 3 (Y) = Param 2**.

| Category | Effects |
| :--- | :--- |
| **No Effect** | X Knob controls input/ synth volume |
| **Dynamics** | Compressor, Equalizer |
| **Amps** | Guitar Amp |
| **Modulation** | Tremolo, Sine Chorus, Vibrato Chorus, Bitcrusher |
| **Pitch** | Pitch Shift, Frequency Shift Delay|
| **Delays** | Digital Delay, Tape Delay, Ping Pong, Tape Loop, Echoverb|
| **Reverbs** | Plate Reverb, Spring Reverb, Freeverb, Shimmer Reverb, Cathedral Reverb, Granular Clouds, Basic Reverb, Allpass Reverb, Hadamard Reverb |
| **Experimental** | Micro Looper, Tape Degradation, Lossy Audio, Spectral Freeze, Oil-Can Echo, Resonator, Wind, Chatter |

---

## Utilities
Flux includes various utility functions for CV and Pulse outputs, accessible via the web UI configuration.

### CV Outputs
*   **MIDI Pitch/Velocity/CC**: Map MIDI data to CV.
*   **Synth/Internal ADSR**: Envelope outputs from synth or internal sources.
*   **Random S&H**: Sample and hold random voltages.
*   **LFO Utility**: Low-frequency oscillator with multiple shapes (Sine, Triangle, Saw, Square, Smooth Random).
*   **Step Sequencer A/B**: 16-step CV sequencers with scale quantization.
*   **Generative Seq A/B**: Algorithmic sequencers.
*   **Clock**: Clock division outputs.
*   **Voice Preview**: Preview synth voices.

### Pulse Outputs
*   **MIDI Gate/Trigger**: MIDI note gates and triggers.
*   **Clock Out**: Clock pulses with divisions.
*   **Voice Audio (PWM)**: Pulse-width modulated audio from synth voices.
*   **Seq A/B Gate**: Gates from step sequencers.
*   **Gen Seq A/B Gate**: Gates from generative sequencers.
*   **Grids (Drum Map)**: Rhythm generator for drums based on Mutable Instruments Grids.

---


## Web UI (Flux Manager)
Connect via USB and visit the **Flux Manager** to:
*   **Configure CV/Pulse I/O**: Map MIDI to CV, set Clock divisions, or use the Step Sequencer.
*   **Manage Samples**: Drag and drop WAV files to create custom kits for the Samplers and Drums.
*   **Save and share settings**: Export settings as patch cards and build custom cards
---

Created for the Music Thing Modular Workshop System by Vincent Maurer (https://github.com/vincent-maurer/) with assistance from Google Gemini.

Thank you to everyone on the Workshop System Discord server, that helped testing and especially to Chris Johnson (https://github.com/chrisgjohnson) for the ComputerCard library and the great support.
