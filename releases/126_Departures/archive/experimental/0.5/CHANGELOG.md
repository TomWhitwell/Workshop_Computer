# Experimental changelog

## 0.5.0-experimental — 8 November 2026

- Hold Z Down at boot/reset to open a silent sound selector. Main left chooses
  Bells, right chooses Wood Chimes; release Z to confirm and start playback.
- Show Bells on physical LEDs 1+3, Wood Chimes on 2+4 during selection. Preserve
  LED6 exclusively for timing warnings; restore normal roles on release.
- Normal boot always chooses Bells. Selection lasts until reset; no flash writes.
- Add damped two-resonance Wood Chimes and warmer pad colour, keeping the accepted
  melody mix, bass, scales, chord commits, score, accents, rests, tide and effects.
- Keep normal Bells audio bit-identical to stable 0.4.1 for the fixed-seed render.
- Debounce the startup release and consume its gesture so it does not advance a chord.
- Preserve stable 0.4.1 source, firmware and documentation unchanged.
