# Birds Handover

Workshop Computer card 44. Firmware version 0.5.0.

This document is for the next person who opens the project after the decisions
have gone slightly cold. It records the prior art, the current behaviour, the
bugs that shaped the code, and the design principles that should survive future
rewrites.

## Project Aim

Birds is a two-voice procedural birdsong instrument for the Music Thing Modular
Workshop Computer. It is built with the Pico SDK and the `ComputerCard` helper
library.

The target is not a perfect biological model or a Merlin Bird ID challenge. The
target is a playable modular instrument that can sit in a patch and produce:

- plausible electronic birdsong,
- strange-but-still-birdlike wrong birds,
- repeatable generative patterns when locked,
- alive-feeling two-bird interaction,
- smooth output without squarewave, noise-burst or CPU/mux failure artifacts.

The most important phrase from the design conversation was "bird-ish-ness". If a
feature sounds clever but makes the bird disappear, it should lose.

## Prior Art

### Hunter Adams' RP2040 Birdsong Lab

The immediate technical inspiration is V. Hunter Adams' Cornell RP2040 birdsong
exercise:

https://vanhunteradams.com/Pico/Birds/Birdsong.html

That lab deconstructs a Northern Cardinal recording by looking at the
spectrogram, selecting dominant frequency traces, and synthesizing them with
direct digital synthesis. Hunter points out that cardinals and many other
songbirds often produce almost pure frequency-modulated tones, and the lab
simplifies the synthesis by following the loudest spectrogram lines rather than
attempting a full physical syrinx model.

Important ideas borrowed from Hunter:

- Use DDS rather than samples.
- Think in terms of pitch contours: chirps, swoops, sweeps and silences.
- Avoid clicks by shaping amplitude.
- Use fixed-point arithmetic on the RP2040.
- Test ISR/sample cost seriously, because the audio callback is not a forgiving
  place to be expensive.

Important departures:

- Hunter's lab is species-specific, keypad-driven, and oriented toward a
  recognizable cardinal song.
- Birds is knob/CV-driven and intentionally more abstract.
- The Workshop Computer has no keypad, so the "which button plays which
  primitive" interface was replaced with a Turing Machine-style lock/change
  grammar.
- The goal is not one known bird but a playable aviary.

### Music Thing Modular Turing Machine

The musical interface is inspired by the Music Thing Modular Turing Machine:

https://www.musicthing.co.uk/Turing-Machine/

The core idea is a 16-bit shift register whose feedback bit may be returned,
flipped, or randomized depending on the big knob. The important musical feature
is not "randomness" by itself, but the continuous region between change and
looping pattern.

Birds keeps this as the main performance gesture:

- centre: wandering, mutating grammar,
- clockwise lock: short repeated phrase,
- anticlockwise lock: inverted double-pass behaviour.

The original Turing Machine has a useful hardware quirk at the anticlockwise end:
the returned bit is always flipped, so the pattern plays once and then again in
inverted form. Birds maps that idea to phrase direction: up gestures can become
down gestures, swoops can reverse, and a 16-step idea can become a 32-step
double pass.

### Birdsong And Duetting Literature

The call-response redesign was informed by antiphonal and duet singing research,
not by a delay pedal model. Useful references:

- Frontiers overview of duetting as collective behaviour:
  https://www.frontiersin.org/journals/ecology-and-evolution/articles/10.3389/fevo.2016.00007/full
- Eurasian wren timing / overlap avoidance study:
  https://www.sciencedirect.com/science/article/pii/S0376635713002453
- General physical synthesis background, including syrinx models:
  https://pubmed.ncbi.nlm.nih.gov/16383664/

The practical takeaway for this firmware:

- "Reply" should not mean "repeat with detune".
- Each bird should have its own phrase stream.
- One bird may answer in a gap left by the other.
- Alternation should be common, but overlap should sometimes happen.
- Phrase-type association matters: a reply can answer a sweep with a chirp, a
  tone with a sweep, a coo with a simpler gesture, etc.

### Physical Birdsong Models

More detailed models exist: syrinx two-mass models, neural song control models,
source-tract coupling, and neural vocoder resynthesis. Those were intentionally
not implemented here.

They are useful conceptually because they remind us that real birdsong involves
airflow, tissue oscillation, filtering, left/right syrinx asymmetry, learned
sequences and social timing. But a full physical model would be too expensive,
too hard to control from three knobs, and likely less immediate as a modular
instrument.

