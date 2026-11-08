# Experimental changelog

## 0.4.1-experimental — 8 November 2026

- Replace CV1 transposition with direct fixed-D scale selection: Dorian, natural
  minor, major, Mixolydian, Lydian, Phrygian and Locrian over approximately 0–5V.
- Preserve shared pitches and fade changed pad notes when switching scales;
  subsequent melody/bass entries follow the new scale. Keep the chord timer.
- Show the active scale as code 1–7 on physical LEDs 1/2/3, with weights 1/2/4,
  while CV1 is patched. Unplugging restores Dorian and the pad-breath display.
- Keep LEDs 4/5/6 as melody, Hold/clock and timing warning respectively.
- Keep CV2 as a prepared degree I–VII within the current scale, committed by
  Pulse In 2; preserve its unplug fallback and Down-switch behaviour.
- Add 49-combination scale/triad tests plus LED, hysteresis and queue checks.
- Preserve the tested 0.4 source, firmware and notices in their existing folder.
