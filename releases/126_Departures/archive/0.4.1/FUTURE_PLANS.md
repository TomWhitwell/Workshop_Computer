# Departures — future plans

## Woodier ensemble and startup sound selection

Requested 8 November 2026. The current reference is stable **0.4.1**, after
successful scale/chord and torture tests. The woodier palette and startup sound
selector remain future design ideas, not implemented firmware behaviour.

- Develop an alternative, woodier ensemble with wood-chime-type sounds alongside
  the current bell ensemble. Aim for warmer, less metallic attacks and resonances.
- Holding **Z Down while booting or resetting** could open a sound-selection mode.
- In that mode, let the user choose **Bells** or **Wood Chimes**, then enter normal
  playback. The proposed startup gesture should be distinct from the existing
  Down-switch chord-advance action during playback.
- Work out the selection knob, LED indication and confirmation gesture when
  prototyping. A possible approach is Main left/right to select, LEDs to show the
  choice, and release Z to confirm.
- Decide whether the choice lasts until reset or is saved between boots; this
  has not yet been specified.
- Keep the accepted bell version available for comparison while developing the
  new palette. Start by comparing timbres with the same phrasing and mix balance.

Use the stable 0.4.1 bell ensemble as the comparison baseline for that future
experiment. This plan does not change synthesis or startup controls.
