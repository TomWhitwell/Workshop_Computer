# Stage 1: testing on the real card (about 10 minutes)

**What stage 1 does:** the timer, the knob set-up, the pulses, the CV ramps and the LEDs. **Audio passes straight through, dry**
(no reverb/delay yet) and the card **does not speak yet**: those are stages 2 and 3.

## Flash it
Use `qi_timer.uf2` (144 MHz). The `_192mhz` file is a backup; stage 1 needs very little CPU, so you should not need it.
Hold the boot button, plug in USB, drag the `.uf2` onto the drive that appears, then **cycle the power**.

## Patch
* **Pulse Out 1** -> a slope's trigger input -> that slope shaping your **chime** (oscillator 1 -> filter -> slope) -> **Audio In 1**.
* **Pulse Out 2** -> the other slope -> your **bell** (oscillator 2 ...) -> **Audio In 2**.
* **Audio Out 1 and 2** -> the mixer / headphones.

## Try this
1. **Power up.** The top two LEDs should breathe slowly.
2. **Set 1:00 x 2 by feel.** Z up. X fully CCW: turn Main fully CCW (the bar should shrink to a sliver = 1 minute).
   X fully CW (intervals; the bar shows the default 6). Main is still fully CCW, so turn it **slowly clockwise**:
   the bar drops to a sliver (1 interval), then grows one step (2 intervals): stop there.
   Y fully CCW (0 extra seconds). Z back to the middle.
   *If a knob seems to do nothing:* it was already where you want it; turn it away a little and back (see "take over" below).
3. **Tap Z down.** The LEDs count down for 10 s, then **Pulse 1** should ring your chime ("started").
4. After 1 minute: the **chime** again. After another minute: the **bell** (Pulse 2), and all six LEDs glow and fade.
5. Start again; **tap** during the countdown (blinks = paused), wait, tap again (it carries on: no new 10 s).
6. **Hold Z down for 3 s**: the LEDs fill; at 3 s it stops. Let go early (after 0.8 s) and nothing happens.
7. Optional: patch **CV Out 1** to something slow (a filter cutoff, a VCA) and hear it rise over the whole session;
   **CV Out 2** rises over each interval and drops back at every chime.

## Please tell me
* Does anything ever change **by itself** or by accident (knobs resting near a step, flipping Z or X)?
* Is the **take-over** right? (A knob does nothing until you move it a little; then its position is the value.)
* Are the **zones** comfortable (X: lowest third = minutes, highest third = intervals, middle third = nothing)?
* LED **brightness** in your room: too dim / too bright? Does the flash on each pulse feel welcome or intrusive?
* Does the 100 ms pulse trigger your slopes reliably?
* Anything glitchy (stuck knobs, LEDs freezing)? That would be unexpected at this CPU load, but say so.
