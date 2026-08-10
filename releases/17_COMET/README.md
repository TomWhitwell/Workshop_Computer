# COMET

A clocked lo-fi **reverb and delay** program card for the [Music Thing Modular Workshop System Computer](https://www.musicthing.co.uk/workshopsystem/) Written in fixed-point C++

## Quick start

1. Download `17_COMET.UF2`
2. Hold the Computer's boot button while powering on; it mounts as an `RPI-RP2` drive.
3. Drag the UF2 onto the drive. The card reboots into MoodComputer.

## Controls

1. Main knob = Clock. CW = fast/clean, CCW = slow/crushed. Internal clock runs 100 ms, 2 s. With a cable in Pulse In 1, the external clock is measured and Main becomes a mult/div ladder instead (×4 ×3 ×2 ×1.5 ×1 ×⅔ ×½ ×⅓ of the incoming rate).
  
2. X knob = MOD-W. Reverb: smear, random modulation of the tank read positions. Delay: feedback, up to gentle self-oscillation at the top. Also morphs the CV Out 2 LFO shape (sine - triangle - ramp - pulse).
  
3. Y knob = Time. Reverb: decay. Delay: division of the clock (1/8 · 1/6 · 1/4 · 1/3 · 1/2 · 3/4 · 1/1 · 3/2, with hysteresis). Also sets the CV Out 2 LFO phase offset.
  
4. Switch up = Reverb
  
5. Switch middle = Delay
   
6. Switch down (momentary) = Freeze, while held. Input is cut and the active engine recirculates reverb holds as an infinite drone, delay repeats loop losslessly. Release and it decays back at whatever Y is set to. Freeze stacks with the crush eg freeze something clean, then slow the clock and watch it pitch down.

Mode changes crossfade over ~13 ms and both engines keep running, so tails ring through a switch flip.

## ins + outs

1. Audio In 1 / 2 Stereo input. In 2 is normalled to In 1 for mono sources.

2. Audio Out 1 / 2 Stereo output, fixed 50/50 dry/wet.

3. CV In 1 Adds to the X knob

4. CV In 2 Adds to the Y knob

5. Pulse In 1 External clock in (auto-detected; Main knob becomes mult/div)

6. CV Out 1 Weird envelope follower of the wet signal: fast attack, release that gets *faster* the louder it is, plus a slow random wobble. Bouncy, saggy, alive. Unipolar 0-5-ish V.

7. CV Out 2 Clocked LFO. One cycle per clock, hard-synced to each tick. Shape from X, phase from Y. Bipolar.

8. Pulse Out 1 Clock through mirrors an external clock, otherwise 10 ms internal clock ticks

9. Pulse Out 2 Reverb: gate high while the tail is audible. Delay: 15 ms trigger whenever a repeat happens (a real onset detector with a refractory period, not a timer).

## LEDs

The six LEDs are a stereo VU meter on the outputs: left column (top/mid/bottom-left) is the left channel, right column the right channel, rising from the bottom row.
