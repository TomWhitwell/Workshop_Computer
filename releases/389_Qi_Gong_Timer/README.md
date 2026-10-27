# QiGong Timer

**An interval meditation timer for Qi Gong (or any practice), set with the knobs and played by your own Workshop System voices.**

The card counts a session of intervals. At the start, at the end of every interval and at the end of the session it sends a
**pulse** that strikes *your* patched sounds: a chime for the intervals, a bell or gong for the end. It adds a calm **reverb and
delay** to those sounds, and keeps everything **completely silent** in between. You set it by touch and by eye: a soft click for
every step, and the LEDs show the numbers.

* Intervals: **1 to 36**, each **0 to 36 minutes plus 0 to 60 seconds** (shortest 10 s, longest 37 min; a session can last up to 22 hours).
* **10 seconds to settle** after you press start, then a **double chime**: the session has begun.
* A chime at the end of every interval, a **bell at the end** of the session.
* Pause, resume, stop. A **preview mode** to tune and dial in your sounds.
* Two separate effect chains (chime and bell never mix): reverb up to 25 s, delay up to 1 s.
* The card makes no sound of its own except very soft clicks while you set it.

---

## 1. Patching

The card does not make the chime and the bell: your Workshop System does. Two voices, each struck by one of the card's pulses.
The **wiring** is the same on every system:

```
Pulse Out 1 ──► Slope A input        (Slope A: Loop off, Falling)
Slope A out ──► Filter A FM
Oscillator ───► Filter A ──────────► Audio In 1      (the interval chime)

Pulse Out 2 ──► Slope B input        (Slope B: Loop off, Falling)
Slope B out ──► Filter B FM
Oscillator ───► Filter B ──────────► Audio In 2      (the end bell)

Audio Out 1 ──► mixer                (the chime, with its reverb and delay)
Audio Out 2 ──► mixer                (the bell, with its reverb and delay)
```

Set each **filter's cutoff low** (closed) so the voice is silent until its Slope opens it. Each pulse lasts 100 ms; a Slope in
**Falling** mode jumps up on the pulse and falls away, opening and closing the filter like a strike.

**The sounds themselves are up to you.** Every Workshop System is a little different (calibration, analogue tolerances), so there
are no knob-by-knob recipes here. Some directions that work well:

* **A chime** is bright, high and fairly short. Try a square or saw oscillator into the filter, a short Slope time, and the
  filter FM amount up enough that each strike opens the filter well above the oscillator's pitch. A little filter resonance adds
  a ringing "ping".
* **A bell or gong** is lower and longer, with partials that are *not* in tune with each other. That is what **oscillator FM**
  gives you: let one oscillator modulate the other (gently: a little goes a long way) and try pitch ratios that are not simple
  (not exactly an octave or a fifth). A longer Slope time lets it ring.
* **The Ring Mod** multiplies two oscillators: a quick way to clangorous, metallic tones. Try it as the bell's source, into the filter.
* Keep the **filter cutoff fully down** between strikes: every Workshop filter leaks a little, and the closer it is to closed,
  the less there is to hide (see section 6).
* Use **preview mode** (section 5) while you tune and dial in your sounds: the card strikes chime and bell alternately, once a second, through the effects.

Leave CV Out 1/2, CV In 1/2 and Pulse In 1/2 unpatched: the card does not use them (CV Out 1 and 2 sit at 0 V).

---

## 2. Quick start

1. **Flash the card** (hold the boot button, plug in USB, drag the `.uf2` onto the drive that appears), then **switch the power off and on**.
   All six LEDs breathe slowly: **ready**.
2. **Tune and dial in your sounds:** triple-tap **Z down** for preview mode. Chime, bell, chime, bell... One tap ends it.
3. **Set the timer:** flip **Z up**. Main = number of intervals, X = minutes, Y = extra seconds (section 3). Flip Z back to the middle.
4. **Start:** tap Z down. The LEDs count down 10 seconds, then the double chime. Breathe.

---

## 3. Setting the timer (Z up, while stopped)

| Knob | Sets | Range | Click |
|---|---|---|---|
| **Main** | number of intervals | 1 - 36 | low |
| **X** | minutes per interval | 0 - 36 | middle |
| **Y** | extra seconds per interval | 0 - 60, in tens | high |

* **Every step makes a soft click**, a slightly different note for each knob, so you can set the timer with your eyes closed.
* **The LEDs show whichever knob you touched last** (the number of intervals when you first flip Z up).
* **Clockwise always adds**, anticlockwise always takes away.
* **Flipping Z up never changes a setting.** A knob only takes over once you turn it a little; from then on its position is the
  value. (If a knob is already at the end you want, nudge it away and back.)
