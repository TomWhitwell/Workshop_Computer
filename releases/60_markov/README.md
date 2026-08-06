# Markov

**Dual generative Markov chain module for the Music Thing Modular Workshop Computer.**

Two independent Markov chain engines run in parallel — a melodic generator on the left side and a rhythmic percussion generator on the right — driven by a shared clock. An internal synth voice combines both into a self-contained generative instrument.

---

## Overview

A Markov chain is a probabilistic state machine: the next state depends only on the current state, weighted by a transition probability table. Each engine has several pre-programmed *profiles* — curated probability matrices that give each one a distinct musical character. Over time, the chains wander through their state spaces in ways that feel intentional but never exactly repeat.

**MarkoV** (melody) has 10 states representing scale degrees. The current state maps to a pitch, quantised to a selected scale and transposed by CV and knob.

**MarkovPerc** (percussion) has 8 states representing hit types: rests, plain hits, accented hits, flams (grace note + main hit), and ratchets (2, 3, or 4 evenly spaced triggers per clock pulse). The percussion engine drives Pulse Out 2 with the appropriate sub-trigger pattern each clock cycle, and also gates the internal synth voice.

---

## I/O

| Jack | Direction | Function |
|------|-----------|----------|
| **Pulse In 1** | Input | Master clock. Rising edge advances both chains. Falls back to CV In 2 internal clock after 3 seconds with no pulse. |
| **CV In 1** | Input | Post-scale transpose ±12 semitones. Shifts the already-quantised pitch by whole semitones. Bipolar; 0 V = no shift. Inactive while Switch DOWN is held. |
| **CV In 2** | Input | Internal tempo. Active only when Pulse In 1 is absent. 0 V = 120 BPM; range ≈ 20–220 BPM. |
| **Pulse Out 1** | Output | Gate pulse (10 ms) on every melody step where the quantised pitch changes. |
| **CV Out 1** | Output | Pitch CV for the MarkoV melody. V/Oct, hardware-calibrated. |
| **Pulse Out 2** | Output | Trigger for MarkovPerc hits. Fires multiple times per clock for ratchets and flams. |
| **CV Out 2** | Output | Accent CV for MarkovPerc. 0 V = rest/silent; +5 V = full accent. 5 levels. |
| **Audio Out 1** | Output | Internal synth voice — square-wave oscillator pitched by MarkoV, shaped by a long decaying envelope and gated by MarkovPerc hits. In Dual Melody mode: voice A synth only. |
| **Audio Out 2** | Output | Unused in primary mode. In Dual Melody mode: voice B synth (independent). |

---

## Controls

### Knob X — Melody Profile

Selects one of five MarkoV transition profiles. Fully CCW = profile 0, fully CW = profile 4.

| Zone | LEDs | Profile | Character |
|------|------|---------|-----------|
| 0 | `OO`<br>`OO`<br>`OO` | **Pentatonic Stability** | Root and fifth are strong attractors. Grounded and tonal. |
| 1 | `XO`<br>`OO`<br>`OO` | **Chromatic Tension** | Stepwise motion dominates (±1 state). Snake-like lines. |
| 2 | `OO`<br>`XO`<br>`OO` | **Jazz Tendencies** | Strong pull toward the 7th degree from almost anywhere; resolves back to root. |
| 3 | `XO`<br>`XO`<br>`OO` | **Glacial** | Stays on the current note most of the time. Very slow evolution. |
| 4 | `OO`<br>`OO`<br>`XO` | **Drone** | Overwhelming self-loop weight (~83%). Barely moves unless disturbed. |

While Knob X is being turned, LEDs 0, 2, 4 (left column) show the current profile index at half brightness. After ~1 second of no movement the LEDs revert to their idle state.

### Knob Y — Percussion Profile

Selects one of four MarkovPerc transition profiles. Fully CCW = profile 0, fully CW = profile 3.

| Zone | LEDs | Profile | Character |
|------|------|---------|-----------|
| 0 | `OO`<br>`OO`<br>`OO` | **Steady** | Rock/pop feel. Frequent hits and accented hits; rests are brief. |
| 1 | `OX`<br>`OO`<br>`OO` | **Syncopated** | Funk/Latin feel. Structural rests, frequent flams, Ratchet 2 as a syncopation device. |
| 2 | `OO`<br>`OX`<br>`OO` | **Jazz/Free** | Complex patterns. Ratchets are common fills; accented flams are the signature gesture. |
| 3 | `OX`<br>`OX`<br>`OO` | **Sparse** | Long silences punctuated by single hits. Ratchets and flams nearly absent. |

