# Skipping Stones Test Procedure

## Flash

1. Copy `uf2/SkippingStones.uf2` to a Workshop Computer card.
2. Insert the card and press reset.
3. Confirm the card boots after a second reset, not only after flashing.

## Smoke Test

1. Patch Audio Out 1 and Audio Out 2 to the Workshop System mixer.
2. Leave all inputs unpatched.
3. Set the switch to Middle.
4. Turn Main from fully CCW to fully CW.

Expected: Audio Out 1 plays the short DR-55 closed-hat sample. Audio Out 2
plays that same attack followed by the 120 ms DR-55 snare-noise tail, with no
second transient at the join. Higher Main settings should increase event
activity and rate. LED 5 should be dim, showing internal clock.

Fail if Audio Out 2 has a full snare transient, plays longer than roughly 120
ms, or either output is only a narrow click.

## Workshop System Patch

1. Patch CV Out 1 to a SineSquare pitch input.
2. Patch Pulse Out 1 to a Slope trigger input.
3. Patch the Slope output to the ring mod/VCA or filter control.
4. Patch CV Out 2 to filter cutoff, Slope time, or another modulation input.
5. Listen to the analogue voice rather than the monitor voice.

Expected: CV Out 1 produces stepped quantized pitches, Pulse Out 1 triggers
the voice, and CV Out 2 moves with related but smoother voltage motion.

## Controls

Test each control with the basic Workshop System patch:

| Control | Expected result |
| --- | --- |
| Main | Changes internal rate and trigger density |
| X | Low values rewrite constantly; high values repeat a loop |
| Y | Widens pitch/modulation spread and adds timing jitter |
| Switch Up | Pulse streams feel more coupled and regular |
| Switch Middle | Pulse streams have more gaps and independence |
| Switch Down | Held Write mode replaces loop steps aggressively |

## Inputs

1. Patch a clock or gate into Pulse In 1.
   - Expected: external clock replaces the internal clock; LED 5 goes bright.
2. Remove Pulse In 1.
   - Expected: after a short timeout, the internal clock resumes.
3. Patch Pulse Out 1 or another gate into Pulse In 2.
   - Expected: loop playback resets to the first captured step.
4. Patch Four Voltages or another CV source to CV In 1.
   - Expected: density/activity tilts with the incoming voltage.
5. Patch a slow CV to CV In 2.
   - Expected: spread/jitter changes with the incoming voltage.
6. Patch a slow CV or audio-rate signal to Audio In 1.
   - Expected: the random voltage distribution tilts brighter/darker rather
     than simply changing pitch directly.

## Stability Checks

1. Let the card run for 10 minutes with no inputs patched.
2. Let it run for 10 minutes from an external clock into Pulse In 1.
3. Move all knobs repeatedly while clocked.
4. Confirm the knobs do not lock around 1780-1790 and that channels do not
   appear to swap or drop to zero.

Those symptoms suggest `ProcessSample()` is overrunning the 48 kHz interrupt.

## Pass Criteria

- Boots from flash and after reset.
- Internal and external clocks both work.
- CV Out 1 tracks pitch musically enough for a first beta.
- Pulse Out 1 and Pulse Out 2 are distinct and patchable.
- Deja Vu control produces an audible move from novelty to repetition.
- No stuck knobs, frozen LEDs, or channel permutation during the stability
  checks.
