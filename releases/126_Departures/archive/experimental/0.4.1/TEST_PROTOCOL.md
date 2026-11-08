# 0.4.1 scale and LED hardware checks

Adrian reported the hardware tests pass on 8 November 2026. Exact patches and
durations were not recorded. Adrian subsequently reported all additional torture
tests pass and requested stable promotion on the same date.
The tested 0.4 image remains
unchanged in `../0.4/`; stable 0.3 remains in the parent release folder.

1. With CV1/CV2 unpatched, confirm the familiar default Dorian behaviour.
2. Patch CV1 and step through the seven centre voltages in the README. Confirm
   the scale and exact LED1/2/3 code at each point. Use Dorian versus major for
   an obvious minor-to-major comparison, with CV2 at degree I and Y low.
3. Change CV1 without any Pulse In 2 triggers. Shared pad tones should continue
   and other tones fade into the new scale; later bell/bass entries follow it.
4. Prepare/commit several CV2 degrees within each scale, including degree VII.
   Check that CV2 alone still does not commit a new chord and that recommits are quiet.
5. Hold Up and stop a patched beat clock. CV1 scale changes and Pulse In 2 chord
   commits should still work; no new beat-driven notes arrive until clock restart.
6. Slowly cross CV1 boundaries, then hold near them. Scale/LED selection should
   resist small noise rather than chatter. Repeat separately with CV2.
7. Unplug CV1 from a non-Dorian scale: Dorian should return and LEDs 1–3 resume
   pad breaths. Unplug CV2: automatic progression and next-chord fallback return.
8. Check LEDs 4 and 5 retain melody/Hold/clock roles. LED 6 must remain exclusively
   a timing warning and stay off throughout normal and extreme patches.
9. Stress rapid scale/chord CV jumps, 5–10Hz beat/commit clocks, maximum controls
   and hot external audio. Check responsiveness, reset reliability and a longer soak.

Record voltages, selected scale/chord, LED states, patch and any audio symptoms.
