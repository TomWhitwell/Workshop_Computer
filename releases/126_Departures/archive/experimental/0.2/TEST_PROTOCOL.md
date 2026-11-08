# Experimental 0.2 hardware checks

The 0.2.0 listening report found external audio too quiet. Version 0.2.1 raises
its effects send by 12dB and still needs hardware listening confirmation. The hardware pass
for 0.1.0 applies only to the stable image in the parent folder.

1. **Listen for 3–5 minutes at noon settings.** Listen for the tide and occasional
   missing bell phrases or bass entries that return naturally. Pad continues.
2. **Tap Down during a sustained chord.** Common tones should carry through;
   changing tones should disappear and return smoothly, without an abrupt retune.
3. **Repeat Down or Pulse In 2 requests during a transition.** The result should
   settle cleanly on the latest chord with no click, stuck voice or missing pad.
4. **Select Up.** Harmony and phrase mutations stop, no new rests start, existing
   rests complete, and the pad breaths and tide continue.
5. **Run a 5–10Hz external clock, maximum knobs and repeated chord triggers.**
   Sweep CVs through approximately ±5V. Watch physical LED 6 (bottom right): it
   should remain off. Check control responsiveness and output stability.
6. **Stop the patched clock.** New beat-driven notes stop; transitions, pad and
   tide keep moving. Restart resumes the phrases; unplug resumes internal timing.
7. **Patch an external oscillator and envelope to the pitch/trigger outputs.**
   During a bass rest, CV Out 2 holds its last pitch and Pulse Out 2 stops new
   triggers; both return with the bass. A resting bell loop contributes no events,
   but the other loops continue driving the shared melody CV/trigger outputs.
   Simultaneous bell events still merge.
8. **Compare external audio with 0.2.0.** Use the same source and Y setting.
   External audio should be substantially stronger in 0.2.1. Test each jack,
   then both together. Y fully left should still remove the external sound.
9. **Feed external audio through the space, reset and power-cycle repeatedly.**
   Check for stable tails and reliable startup, then optionally soak for 20–30min.

Record the patch, knob positions, timing LED state, audible symptoms and whether
reset restores playback. Roll back with `../../uf2/Departures_0.1.0.uf2`.