While Knob Y is being turned, LEDs 1, 3, 5 (right column) show the current profile index at half brightness. After ~1 second the LEDs revert.

### Main Knob (Z) — Switch-Dependent

The Main Knob's function changes with the 3-way switch position.

#### Switch UP — Transpose

Sets the melody base note. Range: −12 to +12 semitones (centre = 0). Applied **before** scale quantisation — turning the knob shifts which scale degrees get played, changing the melodic character rather than just the octave. CV In 1 is applied separately, after quantisation.

#### Switch MIDDLE — Loop & Lock

Controls looping. The knob is symmetric around its centre position:

| Knob position | Behaviour |
|---------------|-----------|
| **Centre** | Free running. No loop, no LEDs. |
| **CW → 16 / 8 / 4 / 3 / 2 beats** | Locked replay. The last N beats of play are captured and repeat exactly. Clean, unchanging loop. |
| **CCW → 16 / 8 / 4 / 3 / 2 beats** | Locked replay with mutation. Same capture mechanism, but each step has a 1-in-8 chance of being recalculated from the previous step via a Markov transition. The loop drifts gradually over many passes. |

When the knob moves away from centre, the last N beats already played are captured into the loop buffer immediately. Returning to centre releases the loop and resumes free running from where the chains left off.

A single LED (at half brightness) indicates the active loop length:

| Loop length | LEDs |
|-------------|------|
| **16 beats** | `XO`<br>`OO`<br>`OO` |
| **8 beats** | `OO`<br>`XO`<br>`OO` |
| **4 beats** | `OO`<br>`OO`<br>`XO` |
| **3 beats** | `OO`<br>`OO`<br>`OX` |
| **2 beats** | `OO`<br>`OX`<br>`OO` |
| **Free (centre)** | `OO`<br>`OO`<br>`OO` |

#### Switch DOWN — Scale Select *(momentary)*

While held, the Main Knob selects the quantisation scale applied to CV Out 1. All six LEDs show the current scale index in 6-bit binary at half brightness.

Release the switch to return to normal LED state. The new scale takes effect on the next clock pulse.

---

## LED Display

LEDs are off by default and illuminate only when relevant:

| State | LEDs 0, 2, 4 (left) | LEDs 1, 3, 5 (right) |
|-------|---------------------|----------------------|
| **Idle / free** | Off | Off |
| **Loop locked** | Single LED at half brightness indicates loop length (see table above) | — |
| **Knob X just turned** | mel_profile (0–4) in 3-bit binary, half brightness, ~1 s | unchanged |
| **Knob Y just turned** | unchanged | perc_profile (0–3) in 3-bit binary, half brightness, ~1 s |
| **Switch DOWN held** | Scale index (0–11) in 6-bit binary across all LEDs, half brightness | — |

Knob X/Y feedback takes priority over loop lock display while the timer is active. Scale display takes priority over both.

In **Dual Melody mode**, the right column (1, 3, 5) shows voice B profile instead of percussion profile when Knob Y is turned.

---

## Scales

Twelve scales arranged CCW (dark/minor) to CW (bright/ambiguous):

LED display shows the scale index in 6-bit binary across all six LEDs (X = lit, O = off). Left column = LEDs 0/2/4 (bits 0/1/2), right column = LEDs 1/3/5 (bits 3/4/5), read top to bottom.

| Index | LEDs | Scale | Notes | Character |
|-------|------|-------|-------|-----------|
| 0 | `OO`<br>`OO`<br>`OO` | **Phrygian** | 7 | Dark, unresolved minor. The flat 2nd gives an ancient or Spanish flavour. |
| 1 | `XO`<br>`OO`<br>`OO` | **Hirajōshi** | 5 | Tense, cinematic minor. A Japanese koto scale with wide leaps. |
| 2 | `OO`<br>`XO`<br>`OO` | **Harmonic Minor** | 7 | Dramatic; the augmented 2nd between ♭6 and 7 is the defining interval. |
| 3 | `XO`<br>`XO`<br>`OO` | **Natural Minor** | 7 | The standard minor centre. Neutral and versatile. *(Default)* |
| 4 | `OO`<br>`OO`<br>`XO` | **Minor Pentatonic** | 5 | Open, classic minor. No semitone steps; very forgiving. |
| 5 | `XO`<br>`OO`<br>`XO` | **m7 Arpeggio** | 4 | Root, ♭3, 5, ♭7 only. Pure minor 7th chord tones. |
| 6 | `OO`<br>`XO`<br>`XO` | **Dorian** | 7 | Minor with a raised 6th. Sophisticated — melancholic but lifted. |
| 7 | `XO`<br>`XO`<br>`XO` | **Major Pentatonic** | 5 | Open, consonant major. Pastoral and unambiguous. |
| 8 | `OX`<br>`OO`<br>`OO` | **Ionian (Major)** | 7 | The standard major scale. Bright and resolved. |
| 9 | `XX`<br>`OO`<br>`OO` | **Maj7 Arpeggio** | 4 | Root, 3, 5, 7. Pure major 7th chord tones. Lush and stable. |
| 10 | `OX`<br>`XO`<br>`OO` | **Whole Tone** | 6 | Dreamy, floating, rootless. Every interval is a tone; no leading-note pull. |
| 11 | `XX`<br>`XO`<br>`OO` | **Chromatic** | 12 | All 12 semitones. The raw Markov output — total ambiguity. |

