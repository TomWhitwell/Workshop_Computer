# Stage 3: the simplified design on the real card (about 15 minutes)

**Changed since your last test (19 Oct, second round):**
* **Silence before and after sessions too.** The silence keeper now opens ONLY for the card's own pulses (the chimes, the bell,
  preview mode), at all times. Before, after and between sessions the outputs stay truly silent, whatever leaks from the modules.
  One consequence: a sound triggered **by hand** (a Slope's button) is no longer heard. To hear your voices while tuning, use
  **preview mode** (triple tap).
* **Main has three clear positions:** fully anticlockwise = **reverb**, 12 o'clock = **mix**, fully clockwise = **delay** (each ~8% of
  the travel); anywhere else nothing changes. The page sign is an **LED row**: top = reverb, middle = mix, bottom = delay.
* **Ready** (stopped) is now **all six LEDs** breathing slowly and dimly.
* CV Out 1 and 2 removed (steady 0 V); stopping stays a 3 s hold.

**This build** (`qi_timer.uf2`, **144 MHz**, with the knobs and LEDs handled by the card's second core):
* **Set-up with LEDs and soft clicks** (no voice): Z up, then **Main = intervals (1-36), X = minutes (1-36), Y = seconds (0-60 in tens)**.
* A **double chime** at the start (two Pulse 1s, 0.6 s apart, after the 10 s settling period).
* **Preview mode:** triple-tap Z down while stopped: chime, 1 s, bell, 1 s, ... A single tap ends it.
* The **silence keeper now gates the dry signal too** (your choice): true silence between sounds; the sounds themselves pass untouched.
* Because of the triple tap, **a single tap now acts 0.4 s after you let go** (start, pause, resume).

The LED pictures are in `LED_LEGEND.md` (rule A, as on your slides 1-2).

## Try this
1. **Flash and power-cycle.** Patch as before (Pulse Out 1 -> chime Slope, Pulse Out 2 -> bell Slope; chime -> In 1, bell -> In 2; Out 1 and 2 -> mixer).
2. **Preview:** triple-tap Z down. Chime, bell, chime, bell... Tune your modules. Try the effects too (Z middle, as before). One tap ends it.
3. **Set-up by eye and ear:** Z up. Turn **Main** slowly: one soft low click per step, LEDs following rule A. Set **4 intervals**.
   Turn **X**: a slightly higher click; set **1 minute**. Turn **Y**: the highest click; set **0 seconds** (no LEDs). Z back to the middle.
4. **Start:** tap. After the 10 s countdown: **two chimes, 0.6 s apart**. Then a chime each minute, and the bell at the end.
5. **Quiet:** turn the mixer up between alerts. It should be **silent** (no leak, no hum). Do the chime and bell still start crisply and fade naturally?
6. Pause, resume, hold 3 s to stop: as before, just 0.4 s slower to respond to a tap.

## Please tell me
* **Clicks:** soft and calm enough? (They are about 23 dB below a typical chime.) Too quiet to hear in your room? Are the three notes distinct enough?
* **LEDs:** can you read 1-36 at a glance with rule A? Is the blink speed (0.4 s) comfortable?
* **Knob feel:** 36 steps per knob is fine-grained: any value that is hard to land on, or that changes by itself?
* **Double chime:** is 0.6 s apart right with your chime's decay, or should they be further apart / closer?
* **Preview mode:** easy to enter? Is 1 s between chime and bell enough to hear each sound?
* **The 0.4 s delay** after a tap: noticeable or fine?
* **Silence keeper:** any sound cut off, softened, or "breathing"?
* **Since the card is back at 144 MHz** with its work split over two cores: any crackle, glitch or stutter?

## The fixes from your last test
A. **Silence outside sessions:** with your FM patch, power up, wait, triple-tap into preview, tap out of it, wait. Silence before and after?
B. **Main pages:** fully anticlockwise = reverb (top row lights), 12 o'clock = mix (middle row), fully clockwise = delay (bottom row);
   in between nothing happens. Clear enough? Is the 12 o'clock window easy to find (not too narrow)?
C. **Reverb page Y:** clockwise = brighter?   D. **0 minutes:** 30-second intervals work?

## Copy, fill in and paste back
```
A. Silence before / after sessions (and after preview): ___
B. Main pages only near the ends: ___
C. Reverb Y clockwise = brighter: ___   D. 30-second intervals: ___
1. Preview mode (triple tap, chime/bell every second, tap to end): works ___  problems ___
2. Clicks (soft and calm? three notes distinct?): ___
3. LEDs rule A (readable at a glance? blink speed?): ___
4. Knob feel (36 steps; hard-to-reach values? anything changing by itself?): ___
5. Double chime (0.6 s apart: right?): ___
6. Silence between alerts (true silence? any sound cut off or "breathing"?): ___
7. The 0.4 s delay after a tap: ___
8. Any crackle, glitch or stutter (especially with long reverb tails, or while turning knobs)?: ___
9. Anything else: ___
```
