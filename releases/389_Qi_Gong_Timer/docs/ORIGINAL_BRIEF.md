# New card brief: Qi Interval Timer (working title)

*(Re-created verbatim from the brief the owner uploaded at the start of the project. Decisions made since are in DESIGN_HISTORY.md.)*

*Filled in from the owner's description. Status: **agreed direction, not yet built**. Items marked **[assumed]** are my defaults and need the owner's OK; **[open]** are real questions.*

## 1. The idea
An **interval meditation timer for Qi Gong**, set entirely with the physical knobs. It counts a session of N intervals, and at the end of each interval it sends a **pulse** so the player's own patched Workshop System sounds (an interval chime and an end bell/gong) can be triggered. It also provides **reverb and delay** for those patched sounds, which come in through the Audio inputs and go out through the Audio outputs. The card makes **no sounds of its own** (apart from spoken set-up prompts).

**Who uses it:** the owner *and other people* (a community card), so it needs clear documentation, safe defaults and a patch guide.

## 2. How a patch looks
* **Interval chime:** Workshop oscillator 1 -> filter -> slope (the slope is triggered by **Pulse Out 1**) -> **Audio In 1**.
* **End bell/gong:** oscillator 2 -> filter -> slope (triggered by **Pulse Out 2**) -> **Audio In 2**.
* The card's effects are **always live**: In 1 -> reverb+delay -> **Out 1**; In 2 -> reverb+delay -> **Out 2** (two independent chains so the two sounds never mix).

## 3. Timer behaviour
* **Settings:** minutes 1-30 (30 notches) + extra seconds 0/15/30/45 (4 notches) -> interval length 1:00 to 30:45; number of intervals 1-20 (20 notches). Total = length x number (max 10 h 15 min).
* **Start (tap Z down, acted on at release):** a **10-second set-up period** (silent; LEDs count down), then **Pulse 1** = "the session has started", then interval 1 begins. *(The owner asked whether this makes sense: yes: it lets you settle and breathe before the first alert, as most meditation timers do.)*
* **During the session:** Pulse 1 at the end of each of intervals 1 to N-1; **Pulse 2** at the end of interval N (End alert). **[assumed]** the end of the last interval gives only the End alert (no Interval alert).
* **Tap Z again:** pause / resume (the set-up period can be paused too; resuming does **not** repeat it). **Hold Z down 3 s:** stop and return to the start position; the next start repeats the 10 s set-up.
* Setting values is only possible while stopped. **[assumed]** Z up while running is ignored.
* **Power-up defaults [assumed]:** 5:00 x 6 intervals (to be tuned). **No saved settings in v1** (flash writes are risky).