Birds borrows the shape of those ideas rather than the equations:

- separate phrase grammar from oscillator rendering,
- add body-like modulation for coos,
- keep contours smooth,
- allow two voices to behave socially.

## Current Interface

Main knob:

- Turing Machine lock/change.
- Around noon, the register mutates freely.
- Fully clockwise locks into a short repeated loop.
- Fully anticlockwise locks into an inverted/double-pass loop.

X knob plus CV 1:

- Master pitch/register.
- Applied live to the current contour.
- Intended to be fast enough for low audio-rate modulation, subject to the
  Workshop Computer ADC/CV input path.

Y knob plus CV 2:

- Syllable and phrase time scale.
- Currently exponential-ish from about `0.1x` fast to `10x` slow:
  `0.1, 0.18, 0.32, 0.56, 1, 1.78, 3.16, 5.62, 10`.
- This replaced a linear time multiplier, which felt like "lots of slow, then a
  tiny fast region at the end".

Switch:

- Middle: normal bird grammar.
- Up: wilder tonal grammar, but using the same duet/call-response engine.
- Down: reseed the shift register.

Pulse input 1:

- External syllable clock.
- If no external clock has been seen recently, the internal clock runs.

Outputs:

- Audio Out 1: bird 1.
- Audio Out 2: bird 2.
- CV Out 1: held pitch trace for bird 1.
- CV Out 2: held pitch trace for bird 2.
- Pulse Out 1/2: onset pulses for birds 1/2.

LEDs:

- LEDs 0/2 and 1/3 show pitch movement for each bird.
- LEDs 4/5 show pulse output activity.

Audio inputs:

- Unused in version 0.5.0.

## Current Synthesis Model

The audio is built from simple primitives:

- `Rest`
- `Tone`
- `UpSweep`
- `DownSweep`
- `Swoop`
- `ReverseSwoop`
- `ChirpUp`
- `ChirpDown`
- `Trill`
- `MelodyRun`
- `Coo`

The waveform is an integer-generated wavetable sine approximation. There are no
samples and no floating point in `main.cpp`.

Each active voice has:

- oscillator phase,
- optional modulation phase for coos,
- start and end frequency,
- fixed-point pitch contour state,
- duration,
- attack/decay envelope,
- optional trill period,
- AM/FM depth for coo phrases,
- current frequency for CV/LED reporting.

The synthesis rule is deliberately conservative:

- smooth pitch contours,
- smooth amplitude envelope,
- no hard audio-rate amplitude gates,
- no white-noise/buzz primitives,
- final output clamp only as a safety guard, not as a tone-shaping device.

### Coo

`Coo` was added after the patch sounded too much like similar up/down runs. It is
intended as a low, body-like phrase rather than a science-fiction FM sound.

Current coo modulation:

- middle switch: about 14-29 Hz,
- switch up: about 14-45 Hz,
- AM is the main audible effect,
- FM is shallow, under roughly 1.2%.

Earlier coo tests used 95-350 Hz modulation, which sounded like "space laser".
That was judged not birdlike enough.

## Sequencing And Duet Behaviour

The global source of memory is a 16-bit shift register, `reg`.

On each syllable tick:

1. The feedback bit is taken from bit 15.
2. The main knob sets the probability that the returning bit flips.
3. The register shifts left and the new bit enters bit 0.
4. The sequencer decodes register bits into bird primitives.

The important implementation detail is that free mode and locked mode are
handled differently.

### Free / Changing Mode

When the main knob is not at either lock extreme:

- each bird has its own sequence counter,
- a leader may initiate when both birds are quiet,
- either bird can schedule the other using a pending start,
- the answer phrase comes from the answering bird's own grammar stream,
- `answerCode()` can transform a same-ish answer into a related phrase,
- answers usually occur after a gap,
- occasionally an answer overlaps the caller.

This is intended to feel like two related birds in the same habitat, not a stereo
delay.

### Locked Mode

The first duet rewrite introduced independent counters and pending answers. That
made the register deterministic, but the audible pattern could still drift, so
5 o'clock did not feel truly locked.

Locked mode now bypasses the drifting duet scheduler:

- pending starts are cleared on each locked step,
- clockwise lock uses a 16-step phase,
- anticlockwise lock uses a 32-step phase,
- phrase choice is derived from the shared locked phase,
- call/answer alternation is deterministic,
- occasional overlap is still allowed but at fixed phase positions.