---

## Percussion Hit Types

The current MarkovPerc state determines what Pulse Out 2 does during each clock cycle.

| State | Pulse Out 2 | Accent CV |
|-------|-------------|-----------|
| **REST** | Nothing | 0 V |
| **HIT** | Single trigger at beat start | Low |
| **ACC HIT** | Single trigger, full accent | High |
| **FLAM** | Soft grace note at tick 0, main hit at 1/8 of clock period | Medium |
| **ACC FLAM** | Soft grace note at tick 0, full-accent main hit at 1/8 | High |
| **RATCHET 2** | 2 evenly spaced triggers | Medium |
| **RATCHET 3** | 3 evenly spaced triggers | Medium |
| **RATCHET 4** | 4 evenly spaced triggers | Low |

---

## Internal Synth Voice

Audio Out 1 generates a square-wave oscillator pitched to the current MarkoV note, shaped by an AR envelope with a fast attack (~0.2 ms) and a long exponential decay (~2 seconds to near-silence).

Notes decay naturally across the full clock period and beyond, giving a sustained, ambient quality. Consecutive notes overlap and blend.

**Gate behaviour:** Pulse Out 1 fires and the envelope retriggers only when the quantised pitch *changes* from the previous step. Repeated identical pitches do not retrigger — the note sustains smoothly rather than stuttering.

**Accent:** Full-accent hits reach full envelope peak; plain hits are softer; grace notes (the leadup in flam patterns) start from silence at a reduced peak so the main flam hit punches through clearly.

**Ratchets:** Each trigger in a ratchet burst restarts the attack from the envelope's current level rather than from zero — the burst sounds like a single energetic note subdivided, not a string of separate staccato hits.

The voice is intentionally minimal: its purpose is to make the module immediately useful standalone. For richer sound, patch CV Out 1 to an external VCO and Pulse Out 2 to an external envelope generator.

---

## Patch Ideas

**Generative melodic voice:**
CV Out 1 → VCO V/Oct. Pulse Out 1 → envelope trigger. Pulse Out 2 → drum module or second envelope. CV Out 2 → accent input or VCA CV.

**Quantised drone:**
Set Melody Profile to Drone (Knob X fully CW) and Percussion Profile to Sparse (Knob Y fully CW). CV Out 1 → VCO. The melody barely moves; occasional sparse hits gate and accent it.

**Clock multiplication:**
Ratchet states on Pulse Out 2 produce up to 4 evenly spaced triggers per clock pulse. Patch to a clock divider or sequencer reset for polyrhythmic structures.

**Loop as a performance tool:**
Let the chains run freely for a few bars until a good pattern appears, then turn Main Knob CW (Switch MIDDLE) to lock the loop. Turn fully CW for clean repetition; nudge CCW to engage mutation and let the pattern evolve gradually. Return to centre to release.

**Scale morphing:**
Hold Switch DOWN and sweep the Main Knob while a sequence is playing. The scale updates on the next clock pulse. Dramatic shifts (e.g. Phrygian → Whole Tone) create instant reharmonisation without stopping the sequence.

**CV transpose as key change:**
With Switch UP, set the Main Knob to the root you want. Patch a CV keyboard or quantiser output to CV In 1 — it will shift the already-quantised melody up or down in semitones, effectively transposing the whole sequence to follow a chord progression.

---

## Dual Melody Mode

Hold Switch DOWN while powering on (or pressing reset) to enter Dual Melody mode. The firmware detects the switch position after the ADC settles (~150 ms) and latches the mode for the session.

In this mode both Markov engines are melodic — there is no percussion engine. Two independent MarkoV chains run in parallel, each outputting a pitched CV and driving its own synth voice.

### I/O in Dual Melody Mode

