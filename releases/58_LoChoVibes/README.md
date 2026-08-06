# LoCho Vibes

LoCho Vibes is a stereo lo-fi chorus and vibrato effect for the Music Thing Modular Workshop Computer.

Inspired by the unstable movement and degraded character of the ZVEX Lo-Fi Junky, this card focuses on animated pitch movement, stereo widening, saturation, compression-style coloration, and degraded cassette-inspired modulation.

---

# Features

* Stereo chorus and vibrato modes
* Triangle, sine, and Random Drift LFO shapes
* Bipolar Character control
* Progressive lo-fi degradation
* Bit-crushing and saturation
* Soft limiting and compression coloration
* Stereo modulated delay architecture
* Stereo chorus widening
* External pulse-clockable LFO
* CV modulation inputs
* Dual animated CV LFO outputs

---

# Controls

| Control                 | Function                        |
| ----------------------- | ------------------------------- |
| Main                    | Modulation Rate                 |
| X                       | Modulation Depth                |
| Y                       | Character (Lo-Fi ↔ Compression) |
| Switch Up               | Vibrato Mode                    |
| Switch Middle           | Chorus Mode                     |
| Switch Down (momentary) | Cycle LFO Shape                 |

---

# Inputs & Outputs

| Input / Output | Function                       |
| -------------- | ------------------------------ |
| Audio In 1 + 2 | Mono summed audio input        |
| Audio Out 1    | Left audio output              |
| Audio Out 2    | Right audio output             |
| CV In 1        | Modulates modulation depth     |
| CV In 2        | Modulates Character control    |
| Pulse In 1     | External LFO clock input       |
| CV Out 1       | Main LFO modulation output     |
| CV Out 2       | Inverted LFO modulation output |

---

# External Clocking

LoCho Vibes supports external pulse clocking via Pulse In 1.

When a pulse source is connected:

* the internal LFO synchronizes to incoming clock pulses
* modulation speed follows pulse timing
* clock changes are smoothed internally
* automatic fallback to internal clock occurs if pulses stop

This allows synchronized chorus and vibrato movement from sequencers, clocks, trigger generators, and rhythm systems.

---

# CV Modulation

## CV In 1 — Depth Modulation

CV In 1 modulates the X control.

This affects:

* modulation intensity
* stereo width
* chorus movement
* vibrato depth

Incoming CV is attenuated internally for musical response.

---

## CV In 2 — Character Modulation

CV In 2 modulates the Y Character control.

This allows external movement between:

* degraded cassette textures
* neutral response
* compressed and saturated modulation

Slow modulation sources can create evolving texture shifts while faster CV creates animated tonal movement.

---

# CV Outputs

LoCho Vibes outputs the internal modulation waveform as CV.

## CV Out 1

Outputs the main LFO.

## CV Out 2

Outputs an inverted version of the same LFO.

Applications include:

* filter modulation
* stereo panning
* synchronized movement
* clocked animation
* opposing modulation sources

The outputs always reflect the selected waveform and clock state.

---

# Character Control


## Counter-Clockwise — Lo-Fi

Turning the control counter-clockwise progressively introduces:

* reduced signal level
* increasing tape-style saturation
* reduced resolution through bit reduction
* softened transients
* degraded cassette texture
* dirtier and grainier modulation


---

## Center Position — Neutral

At approximately 12 o'clock:

* minimal saturation
* no bit reduction
* no compression coloration
* cleanest modulation response

---

## Clockwise — Compression

Turning clockwise progressively introduces:

* increased input drive
* soft limiting
* stronger saturation
* transient compression
* makeup gain
* denser modulation texture

Higher settings create a louder, more forward sound that resembles heavily driven modulation pedals and compressed tape playback.

---

# Modes

## Chorus

In chorus mode:

* dry signal remains blended with the delay line
* stereo modulation widens the image
* modulation varies from restrained and subtle to over the top and chaotic.
---

## Vibrato

In vibrato mode:

* the effect becomes fully wet
* pitch modulation becomes significantly more obvious
* behaviour resembles unstable tape transport and warped cassette playback

At higher depth settings the result can become heavily seasick and degraded.

---

# LFO Shapes

## Triangle

Classic linear modulation.

Provides traditional chorus movement and predictable stereo animation.

---

## Sine

Smooth and fluid.

Useful for subtle pitch movement and gentle tape-style wobble.

---

## Random Drift

A combination of slow modulation and random wandering movement.
Modulation is affected by the depth knob and rate cotrol
* unstable tape transport behaviour
* slow wandering pitch drift
* imperfect mechanical motion
* less predictable stereo movement
* degraded cassette-style character

This mode is intended to emulate old tape machines that never move quite the same way twice.

---

# Stereo Behaviour

LoCho Vibes converts incoming audio to mono internally before creating a stereo modulation field.

The left and right channels use opposing delay modulation and offset delay times to create:

* stereo widening
* animated movement
* asymmetrical modulation
* tape-style instability

---

# LED Behaviour

## Shape Display Overlay

When the switch is tapped downward:

* the LFO shape changes
* LEDs briefly display the selected waveform
* normal LED operation returns automatically

| Shape        | LEDs     |
| ------------ | -------- |
| Triangle     | LEDs 1–2 |
| Sine         | LEDs 3–4 |
| Random Drift | LEDs 5–6 |

---

## Normal Display Mode

| LED   | Function                    |
| ----- | --------------------------- |
| LED 1 | Clock / Rate Indicator      |
| LED 2 | Modulation Activity & Depth |
| LED 3 | Lo-Fi Amount                |
| LED 4 | Compression Amount          |
| LED 5 | Shape Animation A           |
| LED 6 | Shape Animation B           |

### LED 1 — Clock Indicator

LED 1 displays modulation timing.

* Internal clock: flashes once per LFO cycle
* External clock: flashes on incoming pulse clocks

This provides immediate visual feedback of modulation speed and clock synchronization.

### LED 2 — Modulation Activity

LED 2 displays modulation movement and depth.

* brighter modulation indicates greater movement
* responds to both modulation depth and LFO activity
* makes modulation intensity visible even at slow rates

### LED 3 — Lo-Fi Amount

Displays the amount of lo-fi processing applied by the Character control.

* off at the centre position
* increases as the Character control is turned counter-clockwise

### LED 4 — Compression Amount

Displays the amount of compression and drive applied by the Character control.

* off at the centre position
* increases as the Character control is turned clockwise

### LEDs 5 & 6 — LFO Shape Animation

The final LED pair visualizes the currently selected modulation shape.

#### Triangle

A pendulum-style animation.

The LEDs move in opposite directions, reflecting the linear rise and fall of the triangle waveform.

#### Sine

A smooth breathing animation.

Both LEDs brighten and dim together, matching the rounded motion of the sine wave.

#### Random Drift

A wandering imbalance animation.

Brightness slowly shifts between the two LEDs, reflecting the unstable and imperfect movement of worn tape mechanisms.

---

LED brightness reflects both parameter values and modulation movement, providing continuous visual feedback during performance.

---

# Current Status

Actively under development and testing.