This is a design principle, not just a fix: a locked Turing Machine must sound
locked over a human-scale loop, around 8-16 steps, not merely be mathematically
repeatable over a long hidden cycle.

## Bugfix History

### ADC / Mux Mapping Failure

Symptom:

- controls worked after reset,
- after around 20 seconds to two minutes, a control or CV output appeared to be
  reading the wrong mux channel,
- CV Out 1 sometimes jumped from following X to following another input.

Likely pressure points:

- audio callback doing too much work,
- flash access during callback,
- normalisation probe / ADC path extra activity,
- runtime floating point and other expensive operations.

Fixes applied:

- removed normalisation probe use,
- made the target run from RAM with `pico_set_binary_type(... copy_to_ram)`,
- removed floating point from firmware source,
- replaced boot-time float LUT generation with integer LUT generation,
- reduced per-sample 64-bit math,
- removed buzzy/noise primitives,
- kept slow UI/CV/LED updates out of the hottest path where practical.

Status:

- reported stable after those changes, but this remains a thing to watch when
  adding anything expensive to `ProcessSample()`.

### Floating Point Removal

The user explicitly asked whether all floating point had been removed. It had
not been: some remained in LUT generation and ratio-style code.

Fixes:

- removed `cmath`,
- replaced sine generation with integer approximation,
- replaced pitch scaling with an integer lookup,
- replaced linear duration math with fixed-point Q12 scaling,
- kept `-Wdouble-promotion` and `-Wfloat-conversion` as warning tripwires.

Note:

- `ComputerCard.h` itself contains floats in calibration code. The "no floats"
  claim applies to `main.cpp` / the Birds firmware logic, not necessarily every
  bundled helper implementation line.

### Sine Table Wrap Distortion

Symptom:

- after removing floats, the audio outputs sounded distorted or clipped.

Cause:

- the integer sine approximation could produce `+32768`,
- casting that to `int16_t` wrapped it to `-32768`,
- the result was a hard one-sample discontinuity once per cycle.

Fix:

- clamp the generated wavetable to `-28672..28672`,
- add headroom before the final output clamp.

Principle:

- final clipping should be an emergency limit, not part of the oscillator sound.

### Squarewave / Buzz Artifacts

Symptom:

- repeated glitchy bursts or squarewave-ish phrases,
- worse in switch-up mode.

Causes:

- earlier `Buzz`/noise ideas were not birdlike in this patch,
- `Trill` was hard-switching between two pitches,
- `Trill` and `MelodyRun` used abrupt amplitude gating.

Fixes:

- removed buzz/noise primitives,
- changed trill pitch to smooth triangle motion,
- removed hard amplitude gating from trills/runs,
- left only the normal envelope and smooth contouring.

Principle:

- variation should come from grammar, contour, timing and smooth modulation, not
  from hard edges.

### Call And Response Was Too Much Like Delay

Symptom:

- stereo felt like repetition rather than conversation.

Cause:

- bird 2 originally behaved like a delayed/detuned variant of bird 1.

Fix:

- added independent bird sequence counters,
- added `PendingStart` scheduling,
- allowed either bird to initiate,
- made answers draw from the answering bird's grammar,
- added answer transformations,
- allowed mostly alternating but sometimes overlapping phrases.

Follow-up bug:

- this made locked mode drift.

Follow-up fix:

- add a separate locked duet path with fixed 16/32-step phase.

### CV Output Centering

Symptom:

- pitch CV outputs sat mostly around about -3 V.

Fix:

- pitch CV is now centered around 0 V based on the current syllable's min/max
  frequency span.
- silence does not force 0 V. It holds the last pitch voltage until the next
  active syllable.

Principle:

- pitch CV should describe the current gesture, not encode silence as a pitch.

### Y Control Shape

Symptom:

- time control felt like too much slow range and a tiny fast region.

Cause:

- time multiplier was linear across a 100:1 range.

Fix:

- changed to an exponential-ish fixed table in Q12.

Principle:

- wide musical ranges need perceptual/exponential control curves.

## Design Principles

### 1. Birdlike First

Every new sound source must pass the ear test. A feature can be technically
interesting and still be wrong. The removed buzz/noise and the too-fast coo FM
are examples.

