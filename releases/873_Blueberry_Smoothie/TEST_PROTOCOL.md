# Blueberry Smoothie 0.3.0 Test Protocol

Status: user reports all 0.3.0 hardware tests passed on 2026-10-10. The passing
hold-to-record 0.2.0 source and UF2 are preserved in the
[Workshop Buddies archive](https://github.com/soveda/workshop-buddies/tree/main/cards/blueberry/archive/0.2.0).

## Setup

1. Patch one **4 Voltages** output to **CV In 1**.
2. Patch **CV Out 1** to a 1 V/oct oscillator and **Pulse Out 1** to its gate
   or an envelope generator.
3. Patch bottom Slopes output to **Pulse In 2**, with Loop/Falling selected,
   or use an external gate/trigger. Reset Computer with Switch Middle.
   No special 4 Voltages selection, resting-voltage learning, or user
   calibration procedure is needed.

Flash [Blueberry-Smoothie-0.3.0.uf2](uf2/Blueberry-Smoothie-0.3.0.uf2) for these tests.

For Workshop-only audio, patch CV Out 1 to the top oscillator pitch input,
its sine to Ring Mod upper input, and Pulse Out 1 to top Slopes input.
Set top Slopes to Falling with Loop off; patch its output to Ring Mod lower
input, then Ring Mod output to Mix. Turn oscillator FM down.

## Free Play

1. Press each 4 Voltages button and combinations of buttons.
2. Expect quantised pentatonic pitch. Pulse Out 1 and LED 4 follow the separate
   Pulse In 2 gate; latched pitch selection does not hold the gate on.
3. Unpatch Pulse In 2. After jack detection settles, expect gate off regardless
   of pitch/button changes. Repatch it to resume gates without resetting.
4. Turn Main past 3 o'clock, return to centre, then play again. Expect a
   Blueberry-style upward fifth step. Repeat: the next step reaches an octave.
5. Turn Main past 9 o'clock after returning to centre. Expect the reverse
   sequence.

## Recording And Free Loop Playback

1. Leave Pulse In 1 unpatched. Press Down once, then release to Middle.
   LED 2 should come on and stay on. Play pitches and gates with both hands free.
2. Press Down again to stop, then release. LED 2 should go off. Holding Down
   for a second must only toggle once, not repeatedly start/stop recording.
3. Move Switch Up. Expect the phrase to repeat with the same timing and
   pitch/gate pattern. Pulse Out 2 marks each loop restart.
4. Return to Middle. Expect live free play again; the loop remains in RAM.
5. Start another take with Down, release and play, then select Up without a
   second Down press. Recording should stop and playback begin: LED 2 off,
   LED 1 on. This replaces the previous loop.
6. In Up, change the live pitch and gate or unpatch Pulse In 2. The saved
   phrase should remain unchanged. In Middle those inputs should control
   the output again. Record once with Pulse In 2 unpatched: that loop should
   have no output gates.
7. Reset with Down held. Recording must stay off until released and pressed
   again. Reset with Middle should leave LED 2 off and normal live control.

## Clocked Playback

1. Patch a steady clock to Pulse In 1.
   With only the Workshop System, move bottom Slopes from Pulse In 2 to Pulse
   In 1 after recording. The saved gates no longer need a live gate source.
2. With Switch Up, expect each rising clock edge to advance to the next
   captured note event. LED 3 indicates that the clock input is patched.
3. Remove the clock. Expect playback to resume the phrase's recorded timing.
4. Captured events include gate-off states, so not every clock edge will sound
   a note. Try different clock rates and confirm most edges are not missed.

## LED 0 And Stability

1. LED 0 reports whether existing factory/input calibration coefficients were
   loaded from EEPROM. It is informational only: there is nothing to learn
   or calibrate for this test, and either LED state should remain playable.
   It does not report CV output calibration, so it can remain off on a
   Computer with calibrated outputs.
2. Check that a fixed CV In 1 voltage produces a stable quantised pitch.
3. Leave a loop running for 15 minutes, then check recording and free play.
   Reset clears the loop. Timing is recorded at 1 ms resolution; sampled
   short gate triggers are retained for at least one tick.