* **0 minutes** gives intervals of just the seconds (no LEDs). 0 minutes and 0 seconds counts as 10 seconds.
* Settings can only be changed while the timer is stopped; Z up during a session is ignored.
* At power-up: **6 intervals of 5:00**. Settings are not saved when the power is off.

### Reading the LEDs

The six LEDs, read like a page:
```
 1  2
 3  4
 5  6
```

**Intervals and minutes, 1 to 36:**
* **1 to 6:** that many **medium** LEDs.
* Above 6, each **bright** LED stands for a **six**. The rest (1 to 6) is shown by **blinking** one of the bright LEDs
  (bright LED 1 blinking = +1, LED 2 = +2 ...) or, when there are not enough bright LEDs for that, by **medium** LEDs after them.
* **0 minutes:** no LEDs.

| Value | LEDs (1 2 / 3 4 / 5 6) | Read as |
|---|---|---|
| 4 | medium medium / medium medium / off off | 4 |
| 6 | all six medium | 6 |
| 7 | **blinking** off / off off / off off | one six + 1 |
| 8 | bright medium / off off / off off | one six + 2 |
| 12 | bright medium / medium medium / medium medium | one six + 6 |
| 14 | bright **blinking** / off off / off off | two sixes + 2 |
| 15 | bright bright / medium off / off off | two sixes + 3 |
| 20 | bright **blinking** / bright off / off off | three sixes + 2 |
| 30 | bright bright / bright bright / medium medium | four sixes + 6 |
| 36 | bright bright / bright bright / bright medium | five sixes + 6 |

**Seconds:** one **medium** LED for every 10 seconds (none = 0 s, all six = 60 s).

---

## 4. A session

| You do | The card does |
|---|---|
| **Tap Z down** | 0.4 s later it starts: **10 seconds to settle** (all six LEDs bright, going out one by one), then the **double chime** (two strikes of the chime, 0.6 s apart). The first interval starts with the first strike. |
| (wait) | A **chime** at the end of each interval, a **bell** at the end of the last one. A dim bar fills slowly over the whole session; each strike gives a soft flash. At the end, all six LEDs glow and fade out. |
| **Tap** | **Pause** (the bar blinks slowly). Tap again to **resume**: the timing carries on exactly where it stopped. The settling period can be paused too. |
| **Hold Z down for 3 s** | **Stop**. From 0.8 s the LEDs fill up so you can feel it coming; let go before they are full to cancel. The next start begins with the 10 seconds again. |

A single tap acts **0.4 s after you let go** (the card waits to see whether a triple tap is coming).

---

## 5. Preview mode (triple tap, while stopped)

Tap Z down three times quickly. The card strikes the **chime** (Pulse Out 1), then 1 s later the **bell** (Pulse Out 2), and so
on, through the effects: tune and dial in your oscillators, filters and Slopes, and the reverb and delay (section 6), until they sound right.
The LEDs show the left column for the chime's second, the right column for the bell's. **One tap** (or a 3 s hold) ends it.

---

## 6. Effects (Z in the middle)

Each voice has its own reverb and delay: **In 1 → Out 1** (chime) and **In 2 → Out 2** (bell); they never mix. The effects can be
adjusted at any time with Z in the middle, also during a session.

**Main chooses a page; X and Y adjust it.** Main has three clear positions; anywhere else it changes nothing:

| Main | Page | LED sign | X | Y |
|---|---|---|---|---|
| **fully anticlockwise** | Reverb | top row | decay time, 1 s ... 25 s | brightness |
| **12 o'clock** | Mix | middle row | reverb level | delay level |
| **fully clockwise** | Delay | bottom row | echo time, 60 ms ... 1 s | feedback, 0 ... 93% |

* **X and Y keep their settings until you pass through them** ("soft pickup"), so nothing ever jumps when you change page.
  When you touch X or Y, the LEDs show that setting as a bar. **The bar flickers until your knob has caught the setting**: turn
  towards it until it stops flickering, then the bar follows your knob.
* Both mix knobs fully anticlockwise = no effect at all.
* Changing the echo time while echoes ring makes them glide, like tape. Feedback never runs away: the repeats always fade.
* At power-up: a medium-long (about 8 s), darkish reverb at a moderate level; the delay is off. Not saved when the power is off.

### Silence between the sounds

No Workshop System filter (or Ring Mod) closes completely: a faint tone always leaks through, and a long reverb would happily
sustain it. So the card keeps its outputs **completely silent except when it has just struck one of your voices**: each pulse
opens the way, and it closes again once that sound has faded back to the level of the leak (at the latest 20 s after the pulse).
The chime and the bell themselves pass through untouched.

