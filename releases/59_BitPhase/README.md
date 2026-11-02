# BitPhase

BitPhase is a resonant 4-stage phaser for the Music Thing Modular Workshop Computer.

Start with gentle phasing, turn up the resonance for deep moving notches, or
switch to Burst for unstable digital textures. BitPhase takes inspiration from
classic feedback phasers such as the EarthQuaker Devices Grand Orbiter, with
wide sweeps, tremolo blending and feedback that can approach self-oscillation.

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

Turn X up to widen the phaser sweep. Low settings give narrow notch movement
and subtle phasing; high settings produce deep moving notches and more dramatic
frequency sweeps.

---

### Y Knob — Resonance

Y sets the feedback. Start low for smooth phasing and gentle notch emphasis,
then turn it up for stronger resonance. High settings sharpen the notches and
push the feedback towards self-oscillation.

At maximum settings the phaser can approach self-generated tones depending on sweep position and input material.

---

## CV Inputs

### CV In 1 — Rate CV

Modulates phaser and tremolo speed. Try clock-derived voltages, an envelope or
a random voltage to vary the rate.

---

### CV In 2 — Resonance CV

Modulates the phaser feedback. Use an envelope for changing emphasis, or push
the resonance up briefly for bursts of near-oscillation while you play.

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

Burst combines three ways of breaking up the phaser signal, with extra dry
signal to keep the result audible:

### Sample & Hold

Temporarily freezes sample updates.

### Bit Reduction

Applies progressive bit masking.

### Repeat Corruption

Randomly repeats previous samples.

### Presence Compensation

Adds dry signal so the sound does not disappear under extreme degradation.

---

## Synchronisation

### Pulse Input

Resets the phaser LFO.

Each rising edge:

* Resets phaser sweep position
* Synchronises modulation cycles
* Enables clock-locked phase movement

The tremolo LFO keeps running independently of these resets.

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

## Attribution

BitPhase uses the ComputerCard hardware framework by Chris Johnson. The
framework's original notice is retained in `ComputerCard.h`.

---

## Firmware distribution notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
copyright and license texts. Keep the accompanying `NOTICE.txt` with
each UF2 when redistributing it.
