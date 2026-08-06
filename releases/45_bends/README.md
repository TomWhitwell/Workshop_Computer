# Bends

6-Stage Stereo Multi-FX, Glitch Looper & Digital Degradation Engine

## Overview

Bends is a 6-stage stereo multi-effects processor, live stutter looper, and signal degradation engine designed for the Workshop Computer.

The module makes digital imperfection its core concept. Inspired by 90s circuit-bent rack units, Bends exposes the artifacts, memory corruption, and data bus shorting of early digital processors. It functions both as a traditional stereo multi-effects unit and as a self-contained generative sound source.

Bends spans a wide range of sound processing:
* **Clean stereo utility**: BBD chorus, pitch-glide delays, state-variable filters, and spring or hall reverbs.
* **Glitch textures**: Granular freeze pendulums, clock-synced breakbeat slicing, and shimmer pitch-shifting.
* **Digital corruption**: Wavefolder fuzz, bitwise BBD XOR data bus shorting, lossy MP3 compression, network packet dropouts, and CD sputter.

## Navigation & Switch Modes

The Z toggle switch controls module state:

| Gesture | Mode | Function |
| :--- | :--- | :--- |
| **Tap DOWN (<350ms)** | **Page Step** | Advances through pages: `1. Chorus` -> `2. Loss` -> `3. Delay` -> `4. Glitcher` -> `5. Filter` -> `6. Reverb`. |
| **Hold DOWN (>=350ms)** | **Global Mode** | Adjusts global bend, stereo width, and routing presets. |
| **Hold DOWN (3s)** | **Save Preset** | Saves current settings to flash memory (600ms strobe confirmation). |
| **Flick UP** | **Freeze Mode** | Locks recording memory into live granular freeze. |

## Program Pages

* **1. Chorus / Tape Loss**: Stereo BBD chorus, tape drift, Karplus-Strong string synthesis (Y > 68%), and bitwise XOR distortion (Y > 96%).
* **2. Loss Engine**: Morphs across Tape Drive, MP3 Ringing, Bitcrush Fuzz, Decimation, Tape Wow/Flutter, Packet Dropouts, and CD Sputter.
* **3. Multi-Tap Delay**: 16 rhythmic clock subdivisions and feedback XOR overdrive (>70%).
* **4. Glitcher**: Granular stutter looper with tempo-synced slice sizes, playback speed (-1.0x to +2.0x), and pitch mutation.
* **Freeze Mode (Switch UP)**: Playhead scrubbing (Auto LFO, Manual, or Random Skip) over locked RAM buffer.
* **5. Resonant Filter**: State-variable filter (Lowpass to Highpass) with self-oscillation (>75%) and wavefolder drive fuzz.
* **6. Reverb Engine**: Dual spring and hall reverb with damping, shimmer pitch-shifting (>73%), and circuit jitter (>82%).

## Suggested Patches

* **Standalone Glitch Drum Machine**: Unplug all audio inputs. Self-patch `Random CV` (`CV Out 2`) into `CV 1` to self-modulate pitch and Karplus-Strong string plucks, add delay on Page 3, and freeze the buffer.
* **Karplus-Strong String Sequencer**: On Page 1, set Y > 68%. Patch an external clock into `Clock Sync` (`Pulse 1`) to pluck the string while `Pitch CV` (`CV Out 1`) outputs a 1V/Oct Turing Machine sequence.
* **Clock-Synced Beat Slicing**: Send a clock into `Clock Sync` (`Pulse 1`). Delay repeats and looper slice sizes automatically snap to rhythmic subdivisions.
* **Live Freeze Scrubbing**: Flick Z switch UP to freeze live audio. Use Main knob to scrub through the buffer or set to Random Skip on clock pulses while Filter and Reverb continue processing.
* **Contact Mic Soundscapes**: Plug a contact mic into `Audio 1`, tap physical objects, and process through Loss, Delay, and Reverb to generate ambient textures.

## Reset Procedures

* **Preset Clean Reset**: In Global mode (Z Switch DOWN >=350ms), turn Knob Y fully clockwise (>96%) to restore clean defaults across all 6 pages.
* **Factory Hardware Reset**: Power on system while holding Z Switch DOWN (1s LED flash) to restore original factory settings.
