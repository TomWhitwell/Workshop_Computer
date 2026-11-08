# Changelog

## 0.5.0 — 8 November 2026

- Hold Z Down at boot/reset to open a silent sound selector. Main left chooses
  Bells, right chooses Wood Chimes; release Z to confirm and start playback.
- Show Bells on physical LEDs 1+3, Wood Chimes on 2+4 during selection. Preserve
  LED6 exclusively for timing warnings; restore normal roles on release.
- Normal boot always chooses Bells. Selection lasts until reset; no flash writes.
- Add damped two-resonance Wood Chimes and warmer pad colour, keeping the accepted
  melody mix, bass, scales, chord commits, score, accents, rests, tide and effects.
- Keep normal Bells audio bit-identical to stable 0.4.1 for the fixed-seed render.
- Debounce the startup release and consume its gesture so it does not advance a chord.
- Promote after Adrian reported hardware tests passed; retain the exact tested
  firmware bytes. Archive previous stable 0.4.1 and the original 0.5 experiment
  with source, notices and original documentation.

## 0.4.1 — 8 November 2026

Promote the hardware- and torture-tested 0.4.1 image to stable, without changing
its bytes. A fresh build from the promoted source reproduces the tested UF2.

- CV1 directly selects seven scales rooted on D instead of transposing.
- Physical LEDs 1/2/3 show scale codes 1–7 with weights 1/2/4 while CV1 is patched;
  unplugging restores Dorian and the pad-breath display.
- CV2 prepares a degree I–VII chord in the active scale; Pulse In 2 commits it.
  Unplugging CV2 restores automatic progression and next-chord fallback.
- Preserve shared-note transitions, expressive bell phrases, quieter bell balance,
  layer rests, the tide, gliding bass and stronger external-audio effects send.
- Preserve LEDs 4/5/6 as melody activity, Hold/clock and timing warning.
- Archive previous stable 0.3.0 and the completed 0.4/0.4.1 experiments, retaining
  source, firmware, attribution notices and original test records.
- Update operating instructions, metadata, test protocol, build report, archive
  index, experimental status and future plans for the current stable release.

## 0.3.0 — 8 November 2026

Previous stable bell-phrasing release. Original public documentation and firmware
are preserved in [development archive](https://github.com/soveda/Workshop_Computer/tree/3e8807e8/releases/126_Departures/archive/0.3.0/README.md).

## Earlier versions

See the [archive index](archive/README.md) for 0.1.0 and all experimental firmware,
including the accepted 0.2.2 balance and original 0.3, 0.4 and 0.4.1 source bundles.

## Upstream release packaging

Include only the current stable UF2 and source bundle. Earlier stable and
experimental bundles remain preserved on the development branch; the local
archive index links to those original snapshots.
