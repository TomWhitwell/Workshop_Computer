# Experimental changelog

## 0.2.2-experimental — 7 November 2026

- Reduce bell mix gain by 25% (approximately −2.5dB), including their effects
  contribution, in response to hardware listening feedback.
- Preserve pad, bass and external input gains, synthesis parameters and controls.
- Keep previous experimental UF2s for comparison.

## 0.2.1-experimental — 7 November 2026

- Raise the external effects send by 12dB (four times its previous gain) after
  the first hardware listening report found external audio too quiet.
- Preserve wet-only routing, equal input gains, the dry Y endpoint, and the
  internal ensemble's existing gain.
- Add a paired-engine audio-input level regression check, including both input
  jacks and the dry endpoint. Retain the original 0.2.0 UF2 for comparison.

## 0.2.0-experimental — 7 November 2026

- Retain sounding pad tones shared by the next triad, with oscillator and breath
  continuity. Fade changing tones out before retuning and back in afterward.
- Add occasional one-loop bell rests and eight-beat bass rests, one layer at a
  time, with tail-preserving returns on the phrase grid.
- Add a slow, seeded tide to density, brightness, pad/bell strength and space.
- Preserve beat rate, clock stop/unplug behaviour, CV reference, trigger timing,
  panel controls and the bottom-right timing warning.
- Up prevents new rest decisions in addition to holding harmony and mutations.
- Isolate the experiment here; the parent 0.1.0 release remains unchanged.
