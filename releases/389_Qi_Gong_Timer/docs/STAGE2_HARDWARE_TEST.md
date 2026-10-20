# Stage 2: testing the effects on the real card (about 15 minutes)

**New in stage 2:** reverb + delay on both channels (In 1 -> Out 1, In 2 -> Out 2, never mixed), set in **Z middle**.
Everything from stage 1 works as before. The card still **does not speak** (stage 3).

**The card now runs at 192 MHz** (`qi_timer.uf2`), as the Equanimity card does: the two reverbs and delays need the headroom
(measured worst case 73% of the time available per sample; at 144 MHz it would be 94%, too close for comfort).
`qi_timer_144mhz.uf2` is only a fallback in case your unit dislikes 192 MHz (it might crackle under heavy settings).

## The effect controls (Z in the middle)
**Main picks a page; X and Y edit it.** Between the pages Main does nothing.

| Main | X | Y |
|---|---|---|
| **fully CCW: reverb** | decay time (1 s ... 25 s) | darkness (bright ... dark) |
| **12 o'clock: mix** | reverb level (CCW = none) | delay level (CCW = none) |
| **fully CW: delay** | echo time (60 ms ... 1 s) | feedback (0 ... 93%: never runs away) |

* **Soft pickup:** each setting keeps its value until the knob **passes through** it, so nothing jumps.
  The LEDs help: when you touch X or Y, a bar shows the stored value; it **flickers until you've caught it**, then follows the knob.
* Entering a page shows which one for a moment: **left column** = reverb, **middle row** = mix, **right column** = delay.
* **Power-up sound:** a medium-long (~7.7 s), darkish reverb at a moderate level; the delay is off.
* The **dry sound is never changed**: only the effect is added (and only the effect is squeezed if the sum gets near full scale).
* Effects work any time with Z in the middle: stopped, running or paused. Z up is still set-up (it never touches the effects).

## Try this
1. **Flash and power-cycle.** Patch as in stage 1 (chime -> In 1, bell -> In 2, Out 1 and 2 -> your mixer).
2. **Listen to the default:** start a session (or trigger your slopes by hand). Chime and bell should now have a gentle, darkish tail.
3. **Reverb page** (Main fully CCW): sweep **X** up for long tails, **Y** for darker/brighter. Try the longest, brightest setting on the bell.
4. **Mix page** (Main at 12 o'clock): X fully CCW should make Out 1/2 **exactly dry**; raise **Y** to bring in the delay.
5. **Delay page** (Main fully CW): change the echo time while echoes ring (it glides, like tape); push feedback fully up and stop the input:
   the repeats should fade away by themselves, never build up.
6. **Turn knobs while a sound rings:** listen for any clicks or zipper noise.
7. Leave it ringing and listen to the very end of a long tail: it should fade to **true silence** (no hiss, no hum left behind).

## Extra checks (from the Robopera project's hardware findings and the AI directive)
8. **Hum between alerts.** With your chime and bell patched and the mixer turned up, listen in the quiet *between* alerts
   (wait at least 20 s after a sound). Is there a faint tone or hum? Does it get louder when you turn the reverb mix up
   (the reverb would be sustaining the leak)? Robopera found that no filter or Ring Mod "VCA" closes fully.
9. **192 vs 144 MHz.** Flash `qi_timer.uf2`, listen closely to Out 1 with **nothing playing** but the cables in; then flash
   `qi_timer_144mhz.uf2` and listen again. Any faint whistle or tone in one but not the other? (The directive says 144 MHz
   reduces tonal artifacts from the input converter.)
10. **The very end of a long tail.** Set a long reverb, play one bell, and listen as it fades out completely: any crackle or
   "frying" sound in the last moments? (A known DAC glitch at zero; there is a documented fix if you hear it.)
11. **Your patch.** How did you patch the chime and bell (where does each Slope go, which shape mode)? A photo is perfect.

## Please tell me
* **The sound:** does the default suit your chime and bell? Too wet / dry, too long / short, too dark / bright?
* Is the **range** of each knob useful (decay 1-25 s, echo 60 ms-1 s)? Anything you want more resolution on?
* **Clicks, crackles or glitches** anywhere (especially with everything turned up)? That would point to CPU trouble.
* Does the **soft pickup** feel OK with the LED help, or is catching the values fiddly?
* Any **hum, hiss or DC thump** at the outputs (especially when nothing is playing)?
