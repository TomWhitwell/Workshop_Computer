# HANDOFF: QiGong Timer (kept current; read this first)

## 0. First five minutes
1. Unzip; read this file, `docs/DESIGN_HISTORY.md`, `docs/ORIGINAL_BRIEF.md`. The onboarding pack (`Workshop_Card_Onboarding_*`) is the platform background.
2. Test the network: `curl -sI https://github.com | head -1`. (On 7 Oct 2026: github.com, raw.githubusercontent.com, codeload, pypi worked;
   **archive.ubuntu.com timed out**, zenodo.org was blocked, api.github.com rate-limited.)
3. Host tests: `cd host && make fast && ./test_render_fast quick` (about 10 s) and `make test_render && ./test_render quick` (sanitizer, about 70 s).
   Also `make cardcheck` (compiles the card code without test hooks). Expect `ALL TESTS PASSED`. Demos: `./test_render_fast demo` (WAVs in host/out/).
4. Firmware: `scripts/build_uf2.sh` (downloads ~280 MB of toolchain the first time; then ~1 min per build). Default = **192 MHz**.
5. CPU: `scripts/bench_cycles.py` (see its header: bench build `-DCARD_BENCH=ON`, `make tables`, a venv with unicorn + capstone).
6. Ask the owner what they found on hardware (`docs/STAGE2_HARDWARE_TEST.md`), then continue with the next stage.

## 1. The project in one paragraph
A program card for the Music Thing Workshop System **Computer**: an **interval meditation timer for Qi Gong**, set with the knobs alone.
It counts N intervals of a set length and sends **Pulse 1** (a double chime at the start, then each interval) and **Pulse 2** (end)
to trigger the player's own patched chime and bell; it adds **reverb + delay** to those sounds on two independent channels
(Audio In 1/2 -> Out 1/2) and keeps the quiet between them truly silent (silence keeper). Set-up is by **LEDs and soft clicks**.
A triple tap gives a **preview mode** for tuning the modules. A community card: documentation and safe defaults matter.

## 2. The owner
Creative, not a firmware expert (plain language, Claude does the building). Wants **one question per reply**, a short summary first,
tables, demos for audible changes, honesty about what is verified. Will test on real hardware and give short, precise feedback.

