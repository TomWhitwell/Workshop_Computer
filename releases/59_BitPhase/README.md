# BitPhase

BitPhase is a resonant 4-stage phaser for the Music Thing Modular Workshop Computer.

Inspired by the behaviour of classic feedback phasers such as the EarthQuaker Devices Grand Orbiter, BitPhase combines deep notch filtering, wide modulation sweeps, controllable resonance, tremolo blending, and a digital Burst mode for degraded and unstable textures.

At low settings it provides subtle movement and phase animation. At higher resonance settings it produces pronounced notch emphasis and can approach self-oscillation for synth-like tones and feedback sweeps.

---

## Features

* 4-stage all-pass phaser engine
* Variable resonance and feedback
* Edge-of-self-oscillation behaviour
* Independent phaser and tremolo LFOs
* Pulse-syncable phaser sweep
* Three operating modes
* Burst-mode digital degradation
* CV control of rate and resonance
* Dual CV outputs
* Dual pulse outputs

---

## Controls

### Main Knob — Rate

Controls the speed of both internal LFOs.

#### Phaser LFO

* Approx. 0.05 Hz – 6 Hz
* Exponential response for slower low-speed sweeps

#### Tremolo LFO

* Approx. 0.5 Hz – 20 Hz
* Tracks the Rate control independently of the phaser sweep

---

### X Knob — Sweep Depth

Controls phaser modulation depth.

Low settings:

* Narrow notch movement
* Subtle phase animation

High settings:

* Wide frequency sweeps
* Deep moving notches
* More dramatic modulation

---

### Y Knob — Resonance

Controls phaser feedback amount.

Low settings:

* Smooth phasing
* Gentle notch emphasis

Mid settings:

* Stronger resonance
* More pronounced phaser character

High settings:

* Aggressive feedback
* Sharper notches
* Edge-of-self-oscillation behaviour

At maximum settings the phaser can approach self-generated tones depending on sweep position and input material.

---

## CV Inputs

### CV In 1 — Rate CV

Modulates phaser and tremolo speed.

Useful for:

* Clock-derived sweeps
* Envelope-controlled rate changes
* Random modulation

---

### CV In 2 — Resonance CV

Modulates phaser feedback amount.

Useful for:

* Dynamic resonance changes
* Envelope-controlled emphasis
* Oscillation bursts
* Performance modulation

---

## Modes

### ▲ Up — Phaser

Pure resonant phaser.

Features:

* 4-stage all-pass network
* Resonant feedback path
* Wide sweep capability
* Cleanest signal path

Ideal for:

* Traditional phasing
* Resonant sweeps
* Feedback textures

---

### ● Middle — Mix

Blends phasing with tremolo.

Features:

* Phaser output
* Tremolo-modulated phaser layer
* Equal blend of both signals

Produces:

* Animated stereo-style movement
* Pulsing modulation
* More rhythmic textures

---

### ▼ Down — Burst

Digital degradation mode.

Adds:

* Sample-and-hold reduction
* Bit-depth reduction
* Random repeat corruption
* Increased instability

Burst mode operates on the resonant phaser signal and can create highly animated lo-fi textures while retaining the core phaser character.

---

## Burst Degradation System

Burst mode introduces controlled digital corruption.

### Sample & Hold

Temporarily freezes sample updates.

### Bit Reduction

Applies progressive bit masking.

### Repeat Corruption

Randomly repeats previous samples.

### Presence Compensation

Additional dry reinforcement prevents complete collapse of the signal during extreme degradation.

---

## Synchronisation

### Pulse Input

Resets the phaser LFO.

Each rising edge:

* Resets phaser sweep position
* Synchronises modulation cycles
* Enables clock-locked phase movement

The tremolo LFO continues independently.

---

## Outputs

### Audio Out 1 / Audio Out 2

Dual mono output of the processed signal.

---

### CV Out 1

Phaser LFO waveform.

---

### CV Out 2

Tremolo LFO waveform.

---

### Pulse Out 1

High while Burst mode is active.

---

### Pulse Out 2

Phaser LFO phase indicator.

Useful for synchronising external modulation events.

---

## LEDs

* LED 1 — Rate position
* LED 2 — Phaser LFO position
* LED 3 — Sweep position
* LED 4 — Burst intensity indicator
* LED 5 — Burst mode status
* LED 6 — Pulse input monitor

---
