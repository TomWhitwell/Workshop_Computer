# Design history (decisions and the owner's words, in order)

## Before code
* Brief agreed (docs/ORIGINAL_BRIEF.md). The owner dropped internal samples, sample-select, pitch control and drones.
* Claude's first reply asked 13 questions; the onboarding pack says the owner prefers **one question per reply**: from then on, defaults + one question.
* Defaults adopted (owner silent = accepted): the 10 s countdown is called the **settling period**; the recording script will add
  **"zero"**, singular **"minute"/"interval"**, and **"paused"/"stopped"** (use of the last two not decided);
  **dry always at full level**, wet = separate send levels; reverb Y = **darkness**; delay feedback capped ~93% with darkening;
  CV progress ramps **included**; **no** power-up gesture for speech-off; power-up default 5:00 x 6; Pulse 1 is also the start signal.
* **Owner decision:** set-up knobs **take over immediately** (no soft pickup in Z-up), with the value spoken.
  Implementation: on entering set-up (or a new X zone) a knob is *armed* and changes nothing until moved more than
  `kTakeoverMove`; then its position is the value. This keeps the brief's rule "never changes a value by accident".
* ComputerCard **v0.4.0** chosen (the official current copy; a superset of v0.3.0 for everything this card uses).

## Stage 1 (timer, set-up, pulses, LEDs)
* A press longer than **0.8 s** is not a tap (otherwise abandoning a 3 s hold at 2.9 s would pause the session).
  From 0.8 s the LEDs show the hold filling.
* Z-up while running/paused is ignored (as the brief assumed). If the session ends with Z still up, set-up wakes up then.
* The read-out speaks a value once the knob has rested **0.35 s** (one announcement per turn, not one per notch);
  entering a zone speaks that field's current value at once; entering set-up asks for a summary.