This means **sounds you trigger by hand** (a Slope's button) **are not heard**, whether the timer is running or not. To listen
to your voices, use preview mode.

---

## 7. LEDs at a glance

| What the card is doing | What you see |
|---|---|
| Stopped, ready | All six breathe slowly and dimly |
| Setting the timer (Z up) | The number for the knob you touched last (section 3) |
| Settling (10 s after start) | All six bright, going out one by one |
| Each strike (start, intervals, end) | A soft flash of all six |
| Running | A dim bar filling up over the whole session |
| Paused | The bar blinks slowly |
| Holding Z down to stop | A bright bar filling up over 3 s |
| End of the session | All six glow, then fade out |
| Preview mode | Left column for the chime's second, right column for the bell's |
| Effects: Main arrives on a page | Top row = reverb, middle row = mix, bottom row = delay (1.2 s) |
| Effects: X or Y touched | A bar of that setting (flickers until the knob has caught it) |

Everything is kept dim on purpose: this card is used with eyes closed or the lights low.

---

## 8. Jacks

| Jack | Use |
|---|---|
| **Audio In 1** | Your interval chime |
| **Audio In 2** | Your end bell |
| **Audio Out 1** | The chime, with its reverb and delay |
| **Audio Out 2** | The bell, with its reverb and delay |
| **Pulse Out 1** | Strikes the chime: the double chime at the start, the end of every interval but the last, and preview mode |
| **Pulse Out 2** | Strikes the bell: the end of the session, and preview mode |
| CV Out 1/2 | Not used (0 V) |
| CV In 1/2, Pulse In 1/2 | Not used |

---

## 9. Troubleshooting

| Problem | Try |
|---|---|
| No chime or bell at all | Check the wiring in section 1: Pulse Out → Slope input, Slope set to **Falling** (not looping), Slope out → filter FM, filter cutoff low but the FM amount up. Try preview mode. |
| I trigger my Slope by hand and hear nothing | That is the silence keeper (section 6). Use preview mode. |
| A knob does nothing | Set-up: turn it a little more (it takes over after a small deliberate move), or nudge it away and back if it is already at the end. Effects: soft pickup: turn towards the bar until it stops flickering. |
| Main does nothing on the effects | Main only works at its three clear positions: fully anticlockwise, 12 o'clock, fully clockwise. |
| Turning knobs in Z up does nothing | Settings can only be changed while the timer is stopped. Hold Z down 3 s to stop. |
| A tap seems slow | It acts 0.4 s after you let go (so a triple tap can be recognised). |
| The chime changes pitch over the session | Something is modulating your oscillator's pitch (an old patch cable to a CV jack?). |
| The tail of a very long bell is cut | The way out closes at the latest 20 s after the bell's pulse (the reverb's own tail carries on). |

---

## 10. Building from source

The firmware is C++ on the Pico SDK with Chris Johnson's
[ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard) library (v0.4.0, included in `lib/`).

```sh
scripts/build_uf2.sh            # fetches the toolchain the first time; builds out/qi_timer.uf2 (and a 192 MHz variant)
cd host && make fast && ./test_render_fast quick       # the PC test suite (about 1 minute)
```

* Every tuning value is in `qi_config.h`; the card logic is in `qi_engine.h` (timer, switch, set-up, LEDs) and `qi_fx.h`
  (reverb, delay, silence keeper); `qi_card.h` connects them to the hardware.
* It runs at 144 MHz, with the audio on core 0 and the knobs and LEDs on core 1 (worst case 77% of the time available per sample,
  measured with `scripts/bench_cycles.py`).
* Build options: `-DCARD_192MHZ=ON` (not recommended: it caused a whine on CV Out 1 on at least one unit),
  `-DCARD_KEEPER_SEND_ONLY=ON` (the silence keeper only acts on the effects, the dry sound always passes).
* `HANDOFF.md` and `docs/DESIGN_HISTORY.md` tell the story of every design decision and every bug found.

---

## Credits and licence

Designed by **Joe Hedinger (isbn)**. Firmware written with AI assistance (Claude, Anthropic), tested on hardware by the designer.

With thanks to **Tom Whitwell** for creating the Workshop System.

* [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard)
  by Chris Johnson (MIT licence, see `lib/ComputerCard.LICENSE`).
* `scripts/bench_cycles.py` is adapted from the cycle counter of Matt Allison's
  [Equanimity](https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/112_Equanimity) card (MIT licence).

MIT licence: see `LICENSE`.