### 2. Random Syllables Must Come From Grammar

Randomness should select among plausible primitives, phrase positions, rests,
durations and answer choices. It should not spray arbitrary oscillator states.

### 3. Lock Must Be Obvious

When the main knob is locked, the listener should hear a short repeated phrase.
Do not hide determinism in a long composite state machine. Lock means a loop.

### 4. Change Should Be Continuous

The main knob should move smoothly between unstable and locked. X and Y should
feel like playable controls, not mode switches disguised as pots.

### 5. Silence Is A Musical Object

Rests are not failure to emit audio. They are part of bird grammar and are
needed to avoid manic output. More rests usually sounds more birdlike than more
notes.

### 6. Two Birds Are Not A Stereo Effect

Bird 2 should be a related individual, not a delayed duplicate. It can answer,
ignore, overlap, transform or initiate.

### 7. Smooth Edges Unless There Is A Strong Reason

Clicks, hard gates, instant pitch jumps and square modulation quickly become
synth artifacts. Birdlike variation should usually use continuous contours and
rounded envelopes.

### 8. Keep The Callback Boring

The audio callback is the danger zone. Avoid floating point, division-heavy
inner loops, flash reads, unnecessary ADC interaction and complex branching
there. Do expensive or slow-changing work at phrase start or lower update rates.

### 9. CV Should Be Patchable, Not Merely Diagnostic

CV outputs are currently pitch traces. They should remain musically useful:
centered, held through silence, and correlated with the sound enough to patch
filters, folders or external oscillators.

## Code Map

`main.cpp`

- Entire card implementation.
- `BirdCard::ProcessSample()` is the audio/control loop.
- `stepTuring()` advances the shift register.
- `startSyllablePair()` chooses free or locked duet behaviour.
- `startLockedDuetStep()` handles short-loop locked mode.
- `nextPrimitiveForBird()` and `primitiveForBirdAtStep()` decode the register
  into grammar.
- `decodePrimitive()` contains the primitive tables.
- `startBird()` prepares a concrete syllable from a primitive.
- `processBird()` renders one voice.
- `applyBodyFm()` / `applyBodyAm()` implement coo modulation.
- `updatePitchCv()`, `updateLeds()` and `updatePulseOuts()` drive the outputs.

`ComputerCard.h`

- Workshop Computer hardware abstraction.
- Handles ADC, muxing, DAC/CV, LEDs, pulses and calibration.
- Treat as platform code unless debugging hardware interactions.

`CMakeLists.txt`

- Pico SDK build.
- Sets metadata.
- Enables warnings that help catch accidental float conversion.
- Builds with `copy_to_ram`, which was important for ADC/mux stability.

`README.md`

- User-facing description and build instructions.

`info.yaml`

- Release metadata for the Workshop Computer card ecosystem.

## Known Risks And Watchpoints

- The current code compiles, but formatting around `processBird()` and
  `updateGate()` should be cleaned before deeper refactors. The braces are valid,
  but the indentation is misleading.
- `ComputerCard.h` contains calibration code using floating point. If future
  testing shows callback stalls, confirm whether any helper paths called from
  `ProcessSample()` touch that code.
- `updateGate()` still exists mainly for pulse generation in trills/runs, not
  amplitude shaping. Be careful not to reintroduce hard audio gating through it.
- Locked mode was recently changed. Test it by ear at 5 o'clock and 7 o'clock
  before making more duet changes.
- The coo primitive is promising but taste-sensitive. Keep it slow and shallow
  unless deliberately making an alien bird.
- CV 1 input is intended as live pitch control. If low audio-rate modulation is
  pushed harder, check that ADC/mux stability remains solid.

## Suggested Next Steps

1. Test locked mode on hardware at full clockwise and full anticlockwise.
   Confirm the phrase loop feels like 8-16 steps, not a long cycle.
2. Clean indentation in `processBird()` and `updateGate()` without changing
   behaviour.
3. Tune primitive table probabilities by ear, especially rest density and coo
   frequency.
4. Consider a phrase-level "breath" state: longer intentional silences between
   clusters.
5. Consider using audio input later as habitat/control disturbance rather than
   direct audio processing.
6. Add a short table in the README documenting current frequency/time ranges for
   X, Y and `Coo`.

## Build

```sh
cmake -S . -B build
cmake --build build
```

The UF2 is written to:

```text
build/bird_card.uf2
```