## 4. Controls
* **Z up = set-up.** **X** selects what **Main** edits: X fully CCW -> Main sets **minutes**; X fully CW -> Main sets **number of intervals**; X in the middle -> nothing changes (dead zone, **[assumed]**). **Y** sets the **extra seconds**. Notched knobs with hysteresis; soft pickup so values never jump. Every change is **read back in speech** (see 6).
* **Z middle = normal + effects.** Main position picks the effect page: **Main full CCW = reverb** (X = time, Y = depth); **Main at 12 o'clock = wet/dry** (X = reverb mix, Y = delay mix; fully CCW = dry, fully CW = wet); **Main full CW = delay** (X = time, Y = feedback). Other Main positions change nothing. Soft pickup. Both channels use the same settings. **[open]** what "depth" should mean for the reverb: I propose room size/darkness (wet/dry is separate).
* **Z down:** momentary: tap = start / pause; hold 3 s = stop. *(The owner's earlier "hold Z at reset for sample select" is dropped with the samples; the gesture is free for something else, e.g. **[assumed, optional]** hold Z at power-up = speech off.)*

## 5. Jacks
| Jack | Use |
|---|---|
| Audio In 1 / 2 | the player's interval chime / end bell (effects applied) |
| Audio Out 1 / 2 | effected chime / effected bell (and, during set-up, the spoken prompts **[assumed: dry, on both outputs]**) |
| Pulse Out 1 / 2 | Interval alert (and session-start) pulse / End alert pulse; **[assumed]** 100 ms |
| CV Out 1 / 2 | **[proposed bonus, not agreed]** session progress ramp (0 to ~5 V) / interval progress ramp: free slow modulators for patching |
| CV In, Pulse In | unused |

## 6. Spoken feedback (set-up read-out)
* The owner liked Utility Pair's **talking set-up menu** (it speaks the selected item through the left audio output). (Utility Pair is by Chris Johnson, source: github.com/chrisgjohnson/Utility-Pair; licence not checked.)
* **Stand-in for now: read the number digit by digit** (e.g. 25 -> "two ... five"). **Final voice: the owner will record these words themselves** (about 25): one to nineteen, twenty, thirty, forty, "minutes", "intervals", "seconds" (script and converter to be supplied).
* **Placeholder source:** the Free Spoken Digit Dataset (digits 0-9, English, 8 kHz, **CC BY-SA 4.0**: needs attribution and share-alike; **must be replaced before release**). No text-to-speech engine was available in the sandbox. Network is needed to fetch it (github.com / zenodo.org); until then, the placeholder can be synthetic beeps.
* **Technical risk:** with the standard `copy_to_ram` build, constant data is copied into RAM (264 KB). Speech of ~140-350 KB must therefore stay **in flash** (Pico SDK `__in_flash`), which is **unverified until a real build**. Fallback: lower sample rate / 4-bit coding, or fewer words.

## 7. Dropped (the owner decided)
Internal samples/synth, sample-select mode and pitch control (RAM), drone or looping backgrounds between alerts.

## 8. Constraints and notes
* CPU: light (two reverbs + two delays + a timer). RAM: two reverbs (~27 KB each at 24 kHz) + two delays (choose lengths, e.g. up to ~1 s: 16-32 KB each) ~ 120-190 KB: fine. Keep a lower-memory fallback.
* The reverb should be capable of **long, calm tails** (several seconds) that die to true silence (see the Story OST reverb: FDN, half rate, rounding toward zero, tiny gate).
* The effects must be good on **bell-like sounds** (long decays), with dry always at full level and independent reverb/delay wet controls.
* Timer: counts audio samples (crystal-based); keep 32-bit counters (10 h 15 min = 1.77 x 10^9 samples).

## 9. How we judge success
1. Every knob/zone does exactly what the table says and **never changes a value by accident** (hysteresis, dead zones, soft pickup). 2. The timeline is right: 10 s, start pulse, N-1 interval pulses, then the end pulse; pause/resume/stop work. 3. Effects behave on a bell patch: no clicks, long tails die to silence, delay feedback is safe (never runaway). 4. Set-up is usable **with the knobs and your ears alone**. 5. A stranger can use it from the README.

## 10. Deliverables
Source (git), `.uf2` (default + 192 MHz variant), README with a panel diagram and **patch recipes** (chime on oscillator 1 / bell on oscillator 2), LED legend, troubleshooting, `info.yaml` (check the real schema), demo audio, a recording script for the owner's voice, tests, handoff document; submission to the community card repository (card number via pull request).

## 11. Reference code to reuse (from the onboarding pack / Story OST)
FDN reverb (`story_engine.h`: `FdnRead`, `ReverbHalf`, `Reverb`), the 24 kHz echo, rounding toward zero + gate so tails die, soft pickup (`Pickup`), Z-switch handling (boot guard, tap on release, 3 s hold), state machine with fades, time-slice scheduler, host test harness + mock + sanitizer, event-timeline test (as for the stingers). New tests to write: **knob-notch behaviour** (hysteresis), **timeline test** (10 s + pulses), **effects impulse tests** (RT60, silence, stability), **fuzz**.

## 12. Next steps (proposed stages, each ending in something checkable)
1. Timer state machine + knob set-up (notches, zones, pickup) + pulses + LED display, with scripted timeline tests.
2. Reverb + delay on two audio channels with impulse-response tests and an audition (a synthesised bell through the effects, rendered offline).
3. Spoken read-out engine with the placeholder voice (digit by digit) and the recording script/converter.
4. README, patch recipes, LED legend, `info.yaml`, packaging; build the `.uf2` when the network allows.
