# Open bugs

Things seen on hardware that are not yet fixed or not yet understood. Kept
here rather than in CLAUDE.md so the status file stays about what the card
IS, and this stays about what is wrong with it.

---

## Intermittent: A/B gestures stop responding, once

**Seen once**, 2026-08-10, during live drumming. Not reproduced on retry —
the same build behaved correctly afterwards, including UNDO.

Reported symptoms, in the order they happened:

1. A **cowbell fired unprompted** while playing. Cowbell is voice 8, which is
   the `hold A, tap B` gesture — not a combination being played at the time.
2. Afterwards, in DRUMS, **combinations involving A and B stopped working**.
3. In MUTE mode, the B shift **worked for taps C and D but not for A**.

### What would explain all three at once

A stale `shift_` latched on **A**. Every failing gesture is one that needs a
*fresh* shift to be latched; the ones that kept working (C and D taps) arrive
at their pair from a different single, so they relatch on the way in.

That is a hypothesis, not a diagnosis. It has NOT been reproduced.

### What was checked and ruled out

Against `tools/ghostsim.py`, all behaving correctly:

- `hold C, tap A, release, tap A again` — retriggers properly each time
- `hold A, tap B, release to A, tap B again` — same, shift stays A correctly
- landing on AB from an unrelated level, then A, then B, then AB
- jumping C → AB with both fingers landing together (shift correctly C)

The mute slot mapping (`SlotForShiftedTap` vs. the LED walk in `ModeLeds`)
was also verified consistent, and the switch guards around `FireAction` do
hold — a pair action cannot fire unless the switch is genuinely down.

### One real edge case the probe DID find

**pair → pair leaves `shift_` cleared.** Going straight from AC to AB (without
passing through a single) latches no shift, so `VoiceForGesture` fails and the
voice falls back to "whatever either member would give". See
`levels.cpp`'s comment at the `shift_` latch: it only sets on
*single → pair*, and only clears on *→ bare single*.

This is not the reported bug — it produces a *wrong* voice, not a *dead* one —
but it is the nearest thing to a genuine gap in the shift logic and is the
first place to look if this recurs.

### If it happens again

Worth capturing before power-cycling:

- which mode, and whether the switch had been touched recently
- whether the Four Voltages knob had been moved (a drifted calibration
  produces exactly this kind of "some combos work, some don't")
- whether a *hold* was in progress when it started

The calibration angle is worth taking seriously: this card does not yet
persist or re-verify its levels, and two combinations drifting close together
would silently misclassify in precisely this partial way.