| Jack | Function |
|------|----------|
| **Pulse In 1** | Master clock (both chains advance together) |
| **CV In 1** | Post-scale transpose for voice A (±12 semitones) |
| **CV In 2** | Post-scale transpose for voice B (±12 semitones) |
| **Pulse Out 1** | Gate pulse for voice A (10 ms, only on pitch change) |
| **CV Out 1** | Pitch CV for voice A (V/Oct, calibrated) |
| **Pulse Out 2** | Gate pulse for voice B (10 ms, only on pitch change) |
| **CV Out 2** | Pitch CV for voice B (V/Oct, calibrated) |
| **Audio Out 1** | Voice A synth (independent) |
| **Audio Out 2** | Voice B synth (independent) |

CV In 2 is repurposed as a pitch transpose for voice B. The internal BPM clock is not available in this mode; a fixed 120 BPM fallback is used if no external clock is patched.

### Controls in Dual Melody Mode

| Control | Function |
|---------|----------|
| **Knob X** | Voice A profile (0–4, same profiles as primary mode) |
| **Knob Y** | Voice B profile (0–4) |
| **Main Knob + Switch UP** | Shared transpose for both voices (pre-scale) |
| **Main Knob + Switch MID** | Loop length (same symmetric CW/CCW mechanism; both voices share the same loop) |
| **Main Knob + Switch DOWN** | Scale index (shared for both voices) |

### Patch ideas (Dual Melody)

**Two-voice harmony:** CV Out 1 + CV Out 2 → two VCOs; Audio Out 1 + Audio Out 2 → two mixer channels. Both voices wander through the same scale on independent Markov paths, creating unexpected intervals. Knob X = voice A character, Knob Y = voice B character.

**Interval control:** Patch CV In 1 and CV In 2 to different fixed offsets. Voice A and B each transpose independently, so you can anchor them a third or fifth apart while both chains evolve freely.

**Canon/round:** Set both voices to the same profile but start them from different states. They will occasionally converge on the same pitch before diverging again.

---

## Technical Notes

- Sample rate: 48 kHz. System clock: 144 MHz (required for CV PWM resolution).
- Integer-only DSP in the audio callback. Float arithmetic is used only at boot time to populate the MIDI-note → phase-increment LUT.
- The two Markov engines are independent; only the clock is shared.
- **External clock:** rising edge on Pulse In 1. Works with any clock source — Eurorack clock module, LFO square wave, drum trigger, sequencer gate output.
- **Internal clock:** if no rising edge arrives on Pulse In 1 for 3 seconds, CV In 2 takes over. Tempo is 0 V = 120 BPM, with ±5 V spanning roughly 20–220 BPM. Patching Pulse In 1 again resyncs immediately on the next pulse.
- **Transpose signal chain:** knob transpose (pre-scale) → scale quantisation → CV In 1 shift (post-scale). The knob changes which scale degrees are used; CV In 1 moves the result chromatically. CV In 1 is frozen at zero while Switch DOWN is held to prevent accidental pitch shifts during scale selection.
- **Loop buffer:** 16 beats deep. The buffer is filled from a rolling history of recently played states — the loop captures what was already playing, not a newly generated sequence.
- **Loop mutation:** in CCW loop mode, each step has a 1-in-8 chance of being regenerated via a Markov transition from the previous step. Over many passes the sequence gradually drifts while remaining rhythmically coherent.
- **Pitch-change gate:** Pulse Out 1 (and voice B gate in Dual Melody mode) fires only when the quantised pitch changes from the previous step. Repeated notes sustain without retriggering.
- **Percussion density:** the Steady and Syncopated profiles are deliberately active — rests occur but are outnumbered by hits. For more silence, use Sparse (Knob Y fully CW).
- **LED knob timeout:** after Knob X or Y is turned, the relevant column shows the profile index at half brightness for ~1 second, then reverts. Scale display (Switch DOWN) has no timeout — it persists until the switch is released.
- **Dual Melody boot:** mode is latched once, ~150 ms after power-on. Hold Switch DOWN before applying power (or during a reset), release after the LEDs first flicker. Power-cycling without holding DOWN returns to primary mode.

---

## Credits

Markov chain algorithms adapted from the [MarkoV](https://github.com/uglifruit/O_C-Phazerville/blob/UGLIALL-on-uglimods/software/src/applets/MarkoV.h) and [MarkovPerc](https://github.com/uglifruit/O_C-Phazerville/blob/UGLIALL-on-uglimods/software/src/applets/MarkovPerc.h) applets for [Ornament & Crime (Phazerville)](https://github.com/djphazer/O_C-Phazerville), written by Andy Jenkinson (uglifruit).

Ported to the [Music Thing Modular Workshop Computer](https://github.com/TomWhitwell/Workshop_Computer) by Andy Jenkinson (uglifruit).
