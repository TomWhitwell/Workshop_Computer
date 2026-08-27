# CS80 Dual Output Experimental

This is a local experimental variant of `580_CS80` for exploring a split-output
voice layout.

Current behavior:

- `Audio Out 1`: base mono CS80 voice
- `Audio Out 2`: second oscillator lane using the same voice architecture but
  detuned by `pitchOffsetQ8`
- the rest of the patch structure is shared for now: same envelope, filter
  topology, source mix, modulation, and ring-mod controls

Control note:

- `Switch Up / Main` is the detune amount for output 2 in this experiment
- `Switch Down / Main` remains the held performance pitch control

This folder is intentionally separate so the main `580_CS80` build stays stable
while we explore whether split-output dual-oscillator behavior feels more like
the direction you want.
