# Departures 0.3.0 hardware checks

This stable image is identical to the promoted 0.3.0 experiment.
The archived 0.2.2 image remains useful as a listening reference.
Compare both UF2s with the same patch and knob settings. Resets choose fresh
hardware seeds; use the fixed-seed host renders for a repeatable musical A/B.

1. **Stored expression:** start Main and X near noon and Y low. Listen for
   recurring softer/darker and stronger/brighter attacks as each phrase returns.
   The loudest accents should remain within the familiar 0.2.2 bell balance.
2. **Octave answers:** listen for 3–5 minutes. Occasionally one phrase should
   answer in a higher or lower register, then return home. Other layers continue.
3. **Gentle changes:** compare longer passages with 0.2.2. Mutations should sound
   like nearby note changes, preserving more of a phrase's identity.
4. **Hold:** select Up during an answer. It should finish and return home, with
   no new answers, rests or note mutations. Harmony holds; tide and breaths move.
5. **Pitch outputs:** listen to an external oscillator driven by CV Out 1 and
   Pulse Out 1. Octave changes should follow the internal melody; audio accents
   do not change CV/gate amplitude or require additional patch connections.
6. **Clock stop:** stop a patched clock. No new beat-driven notes/answers occur.
   Restart resumes progression; unplugging restores internal beat timing.
7. **Stress:** run a 5–10Hz external clock, full knobs, CV extremes and repeated
   chord requests. Feed external audio into each input and both together.
   Controls should stay responsive; physical LED 6 should remain off.
8. **Reset and soak:** repeat reset/power cycles and optionally listen for
   20–30 minutes. Check for reliable startup, stable tails and no stuck register.

Promotion was requested by Adrian on 8 November 2026. Record settings, patch,
physical LED 6 state and any audio symptoms. The reference firmware is
`archive/experimental/0.2/uf2/Departures_0.2.2-experimental.uf2`.
