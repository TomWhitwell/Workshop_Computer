# Departures 0.4.1 — hardware checks and recorded result

Adrian reported the normal hardware checks pass, then all additional torture
tests pass, on 8 November 2026. This is the exact firmware promoted to stable.
No failures were reported. Exact durations, source settings and instrumented
measurements were not supplied; times/rates below describe the suggested protocol.

## Normal functional checks

1. With CV1/CV2 unpatched, confirm the default Dorian behaviour.
2. Patch CV1 and visit the seven centre voltages in the README. Confirm direct
   scale changes and the exact LED1/2/3 code. Dorian versus major on degree I,
   with Y low, gives an obvious minor/major comparison.
3. Change CV1 without commit triggers. Shared pad tones continue, other tones
   fade into the new scale, and subsequent melody/bass entries follow it.
4. Prepare and commit CV2 degrees within each scale. CV2 alone must not commit
   a chord; recommitting the same chord must not restart the pad fade.
5. In Hold or with the beat clock stopped, external scale changes and chord
   commits still work. New beat-driven notes wait for clock restart.
6. Cross CV boundaries slowly and hold near them; hysteresis should resist small
   noise. Check both selectors independently.
7. Unplug CV1 for Dorian and pad-breath LEDs. Unplug CV2 for automatic progression
   and the ordinary Pulse In 2 next-chord action.
8. Confirm LEDs 4/5/6 remain melody, Hold/clock and the latched timing warning.

## Additional torture protocol

Watch physical **LED6, bottom right** throughout: it should remain off.

1. **Maximum load, suggested five minutes:** Main/X/Y fully clockwise; Pulse In 1
   at 10Hz and Pulse In 2 on a separate 7Hz clock; external audio into both inputs.
2. **Scale/chord chaos, suggested five minutes:** retain that patch; drive CV1
   with a 0–5V square wave around 8Hz and CV2 with unrelated stepped/sweeping CV.
   Fragmented harmony is expected; processing dropouts, hangs and frozen controls
   are failures.
3. **Hold/stop:** select Up, then stop the patched beat clock. External scale
   changes and commits continue; no new bell/bass strikes occur; pad and tails move.
4. **Commit isolation:** hold CV1 steady, stop Pulse In 2, and move CV2. No new
   destination should commit until Pulse In 2 resumes.
5. **Repatch/reset:** unplug each CV for its fallback, then reset with CVs high and
   clocks running. Every boot should recover and all controls remain responsive.

For any later failure, record patch, voltages, controls, LED6 state, whether the
controls still responded, and whether reset recovered playback. Archived rollback
images are linked in [archive/README.md](archive/README.md).
