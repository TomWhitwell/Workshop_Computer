# LED legend

The six LEDs form a 2 x 3 grid, read like a page:
```
 1  2
 3  4
 5  6
```
Everything is kept **dim** on purpose (this card is used with eyes closed or the lights low). All levels are in `qi_config.h` (`kLed...`).

## Setting the timer (Z up)

Each knob sets one thing; the LEDs show whichever you touched last (intervals when you first flip Z up), and every step
makes a soft click (a slightly different note for each: intervals low, minutes middle, seconds high).

| Knob | Sets | Range |
|---|---|---|
| **Main** | number of intervals | 1 - 36 |
| **X** | minutes per interval | 0 - 36 (0 = seconds only: **no LEDs**) |
| **Y** | extra seconds per interval | 0 - 60, in tens (0 min + 0 s counts as 10 s, the shortest interval) |

**Intervals and minutes (the owner's design, "rule A"):**
* **1 to 6:** that many **medium** LEDs.
* Above 6, each **full** (bright) LED stands for a **six**, and the rest (1 to 6) is shown either by **blinking** one of the
  full LEDs (blinking LED 1 = +1, LED 2 = +2 ...) or, when there are not enough full LEDs for that, by **medium** LEDs after them.

| Value | LEDs (1 2 / 3 4 / 5 6) | Read as |
|---|---|---|
| 1 | medium, off / off, off / off, off | 1 |
| 4 | medium, medium / medium, medium / off, off | 4 |
| 6 | all six medium | 6 |
| 7 | **blinking**, off / ... | one six + 1 |
| 8 | full, medium / ... | one six + 2 |
| 12 | full, medium / medium, medium / medium, medium | one six + 6 |
| 13 | **blinking**, full / ... | two sixes + 1 |
| 14 | full, **blinking** / ... | two sixes + 2 |
| 15 | full, full / medium, off / ... | two sixes + 3 |
| 18 | full, full / medium, medium / medium, medium | two sixes + 6 |
| 20 | full, **blinking** / full, off / ... | three sixes + 2 |
| 21 | full, full / **blinking**, off / ... | three sixes + 3 |
| 30 | full, full / full, full / medium, medium | four sixes + 6 |
| 36 | full, full / full, full / full, medium | five sixes + 6 (the maximum) |

**Seconds:** one **medium** LED per 10 seconds (no LEDs = 0 s, all six = 60 s).

## Everything else

| What the card is doing | What you see |
|---|---|
| Stopped, ready | **All six LEDs breathe** slowly (about 5 s per breath), dimly |
| Settling period (10 s after Start) | All six **bright, going out one by one** (a countdown) |
| Each pulse (the double chime at the start, interval chimes, the end bell) | A **soft flash** of all six, fading over 0.6 s |
| Running | A **dim bar filling up** over the whole session |
| Paused | The bar (or countdown) **blinks slowly**: 1 s on, 1 s low |
| Holding Z down to stop (after 0.8 s) | A **bright bar filling** over the rest of the 3 s; let go before it is full to cancel |
| End of the session | All six **glow, then fade out** over 6 s; then back to "ready" |
| **Preview mode** (triple tap) | **Left column** during the chime's second, **right column** during the bell's (plus the flash) |
| **Effects (Z middle):** Main enters a page: fully anticlockwise = reverb, 12 o'clock = mix, fully clockwise = delay (anywhere else Main does nothing) | For 1.2 s, which page, in the order of Main's travel: **top row** = reverb, **middle row** = mix, **bottom row** = delay |
| **Effects:** X or Y moved on a page | For 2 s, a **bar of that setting's stored value** on a faint background. It **flickers** until the knob has "picked up" the value; then it follows the knob |

Priority when several apply: holding Z down > effect display > everything else.
Snapshots of each state are printed by `host/test_render_fast leds`.
