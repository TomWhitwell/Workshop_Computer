# Dub Warning Test Process

Current beta result: passed single-device hardware testing.

Use `UF2/DubWarning.uf2`.

## Setup

- Flash the UF2 to the Workshop Computer.
- Patch `Audio Out 1` to a mixer or monitoring chain.
- Start with `Main`, `X`, and `Y` at noon.
- Leave `Audio In 1`, `Audio In 2`, `CV In 1`, and `CV In 2` unpatched for the baseline test.

## Baseline controls

1. Set `Z Up`.
2. Confirm a continuous siren is audible from `Audio Out 1`.
3. Turn `Main` across its travel and confirm smooth pitch change. `LED1` should follow.
4. Turn `X` across its travel and confirm smooth siren sweep changes. The top quarter should stay musical rather than extreme. `LED2` should follow.
5. Turn `Y` across its travel and confirm delay moves from subtle repeats to dub wash/self-oscillation territory without an excessive noise floor. `LED3` should follow.
6. Confirm `LED4` pulses with the internal LFO.

## Switch

1. Set `Z Up` and confirm continuous siren.
2. Set `Z Middle` and confirm the siren is silent until triggered.
3. Tap `Z Down` from either `Z Up` or `Z Middle`.
4. Confirm `Z Down` fires the Wheel Up one-shot: a compact trilled siren phrase with stronger echo send.
5. Confirm `LED5` follows the Wheel Up/hit envelope during the gesture.

## Inputs

1. `Audio In 1`: patch an audio-rate or CV signal. Confirm it adds pitch modulation on top of the current Main pitch source.
2. `Audio In 2`: patch a slow CV or offset. Confirm it overrides the Main knob as the base pitch source. Move the Main knob while Audio In 2 is patched and confirm the knob no longer changes the base pitch. `LED1` should follow Audio In 2.
3. `CV In 1`: patch a slow CV or sequencer. Confirm it modulates siren pitch.
4. `CV In 2`: patch a slow CV. Confirm it changes oscillator color/drive without changing the basic delay setting.
5. `Pulse In 1`: with `Z Middle`, send a gate or trigger. Confirm it fires the normal gated siren envelope, not the Wheel Up gesture.
6. `Pulse In 1`: with `Z Up`, send a gate or trigger. Confirm it retriggers the siren envelope/hit while the continuous siren remains active.
7. `Pulse In 2`: send a trigger. Confirm oscillator/LFO phase reset and a short pluck/noise event, but no Wheel Up phrase.
8. `Pulse In 2`: send fast triggers or audio-rate pulses. Confirm it creates interesting reset-pluck textures. Sweep `Main` while triggering, or patch a slow CV into `Audio In 2`, and confirm the texture changes musically.

## Outputs

1. `Audio Out 1`: confirm main wet/dry siren mix.
2. `Audio Out 2`: confirm alternate direct-plus-echo output.
3. `CV Out 1`: patch to a CV input or scope. Confirm it outputs the internal siren LFO.
4. `CV Out 2`: patch to a CV input or scope. Confirm it follows the gate/hit envelope.
5. `Pulse Out 1`: patch to an envelope, trigger input, or scope. Confirm it goes high while the gated siren or Wheel Up gesture is active.
6. `Pulse Out 2`: patch to a trigger input or scope. Confirm it emits a short pulse on `Pulse In 1`, `Pulse In 2`, and Wheel Up.

## Pass Criteria

- `Z Up` is playable as a continuous siren.
- `Z Middle` plus `Pulse In 1` gives a usable gated siren around 500 ms.
- `Z Down` gives a distinct Wheel Up gesture.
- `Pulse In 2` resets/plucks without firing Wheel Up, and fast triggers create useful textures.
- `Audio In 2` defeats the Main knob when patched and acts as the base pitch control.
- `Y` remains musical over its travel, with the last quarter intense but usable.
- No significant hiss remains after a gated siren has decayed.