### Bugs found
| # | Bug | Found by | Fix |
|---|---|---|---|
| 1 | Take-over distance 80 was too far: from near an end stop you could not set 1 interval | `timeline` test (set-up helper) | `kTakeoverMove` 80 -> 40; documented "nudge away and back" |
| 2 | With X in the dead zone the LEDs still showed the minutes bar | `leds` snapshot | display follows X |
| 3 | Test-event names were declared only in the test build: the **card build failed** | first real ARM build | names declared always; new `make cardcheck` compiles card code without test macros |
| 4 | The real `ComputerCard.h` next to `qi_card.h` hid the PC mock (quoted includes search the file's own folder first) | `make cardcheck` | real header moved to `lib/` |
| 5 | A function-level `static` card object pulled ~67 KB of C++ runtime (thread-safe statics, demangler) into RAM (36.8% -> 11.1%) | linker memory table + `nm` | plain local object in `main()`; big buffers will be globals |
| (tests) | CV hold measured mid-tap; glue audio test outside 12-bit range; fuzz condition inverted | reading failures | fixed the tests |

## Stage 1 on hardware
* **Owner (8 Oct): "Tried it on the card. It works!"** First card from the onboarding pack to run on real hardware.

## Stage 2 (effects)
* **Owner: "Start stage 2, please!"** (plan as proposed: three pages in Z middle, soft pickup, gentle defaults, delay off).
* **Signal flow** per channel: input -> delay -> echoes; the reverb is fed by the input **and** the echoes (so echoes sit in the same space).
  Dry is never changed. Both effects run at 24 kHz; channel 1 on even samples, channel 2 on odd (even CPU load).
* **Reverb** adapted from Story OST's FDN (4 lines, Hadamard, damping, wandering lengths, two diffusers), with these changes:
  overflow-safe multiplies (Story OST's `MulTZ15` overflows above ~65,000 but its clamp is +/-1,000,000); internal precision x64 (Q6);
  input compensation `(1-g^2)^0.15` (calibrated so the reverb sounds equally loud at every decay time, within 0.8 dB);
  feedback boosted to make up the darkening filter's low-mid loss at 150 Hz (so the decay label is true for the body of a bell),
  capped so deep bass rings at most 1.33x the setting.
* **Delay** 16-bit, 60 ms - 1 s (exponential), feedback 0-93% with darkening and a soft knee in the loop; time changes glide (~85 ms).
* **Levels:** full reverb mix ~ +3 dB over a dry bell; default mix ~ -5 dB. Full delay mix: first echo = dry level. Mix knobs squared, exactly 0 at the bottom.
* **Output:** dry untouched; only the wet part is limited, softly, into the headroom the dry signal leaves.
* **Clock: 192 MHz at 1.15 V is now the default** (measured: worst 73% of the per-sample budget; 94% at 144 MHz). 144 MHz kept as a fallback build.
* LEDs: page indicator (columns/row) and a value bar that flickers until soft pickup catches the value.

### Bugs and problems found in stage 2
| # | Problem | Found by | Fix |
|---|---|---|---|
| 6 | The output limiter also squashed loud **dry** peaks (> 78% of full scale) | `fxdry` | limit only the wet part, into the headroom the dry leaves (`AddWet`) |
| 7 | Darkest setting: a leftover value **circulated forever** (dead band of a small filter coefficient) | `fxreverb` (darkest never silent) | 4 extra bits in the damping filter state |
| 8 | Long decays came out much shorter than labelled (13.9 s for "25 s") | `fxreverb` | precision x16 -> x64, and feedback compensation for the darkening filter's loss (now 20.8 s / 21.8 s low band) |
| 9 | Longer decays were up to 10 dB quieter | `fxlevels` | compensation exponent calibrated (0.15) |
| 10 | A constant voltage at an input (the inputs pass DC) **pinned the reverb at its limit** | `fuzz` (24 million clamp hits) | high-pass on the effect send (two stages, ~30 Hz), DC blocker on the wet return, filter steps that always settle exactly |
| 11 | A negative leftover in the DC filter could stay forever (plain `>>` rounds the wrong way) | `fxstable` (never silent) | `SettleStep` |
| 12 | Delay hard-clipped inside its loop on sustained loud tones at 93% | `fxstable` | soft knee in the loop |
| 13 | Tests ran the reverb with empty tables when run on their own | `fxreverb` alone gave silence | tests initialise the tables |
| 14 | **CPU: 100% of the budget at 144 MHz** (worst sample) | `scripts/bench_cycles.py` (adapted from Equanimity) | branch-free helpers + local pointers (bit-identical output, 15% faster: 94%); 192 MHz default (73%) |

## Reading the Robopera update (11_UPDATE_from_Robopera.md) and the AI directive V1.9 (8 Oct)
* Most Robopera lessons were already met independently (build in sandbox, v0.4.0, card in main(), host-mock shadowing, integer traps).
* **New for this card (proposals, not decisions):**
  - The brief's patch line is not physical: a Slope is an envelope, not a VCA. Recipes: chime = square -> filter (Slope -> filter FM);
    bell = sine -> Ring Mod (Slope -> bottom input); Slopes **Loop off, Falling**; bell on the exponential (bottom) Slope.
  - **Leak hum** between alerts (no VCA closes fully) would be sustained by long reverbs. Proposal: a *timer-aware* silence keeper
    (opens on the card's own pulses, envelope follower as backup) before the effects. Open choice for the owner: send only, or dry too.
  - **Clock:** the directive prefers 144 MHz for fewer ADC tonal artifacts; stage 2 moved to 192 MHz for CPU. Owner to A/B by ear.
  - **DAC zero-crossing glitch** (directive errata): ComputerCard v0.4.0 has no workaround; quiet tails sit at zero. Fix = small DC offset, if heard.
  - Model the analogue voice on the PC (Robopera §8) for honest demos and gate tests.
* Added checks 8-11 to docs/STAGE2_HARDWARE_TEST.md.

## Owner's patch (8 Oct, from the stage 1 hardware test)
* **Slope output -> filter FM input; Slope in Falling mode.** Triggered by the card's 100 ms Pulse Out: works on hardware.
  So the chime/bell "VCA" is the filter (cutoff closed, opened by the Slope), the same patch Robopera measured as leaking a
  very low hum between notes. Recipes in the README will use this patch. (Oscillator output, sine or square, not yet known;
  assumed square, since a filter envelope barely changes a sine.)

## Silence keeper (8 Oct; owner: "Sounds good, please go ahead with that while I test stage two")
* **PC model of the owner's patch** (host/test_keeper.inc): square oscillator -> 4-pole low-pass opened by a Falling Slope
  (triggered by the card's own pulses, closing the loop), plus a leak of the oscillator, 50 Hz hum and hiss (~ -55 dBFS together).
* **Keeper** (qi_fx.h, per channel, before the effects): envelope follower; learned hum level (quiet pulls it down fast, it creeps
  up slowly, capped at 30 units); open at 2 x hum + 5, close below 1.5 x hum + 3 for 30 ms; fade in ~0.7 ms, out ~43 ms, snap to 0.
  **Timer-aware:** the card's own pulse (Pulse 1 -> channel 1, Pulse 2 -> channel 2) opens it **fully at once** and holds it 0.4 s.
* **Finding:** a ~0.7 ms fade-in softened the chime's attack when gating the dry signal (36 samples at -9 dBFS); opening instantly
  on our own pulse fixed it (the sound has not started yet; only the leak is there). Hand-triggered sounds keep the fade-in.
* **Results (model):** effect between alerts -67 dBFS -> **exact silence**; with dry gating Out 1 between alerts -56 dBFS -> **exact silence**;
  effect during the chime changed <= 0.04 dB (also with a 4x louder leak); dry gating leaves the chime bit-exact and only acts once
  its tail is at -49.5 dBFS; hand-triggered sounds -0.02 dB; held tones down to -30 dBFS never gated.
* **Key insight:** what you hear between alerts is mostly the **dry** leak passing through the card (-56 dBFS); the reverb's sustained
  copy is ~11 dB below it. So the send-only keeper removes the reverb wash, but only the dry option makes the gaps silent.
  Default stays **send only** (the brief's "dry never changed"); `-DCARD_KEEPER_DRY=ON` builds `qi_timer_keeper_dry.uf2` for the owner's A/B.
* CPU (emulated): worst 75% at 192 MHz (was 73%); **98% at 144 MHz**: the 144 MHz build is now only for check 9 (listening for whistles).

## The owner's simplification (8 Oct, with slides "Untitled presentation.pdf")
Owner's words, in short: *set intervals, minutes and seconds with just the LEDs and a subtle "click" (like the dial of a safe
lock), a different click for each; Z up = settings, Main = intervals, X = minutes, Y = seconds; a "double chime" at the start;
a quick triple tap on Z down = "sound preview tweaking mode" pinging the two voices one then the other every second; a single tap returns.*
Then: **"Rule A, please."** and **"Please make the click sounds very soft and calm."** Earlier the same day: **"I prefer send+dry"** (silence keeper).
* **The voice stage is dropped** (no recording, no placeholder voice, no speech data in flash).
* **Rule A** (slides 1-2; slides 3-4 followed a different rule by one): 1-6 = medium LEDs; above 6 each full LED = a six; the rest
  is shown by blinking full LED number n, or by medium LEDs after the full ones. Every value 1-36 has its own picture (tested).
* Ranges: intervals 1-36 (Main), minutes 1-36 (X), seconds 0-60 in tens (Y). Longest session 36 x 37 min = 22 h 12 min =
  3.84 billion samples: fits the unsigned 32-bit counter (limit 4.29 billion).
* **Clicks:** short sine bursts (0.67 ms attack, ~5 ms fade, ~19 ms audible), -34 dBFS peak, A4 / D5 / G5 for intervals / minutes /
  seconds; only in set-up; never into the effects or through the keeper.
* **Double chime:** Pulse 1 at the end of settling and again 0.6 s later; interval 1 starts at the first.
* **Taps:** counted together if less than 0.4 s apart; act 0.4 s after the last release. 3+ while stopped = preview; 2 = one tap;
  3 while running = one tap (pause). Preview: Pulse 1 at 0 s, Pulse 2 at 1 s, repeating every 2 s; a tap or a 3 s hold ends it.
* **Keeper:** send + dry is the default (`-DCARD_KEEPER_SEND_ONLY=ON` for the old behaviour). Opening is instant.

### Bugs and problems found
| # | Problem | Found by | Fix |
|---|---|---|---|
| 15 | With dry gating, the first 1-3 samples of a hand-triggered attack were zeroed (a Falling Slope opens the filter at once, to -15.6 dBFS) | `keeperhand` (new dry check) | the dry path checks every sample against the opening threshold and opens on the spot (now the loudest gated sample is -43 dBFS, in the faded tail) |
| 16 | Fuzz: a scripted tap could become the 3rd of a random multi-tap and (correctly) start preview mode | `fuzz` | the script lets pending taps settle; fuzz now also produces quick multi-taps and must reach preview mode |
| 17 | Fuzz range check still used the old ranges (30 / 3 / 20) | reading the code | uses the config constants |
| (tests) | click test: a 140-unit nudge did not cross a 36-step boundary + hysteresis; pitch measured over the fade read low; LED snapshots taken while a session was still running | test output | fixed the tests |

## Reading the Robopera pack, file 12 (8 Oct): checked against this card
* **Stack:** card object 944 bytes on core 0's 2 KB stack (main frame 1,008 B); measured the audio interrupt's worst chain with
  `-fstack-usage` at ~1.4 KB total -> ~600 B spare. Fine (Robopera's 3.7 kB object would not have fitted). Logged; no change.
* **DC blocker precision** ("12+ bits; 4 bits tracked the median"): reproduced the measurement on our wet DC blocker (bell, longest
  reverb): its corrections looked large (-25 dB below the wet), but compared with an ideal double-precision high-pass the 4-bit version
  is within 0.1 unit (-93 dB): our wet is already Q6, so it has 10 bits beyond the output. Not affected; no change.
* **192 MHz whine on CV Out 1 on the owner's unit** (Robopera): this card defaults to 192 MHz and uses CV Out 1. At 144 MHz the card
  would now need 101% of the budget at worst (mean 83%): reverb Tick + reads + diffusers ~860 cycles/sample. Owner to decide the route.
  Interim: test sheet says not to use the CV outs with the 192 MHz build.
* Adopted for the process: one "latest" output folder; test sheets end with a copy-paste answer block.
* Not relevant now: flash/ADC trap (no saving in v1), USB on core 1 (no web editor), web-editor and children's-design lessons.
* For stage 4: watchdog + breadcrumbs (release safety net), `npm run validate-info` for info.yaml, releases/NN_name layout,
  credits (ComputerCard; Patch Notes / Vincent Maurer if we draw the panel from it), "flip Z up, flick it down" wording.

## Back to 144 MHz (owner: "Route A, please", 8 Oct)
* **Step 1, the UI on core 1** (official guidance; Robopera does the same): the engine now has two entry points. `Process()` on core 0
  every sample (audio, effects, keeper, clicks, timer, switch, pulses); `Control()` on core 1 every 32 samples, paced by core 0's sample
  count (knobs, set-up, LEDs, CV, effect pages, smoothing, reverb wander, keeper learning). Core 1 is the only writer of LEDs and CV.
  Hand-offs between the cores are event counters (pulse flash, end glow, pause blink) and one 32-bit click command (sequence << 2 | setting),
  so neither core can overwrite the other's update half-way. Core 1 waits on the timer (busy_wait_us(50)) between steps, not polling RAM.
* **Measured (emulator, 11 scenarios):** worst sample at 144 MHz **101% -> 80%**, mean 83% -> 75%; core 1 uses 1.8% of its budget.
  The saving (~600 cycles worst) was larger than estimated: the per-sample LED and CV writes and the control jobs cost more than they looked.
* **Step 2 (a cheaper reverb) was NOT needed:** the sound is unchanged (no A/B required).
* **144 MHz is the default again**; 192 MHz is a build option only (`-DCARD_192MHZ=ON`).
* All 47 host checks pass unchanged (the tests call Control() every 32 samples exactly as core 1 does); sanitizer clean.
* Not modelled by the emulator: the two cores competing for memory. To be confirmed on hardware (listen for crackles with long tails).

## Owner's hardware test of the simplified design (19 Oct)
Owner: *"Clicks are wonderful. Triple-tap works well. Start two chimes work well."* And four reports:
1. **"Y switch is the wrong way around (at least sometimes)! Clockwise seems to remove the effect... clockwise always increases/adds."**
   Audit of every knob: only **reverb page Y** broke the rule (it was darkness). Now **brightness** (knob stores brightness;
   darkness = 4095 - Y inside; default unchanged: 1495 = the old 2600 darkness). "Sometimes" may also be soft pickup (a knob does
   nothing until it passes its stored value).
2. **"A very low whine / drone between chimes, slowly creeping up in pitch, another creeping down, looping across the interval"**
   (CV Out 1/2 patched into oscillator pitch). **Reproduced** (`cvwhine`): CV Out 2 rises over each interval -> the oscillator glides
   up (FM difference tones glide down) and its leak past the closed filter grows with pitch, to -34 dBFS, above the keeper's hum cap
   (30 units, ~ -37 dBFS) -> the keeper took the leak for a sound and let it through: Out 1 between chimes -35.6 dBFS.
   **Fix:** during a session (settling, running, paused, preview) only the card's own pulses open the keeper; while closed it follows
   the leak level freely (cap 1000 units); it closes at the latest 20 s after a pulse. Stopped: as before (hand triggers).
   After: Out 1 between chimes **exact silence**; all keeper guarantees unchanged (chime bit-exact, effect <= 0.04 dB, hand triggers).
3. **"Let users set 0 minutes... 10-60 seconds (no LEDs)."** X = 0-36 (37 steps); 0 = no LEDs; 0 min + 0 s = 10 s (kMinIntervalSeconds).
4. **"Holding the Z switch down is not good practice, as it's spring loaded... Is that true?"** No source found: the directive describes
   Z as "momentary down, normal up" (a spring-return position made to be pressed and held); Story OST used a hold too. Alternative
   offered to the owner; awaiting the choice.
Checks: 50 host checks pass; sanitizer clean; CPU at 144 MHz worst 81%, mean 76%; core 1 1.9%.

## CV outs removed, hold stays (19 Oct)
Owner: *"Let's leave Z held down to reset."* and *"we can probably now drop the CV 1 and 2 out... we're using the analogue voices for
the chimes... we can remove them. This may help stop the whine noise, which was, in retrospect, a mistake of my patching."*
* CV ramps removed (engine, glue, tests). **Both CV outs are held at 0 V**, written once in `Setup()`: ComputerCard never initialises
  them, and its stored 0 means `val = 2047` (the top of the range), so "just stop writing" would have left them high.
* The session-aware keeper stays: any leak that grows during a session (e.g. a pitch knob turned up) stays silent (`cvwhine`, now driven
  by a synthetic pitch sweep). The `cv` ramp test is replaced by a glue check (CV outs at 0 V from start-up, never changed).
* 49 checks pass; sanitizer clean; CPU at 144 MHz worst 81%, mean 76%.

## Owner's second test of the simplified design (19 Oct)
Owner: *"Clicks are great. LEDs great. Knob feel perfect. Double chime good. Preview mode great. 0.4 s delay all fine. No crackle or
glitch or stutter."* (So 144 MHz with the UI on core 1 works on hardware.)
1. *"A hum/thumping after exiting preview mode... the oscillators and the 'beats' of the FM leaking through... only occasionally, and
   went away entirely during intervals. Make sure things go completely silent before/after interval sessions."*
   **Cause:** while stopped, the keeper still opened for any sound above 2 x the learned hum; a BEATING leak swells and fades, so the
   learned level kept dropping in the fades and each swell looked like a sound. **Reproduced** (`stoppedsilence`: 92% of samples leaked
   just after preview). **Fix:** only the card's own pulses open the keeper, at all times; while closed it follows the leak freely;
   closes at the latest 20 s after a pulse. Simpler code (the per-sample opening, hum cap and creep are gone); CPU 81% -> 77%.
   Consequence: hand-triggered sounds are not heard (tune in preview mode). `keeperhand` now checks they stay silent; `keeperheld` removed.
   **Near miss:** the effect unit tests then "passed" with a closed keeper (nothing reached the reverb); `RunChannel` now turns the keeper
   off and `fxstable` requires the reverb to have received the tone.
2. *"The Main knob... has to be fully (or very nearly fully) clockwise or anti-clockwise... nothing at 12 o'clock."* Reverb page only
   in the first ~8% of Main, delay page only in the last ~8% (measured by `mainzones`). **Open question:** the mix page sits at
   12 o'clock (brief); where should the two mix levels go?
3. *"Recommended patches: drop this plan... every Workshop System is slightly different... suggest careful use of oscillator FM, Ring Mod
   and filter cut-off / FM amount."* Agreed: knob-by-knob recipes cannot be replicated; the wiring (Pulse Out -> Slope, Falling mode,
   Slope -> filter FM, voice -> Audio In) can. README: wiring + sound-design guidance + "tune in preview mode".

## Pages and LEDs (19 Oct)
* Owner: *"Option A is best; I like the idea of clear pages using LEDs."* The mix page stays at 12 o'clock as a narrow window
  (~8% of Main's travel, like the two ends). Anywhere else Main changes nothing (measured by `mainzones`).
* Owner: *"Could the LEDs signalling the three pages not just be reverb = top 2 LEDs, mix = middle 2, delay = bottom 2?"* Yes: page signs
  are now rows in the order of Main's travel (was left column / middle row / right column). `fxpages` checks all three rows.
* Owner: *"Could the 'ready' state just be all LEDs, not just 2?"* Yes: all six breathe together, dimly (this also removes the only overlap
  with the reverb page's top row). The `leds` test checks all six are equal at every moment.
* 48 checks pass; sanitizer clean.

## Stage 4: the release (20 Oct)
* Owner: *"Please start on this."* Read the repository's own rules first (sparse clone): `documentation/info.yaml.md` (structured
  panel metadata lets the site draw the panels: no hand-made SVG needed), README (card numbers are claimed first-come by pull request),
  recent card layouts (Equanimity: source, `UF2/`, `lib/ComputerCard.LICENSE`, tests), existing tags (reuse: utility, trigger, effect,
  reverb, delay). Folder `114_Qi_Interval_Timer` (first free number after the recent cards).
* `validate-info`: no errors for this card (1 warning: contact); errors only in four older cards. `sync-curation` adds our entry to
  `flairs.yml` (to commit with the PR). The full site build needs a full clone (not done).
* Demos re-rendered from the current code (the old setup-clicks demo began with a hand-triggered chime, which the card now keeps silent).
* Slip caught: an edit replaced a demo function "to the end of the file" and deleted two later tests; the build failed, the file was
  restored from git and the edit redone on exact boundaries.
* Placeholders: `[Creator]` (name not yet known). Version 0.9.0, Status Beta, draft true until the final hardware test.

## Credits and number (20 Oct)
Owner: *"For the creator credit, it's 'Joe Hedinger (isbn)'. ... I'd like to try and get number 389. ... Please could the credits say
somewhere 'With thanks to Tom Whitwell for creating the Workshop System.'"*
* Creator filled in (info.yaml, README, LICENSE); the thanks added to the README credits.
* 389 checked free on 20 Oct (main branch: no 38x folders; GitHub search: no open pull request mentioning 389). Folder: `389_Qi_Interval_Timer`.

## Renamed (20 Oct)
Owner: *"Can I change the card's name to either 'QiGong Timer' or 'QG Timer' if the first option is too long?"* "QiGong Timer" (12 characters)
is well within the catalogue's range (median 9, several 18-38), so: **QiGong Timer**, folder `389_QiGong_Timer`, firmware
`UF2/qigong_timer.0.9.0.uf2`. (The internal build target stays `qi_timer`.)
* Rebuild after the rename: differs from the released UF2 in **2 bytes only, the build date** the Pico SDK stamps into the binary info
  ("Oct 19" vs "Oct 20"); the program is identical. (Earlier "byte-identical" checks rebuilt on the same day.) The released UF2 is kept:
  it is the file under hardware test. To compare builds across days, ignore the date string.