## 3. The owner's rules (do not undo)
1. Everything in the brief marked agreed (docs/ORIGINAL_BRIEF.md), as amended in docs/DESIGN_HISTORY.md.
2. **Set-up knobs take over immediately** (owner's choice, 7 Oct): no soft pickup in Z-up. Merely flipping Z up must never change a
   value (each knob is armed until it moves `kTakeoverMove`). **Set-up = Main intervals, X minutes, Y seconds; LEDs rule A; soft
   calm clicks; no voice** (owner, 8 Oct).
3. The card makes **no sounds of its own** except the very soft set-up clicks. No drones, samples or backgrounds (dropped by the owner).
4. Two **independent** effect chains. **Sounds pass bit-for-bit; the dry signal is gated only in the quiet between them**
   (owner chose send + dry, 8 Oct). The wet part is limited into the dry's headroom. **Only the card's own pulses open the silence
   keeper, at all times: silent before, between and after sessions** (owner, 19 Oct). Hand-triggered sounds are not heard; tune in preview.
5. Don't change tuning values or the design to make a build pass: show the first error, explain, propose.
6. Effects in Z middle: Main fully CCW = reverb (X time, Y brightness), 12 o'clock = mix (X reverb, Y delay), fully CW = delay (X time, Y feedback); soft pickup.
7. **Clockwise always adds / increases; anticlockwise removes / decreases** (owner, 19 Oct). Check every new control against it.
8. **Main has three clear positions only** (owner, 19 Oct): fully anticlockwise = reverb, 12 o'clock = mix, fully clockwise = delay
   (~8% of travel each); elsewhere nothing. **Page signs are LED rows**: top = reverb, middle = mix, bottom = delay. **Ready = all six breathing.**
9. **No patch recipes** (owner, 19 Oct): every Workshop System differs (calibration, analogue tolerances). The README gives the fixed
   wiring and considered sound-design guidance (oscillator FM, Ring Mod, filter cutoff / FM amount), not knob-by-knob patches.

## 4. What exists now (simplified design done, 8 Oct; stage 1 confirmed working on hardware by the owner; stage 2/3 not yet)
| Feature | Status |
|---|---|
| Timer: 10 s settling, double chime, N-1 interval pulses, end pulse; pause/resume (also during settling, no repeat); hold 3 s = stop | done, tested to the exact sample (incl. the 22 h 12 min maximum) |
| Z switch: boot guard 125 ms, bounce < 10 ms ignored, tap = 10 ms..0.8 s, taps < 0.4 s apart counted together and acted on 0.4 s after release; hold 3 s fires while held | done, tested |
| Set-up (Z up, only while stopped): Main = intervals 1-36, X = minutes 0-36 (0 = seconds only; 0:00 -> 0:10), Y = seconds 0-60 in tens; hysteresis 48; take-over 40 | done, tested (sweeps, jitter, ownership, take-over) |
| LED display rule A (docs/LED_LEGEND.md); soft clicks A4/D5/G5 at -34 dBFS | done, tested (rule vs slides, all 36 pictures unique, click level/length/pitch) |
| Double chime (2 x Pulse 1, 0.6 s apart); triple tap -> preview mode (chime / bell every second) | done, tested to the sample |
| CV Out 1/2: **removed** (owner, 19 Oct): held at 0 V, written once in `Setup()` (ComputerCard's own start value is the TOP of the range) | done, tested (`glue`) |
| LEDs: see `docs/LED_LEGEND.md` (proposal, dim by design) | done, snapshot test; **not seen on hardware** |
| **Effects** (qi_fx.h): per channel delay (60 ms-1 s, fb <= 93%, darkening, soft knee) -> FDN reverb (decay 1-25 s, darkness), fed by input + echoes; 24 kHz, channels on alternate samples | done, tested (RT60s, silence, stability, DC, clicks, independence, levels); **not heard on hardware** |
| Effect pages in Z middle, soft pickup, LED page/value display, ~5 ms parameter smoothing | done, tested |
| Output: dry untouched + wet limited into the dry's headroom | done, tested (exact dry at mix 0) |
| **Silence keeper** (per channel, before the effects; timer-aware; instant opening, per-sample on the dry path); **send + dry by default** (`-DCARD_KEEPER_SEND_ONLY=ON` for send only); **in a session only the card's pulses open it** (fix for the owner's CV-pitch whine) | done, tested on a PC model of the owner's patch (`test_keeper.inc`, incl. `cvwhine`) |
| Firmware | **builds**, 0 warnings, RAM 186 KB (73%; 153 KB effect buffers). **192 MHz default**, 144 MHz fallback |
| CPU (emulated, scripts/bench_cycles.py, 11 scenarios incl. set-up clicks and preview) | **144 MHz default:** worst sample **80%** (mean 75%) on core 0; core 1 1.8% |

## 5. Where things are
| File | Role |
|---|---|
| `qi_config.h` | every tuning number (timings, ranges, defaults, zones, hysteresis, LED levels) |
| `qi_engine.h` | `qi::Engine`: `Init(seed)`, `Process(Inputs, Outputs)` per sample; timer, switch, set-up, effect pages, LEDs; no hardware includes |
| `qi_fx.h` | the effects: `FxChannel` (delay + reverb), tables, overflow-safe/branch-free fixed-point helpers, `Pickup`, `AddWet`; global buffers |
| `qi_card.h` | thin ComputerCard glue: core 0 `ProcessSample()` -> `Process()`; core 1 `Core1Step()` -> `Control()` every 32 samples, writes the LEDs; CV outs set to 0 V once |
| `main.cpp` | clock (**144 MHz**, or 192 MHz at 1.15 V with `-DCARD_192MHZ=ON`), `Setup()` (launches core 1), `Run()`; bench hooks with `-DCARD_BENCH=ON` |
| `lib/ComputerCard.h` | **v0.4.0** (MIT, Chris Johnson), vendored. In `lib/` so host builds use the mock |
| `cardkit_dsp.h` | fixed-point helpers from the onboarding toolkit |
| `CMakeLists.txt`, `pico_sdk_import.cmake` | firmware build (copy_to_ram, XOSC delay x64, no USB stdio, `--print-memory-usage`) |
| `scripts/build_uf2.sh` | the build that actually worked (see section 10) |
| `scripts/bench_cycles.py` | emulated Cortex-M0+ cycle count of the real firmware (adapted from Equanimity, Matt Allison, MIT) |
| `host/` | `test_render.cpp` + `test_fx.inc` (tests, demos), `Makefile` (`fast`, `test_render` = sanitizer, `cardcheck`, `tables`), `mock/`, `dump_tables.cpp` |
| `docs/` | brief, design history, LED legend, stage 1 + 2 hardware tests, notes for the onboarding pack |

**Two cores:** core 0 `Process()` per sample: timer (+ preview), switch (+ tap counting), effects, keeper dry gate, click synth, outputs.
Core 1 `Control()` every 32 samples (1.5 kHz): set-up knobs (+ click requests), effect pages, smoothing, wander, keeper learning, LEDs.
Cross-core hand-offs: event counters (`pulseSeq_`, `finishSeq_`, `pauseSeq_`) and the click command (`clickCmd_`); never write the other core's state.
**Memory:** effect buffers are global arrays (`gFdn`, `gAp`, `gDel`): there is one engine on the card; host tests clear them per Sim.
Per sample: timer counter + switch + slice job + outputs.
**State:** `ST_STOPPED / ST_SETTLING / ST_RUNNING` + `paused_` flag. Settings latch at Start.

## 6. Verification (last run 7 Oct 2026)
| Test (`./test_render_fast <name>`) | Checks | Result |
|---|---|---|
| `switch` | boot guard, bounce, tap at release, 2 s press = nothing, hold = exactly 3.000 s, release after hold ignored, hold while stopped | pass |
| `timeline` | 1:00x3, 1:15x1, 2:45x2, 1:30x4 set **with the knobs**; every pulse at the exact sample; widths 100 ms | pass |
| `pause` | pause 7.15 s in settling + 23.16 s in interval: pulses shift by exactly that; blink; stop/restart repeats settling | pass |
| `notch` | minutes/intervals/seconds: every value in order up and down, both ends; +/-30 jitter on every boundary: 0 accidental changes | pass |
| `zones` | X middle does nothing; slow X sweep with jitter = exactly 2 zone changes; Z middle does nothing; Z up while running ignored | pass |
| `takeover` | entering set-up with knobs elsewhere + jitter: 0 changes; a 150-unit turn takes over; X flips: 0 changes | pass |
| `speech` | 1 summary on entering; a 1 s turn 5->12 = one request "12" ~0.33 s after stopping; entering a zone says its value | pass |
| `cvwhine` | a leak growing 5 octaves during a session (the owner's CV-pitch whine): exact silence between chimes | pass |
| `leds`, `glue` | legend snapshots; the real glue class against the mock (audio exact, one start pulse) | pass |
| `fuzz [minutes]` | random abuse + scripted sessions; invariants (settings only change when set-up active, pulses 100 ms, audio exact) | 240 simulated min under sanitizer: pass (75 sessions, 0 errors) |
| `longest` (in `all`) | 30:45 x 20 run in full: end pulse at sample +1,771,680,000 exactly | pass (~28 s, fast build only) |
| `fxdry` | both mixes at 0: output == input exactly | pass (0 samples differ) |
| `fxreverb` | noise-burst RT60 per X (target / all / low band: 1.0/0.7/0.9 ... 25/20.8/21.8 s); darkness changes tone not time; exact silence | pass |
| `fxdelay` | echo at 60.0 / 245.0 / 999.4 ms; at 93% feedback silent after 3-52 s | pass |
| `fxstable` | full-scale tones 55 Hz-7 kHz, noise, DC, a DC step, all at maximum: no reverb clamps, silence after, sub-audio drift ~1/2000 | pass |
| `fxchannels` / `fxpages` / `fxclicks` | no leakage; soft pickup, dead zones, set-up never touches effects; no clicks while sweeping all knobs | pass |
| `fxlevels` | reverb level within 0.8 dB across decay times; default mix ~ -5 dB vs a dry bell | pass (report) |
| optimisation check | branch-free rewrite: 4 min of output hashed before/after | bit-identical |
| sanitizer | quick suite + 210 simulated minutes of fuzz with effects | 0 errors |
| firmware | 192 MHz default + 144 MHz fallback | 0 warnings, RAM 186 KB (73%) |
| CPU | `bench_cycles.py`, 9 scenarios, 554,000 samples | worst 73% / mean 61% at 192 MHz |

## 7. Known unknowns and risks
* **Never run on hardware.** Knob noise vs hysteresis (48) and take-over (40); whether the switch's Up reading is clean; LED levels in a dark room.
* **Take-over at an end stop:** if a knob already sits fully CCW/CW at the value you want next, it must be nudged away and back (inherent to take-over; documented).
* **Effects never heard on hardware**; the demo uses synthesised stand-ins for the owner's chime and bell.
* **192 MHz at 1.15 V** is proven by Equanimity, not yet by this card. CPU figures are emulated (no RAM contention with DMA modelled).
* **RAM:** 70 KB free for stage 3. Speech must stay in flash.
* DAC zero-crossing glitch (directive errata) not addressed: listen for crackle on very quiet tails.
* **Speech data in flash** (stage 3): still unverified (`copy_to_ram` copies initialised data to RAM; need `__in_flash` or similar).
* Words "paused"/"stopped" are in the planned script but not yet used (speaking during meditation may disturb; owner to decide).

## 8. Next
**Stage 4 (release) is drafted** (20 Oct): `README.md` (patching, quick start, set-up with the LED number table, a session, preview,
effects, silence, LEDs, jacks, troubleshooting, building, credits), `info.yaml` (core fields + structured panel / switch / knobs /
LEDs, validated with the repository's `npm run validate-info`: no errors, 1 warning: `contact`), `LICENSE` (MIT), `lib/ComputerCard.LICENSE`,
`UF2/qigong_timer.0.9.0.uf2`, `samples/` (3 demos from the current code), `docs/SUBMITTING.md` (pull request steps).
Creator: **Joe Hedinger (isbn)**; folder **`389_QiGong_Timer`** (owner's choice; free on 20 Oct). Still needed: optional contact,
the final hardware test of this build (`docs/STAGE3_HARDWARE_TEST.md`), then Version 1.0.0 / Status Released / draft false, and the pull request.

## 9. Ideas backlog (proposals only)
Speak "paused"/"stopped"; a distinct start signal; saved settings (flash, risky); optional freeze; trimming CPU further so 144 MHz is comfortable.

## 10. Environment notes
No apt (archive.ubuntu.com unreachable): CMake 3.31 via `pip install "cmake<4"`, xPack ARM GCC 14.2.1-1.1 from GitHub, Pico SDK 2.1.1 + tinyusb from GitHub;
the SDK fetches and builds picotool itself. ~300 s per tool call: run cmake and make in separate calls the first time.

## 11. Glossary
**Settling period**: the silent 10 s after Start. **Take-over**: a set-up knob does nothing until moved a little, then its position is the value.
**Zone**: X's thirds (minutes / nothing / intervals). **Notch**: one value's slice of a knob's travel. **Slice**: one of 32 control-rate job slots.
