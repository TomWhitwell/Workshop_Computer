# DualQuantPitch

This Readme is LLM output

A dual granular pitch shifter for the Workshop Computer platform.

This project implements two independent fixed-point granular pitch shifters running entirely inside the audio ISR at 48kHz on RP2040 hardware.

The design intentionally embraces a rough, lo-fi granular texture rather than attempting transparent studio-quality pitch shifting.

---

# Features

* Dual independent pitch-shifted outputs
* Shared circular audio buffer
* Fixed-point DSP throughout
* No floating point operations inside the ISR
* No dynamic allocation
* ISR-safe implementation
* Optional chromatic pitch quantisation
* CV-controllable pitch shifting
* Granular overlap texture
* Interpolated sample playback
* Triangle grain windowing
* Pitch CV outputs
* LED feedback for quantiser state

---

# Design Goals

This module was designed around several constraints:

* Stable real-time ISR execution
* Minimal CPU overhead
* Predictable timing behaviour
* Low memory usage
* No blocking operations
* No expensive transcendental math
* Musically useful degradation

The goal is not perfect transparency.

Instead, the module intentionally produces:

* gritty textures
* grain smearing
* metallic transients
* chorus-like motion
* unstable granular character

This makes the module particularly suitable for:

* drones
* ambient processing
* experimental synthesis
* tape-like pitch artefacts
* glitch textures
* modular feedback systems

---

# Architecture

## Circular Buffer

Incoming audio is written into a shared 2048-sample circular buffer.

Both pitch shifters read from this buffer independently using fixed-point read heads.

Power-of-two buffer sizing allows efficient wrapping using bitmask operations instead of modulo arithmetic.

```cpp
static const int32_t BUFFER_SIZE = 2048;
static const int32_t BUFFER_MASK = BUFFER_SIZE - 1;
```

---

## Fixed-Point Format

The DSP engine uses Q16.16 fixed-point arithmetic.

* Upper 16 bits: integer component
* Lower 16 bits: fractional component

Examples:

| Value  | Meaning |
| ------ | ------- |
| 65536  | 1.0     |
| 32768  | 0.5     |
| 131072 | 2.0     |

Pitch values use Q8.8 fixed-point semitone representation.

---

## Granular Playback

Each output continuously reads through the delay buffer at a variable playback rate.

Read heads are periodically reset behind the write head to create overlapping grains.

This prevents uncontrolled drift while intentionally producing granular artefacts.

---

## Interpolation

Linear interpolation smooths playback between adjacent samples.

```cpp
sample = s1 + (((s2 - s1) * frac) >> FP_SHIFT);
```

64-bit intermediate multiplication is used to avoid overflow.

---

## Grain Window

Each grain uses a triangle amplitude envelope.

This reduces hard discontinuities at grain boundaries and helps minimise clicks.

The implementation uses shift operations instead of integer division for ISR efficiency.

---

# Controls

| Control                | Function                                 |
| ---------------------- | ---------------------------------------- |
| Main knob              | Global pitch offset                      |
| X knob                 | Pitch offset for output A                |
| Y knob                 | Pitch offset for output B                |
| CV input 1             | Additional pitch modulation for output A |
| CV input 2             | Additional pitch modulation for output B |
| Switch middle position | Enable chromatic quantisation            |

---

# Outputs

| Output      | Function                  |
| ----------- | ------------------------- |
| Audio Out 1 | Pitch-shifted signal A    |
| Audio Out 2 | Pitch-shifted signal B    |
| CV Out 1    | Quantised pitch voltage A |
| CV Out 2    | Quantised pitch voltage B |

---

# Quantiser

The quantiser uses fixed-point chromatic rounding.

Pitch values are represented in Q8.8 semitone format.

The implementation correctly handles negative semitone values.

---

# Playback Ratios

Pitch shifting uses a precomputed ratio lookup table.

This avoids expensive floating-point operations such as `powf()` inside the ISR.

Ratios are intentionally approximate and musically tuned.

---

# Performance

The audio ISR runs at 48kHz.

Design optimisations include:

* fixed-point arithmetic
* lookup tables
* power-of-two buffer wrapping
* shift-based envelope calculations
* no heap allocation
* no floating point
* no modulo operations in hot paths

The system clock is configured to 144MHz to provide stable DSP headroom.

---

# Audio Character

Expected sonic characteristics:

* rough granular texture
* transient smearing
* grain overlap modulation
* metallic artefacts
* pitch instability at extreme shifts
* chorus-like motion at smaller intervals

The implementation intentionally prioritises character over transparency.

---

# Safety Notes

The DSP engine includes:

* startup buffer initialisation
* bounded circular addressing
* overflow-safe interpolation
* saturated output clamping
* deterministic ISR execution

All processing is designed to remain ISR-safe.

---

# Future Improvements

Potential future enhancements:

* alternative grain windows
* equal-power overlap curves
* stereo grain decorrelation
* feedback paths
* freeze mode
* reverse grains
* pitch scale selection
* microtonal quantisation
* grain jitter modulation
* clock-synchronised grain resets

---



# Author

Created by Adrian Vos
Much thanks to Tom Whitwell for making all this possible, Chris Johnson for the ComputerCard framework and inspiration and everyone on the Workshop System Discord for the encouragement.
License MIT
