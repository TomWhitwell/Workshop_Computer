# Uncertainty for Workshop System Computer

A tribute to the Buchla 266 Source of Uncertainty, sharing the card with
an antialiased wavefolder and a Pulse In 1 alternator, for the Music
Thing Modular Workshop Computer.

**Status: confirmed working on hardware.**

## What's here

Self-contained; doesn't depend on anything outside this folder.

```
releases/106_uncertainty/
├── main.cpp              # wiring: panel I/O, switch/LED logic, core1 launch
├── CMakeLists.txt        # Pico SDK firmware build
├── ComputerCard.h         # Workshop Computer hardware abstraction
├── pico_sdk_import.cmake
├── dsp/
│   ├── noise.h              # flat / pink / blue noise (shared xorshift32 PRNG)
│   ├── qrv.h                 # Quantized Random Voltage (sample & hold)
│   ├── wavefolder.h          # antialiased fold, ported from Chris Johnson's Utility Pair
│   ├── soft_takeover.h       # pot-pickup, ported from this repo's 97_alloy release
│   └── dual_role_knob.h      # one knob, two roles (normal / attenuverter), pickup-guarded
├── info.yaml
└── UF2/uncertainty.uf2    # built firmware
```

FRV (Fluctuating Random Voltage) and the Pulse In 1 alternator don't have
their own headers — FRV is a member function of the card class in
`main.cpp`, run on the RP2040's second core, following the same pattern
as the ComputerCard `second_core` example (a slow, `float`-based control
loop feeding a value that `ProcessSample()` just outputs each 48kHz
sample). The pulse alternator is a dozen lines of edge-detection directly
in `ProcessSample()` — small enough that a separate header would be more
ceremony than the logic it holds.

## Panel

```
                    ┌─────────────────────────┐
                    │   ●  MAIN                │
                    │  (fold drive / CV1 att.) │
              ○ 0   │   ○ X        ○ Y         │  ○ 1
   (noise: flat)    │  (FRV rate/  (QRV range/  │  (FRV level)
              ○ 2   │   FRV att.)   QRV att.)   │
 (noise: low-biased)│   [ (ON)-OFF-ON  Z ]     │  ○ 3
              ○ 4   │  up = attenuverters       │  (QRV level)
(noise: high-biased)│  down-tap = cycle noise   │  ○ 5
                    │                          │  (alternator hit)
                    │ AudioIn1  AudioOut1      │  fold in, fold out
                    │ (unused)  AudioOut2      │  noise out
                    │ CVIn1     CVOut1          │  fold drive mod (bipolar), FRV out
                    │ CVIn2     CVOut2          │  FRV rate mod, QRV out
                    │ PulseIn1  PulseOut1      │  QRV trigger + alternator in, alternator out A
                    │ (unused)  PulseOut2      │  alternator out B
                    └─────────────────────────┘
```

| Section | Input(s) | Control(s) | Output | LED |
|---|---|---|---|---|
| Noise source | — | Z tap cycles flat → low-biased (pink) → high-biased (blue) | Audio Out 2 | 0/2/4, one lit = active mode |
| FRV | CV In 2 (rate mod, ±6V) | X: rate (0.05–50Hz, exponential); Z up = X is FRV's output attenuverter instead | CV Out 1 | 1, brightness = \|output\|, 0V off → ±6V brightest |
| QRV | Pulse In 1 (new-value trigger) | Y: range (0 to +6V); Z up = Y is QRV's output attenuverter instead | CV Out 2 | 3, brightness = \|output\|, 0V off → ±6V brightest |
| Wavefolder | Audio In 1, CV In 1 (drive mod, bipolar) | Main: fold drive; Z up = Main is CV In 1's attenuverter instead | Audio Out 1 | — |
| Pulse alternator | Pulse In 1 (shared with QRV trigger) | — | Pulse Out 1 / 2, alternating each pulse | 5, on for every pulse, both outputs |

CV In 1 adds directly onto Main's drive amount, unclamped — Chris
Johnson's original `mult = knob + CVIn` formula verbatim — except CV In 1
now passes through Main's attenuverter (see below) before it's added.
Because that attenuverter is bipolar and the sum is unclamped, enough
negative CV can still push the combined drive below zero: instead of
just cancelling drive out to silence, that inverts the signal before
folding, the way driving a real wavefolder's input through zero does.

### Z Up: attenuverters

Z Up gives Main/X/Y a second job each, as an attenuverter for the section
they already control — same knob, same section, different function. The
curve is a standard bipolar attenuverter: 12 o'clock (knob centred) mutes
that path entirely; turning CCW ramps towards -1 (inverted, growing
towards full negative at full CCW); turning CW ramps towards +1 (growing
towards the original full-positive value at full CW). For FRV and QRV
this makes their outputs genuinely bipolar — they're normally 0 to +6V
only, but with the attenuverter turned CCW of centre they swing negative
instead.

Switching Z between Up and Middle/Down never snaps a value to wherever
the knob physically happens to be sitting. Each of the two roles a knob
can play has its own held value; whichever role isn't currently active
stays frozen at its last setting, and only resumes tracking the knob once
the knob is physically moved back to (or through) that frozen value —
classic pot-pickup, implemented once in `dsp/dual_role_knob.h` (built on
`dsp/soft_takeover.h`) and reused for all three knobs. Concretely: turn
Main up while the wavefolder is nicely driven, dial in a CV In 1
attenuverter amount without disturbing that drive setting, flip back
down, and the drive knob picks up exactly where it left off rather than
jumping to wherever you left it while it was busy being an attenuverter.

## Hardware reality this build assumes

Voltage range is ~±6V bipolar on Audio/CV jacks (signed 12-bit, -2048 to
+2047), not 0–10V or ±5V. FRV and QRV's *unattenuverted* range is "0 to
+6V" because that's genuinely their range before the attenuverter is
applied — they're computed as unipolar signals and then optionally
inverted/scaled — output via `CVOut1Millivolts()` / `CVOut2Millivolts()`
with millivolt arguments clamped to [-6000, 6000].

The Z switch is (ON)-OFF-ON: Up and Middle hold, Down is
momentary/spring-loaded. Noise-mode cycling triggers once per Down-tap,
detected as `SwitchVal()==Down && SwitchChanged()` (entry into Down, not
level) — the switch always springs back afterward, and the LEDs (not the
switch position) hold the mode memory. Up, unlike Down, is a genuine held
position, which is what makes it usable as a "second function" state
rather than a tap gesture.

System clock is 144MHz — the directive's recommended default (96k
audio-input oversampling, reduced ADC tonal artifacts). There's no
192MHz/192kHz mode in ComputerCard.

## DSP notes

- **Flat noise:** raw xorshift32 output.
- **Low-biased (pink, -3dB/oct):** Paul Kellett's well-known three-stage
  one-pole cascade, in Q15 fixed point.
- **High-biased (blue, +3dB/oct):** `white - pink`. Pink noise is white
  noise with the highs rolled off, so subtracting it back out cancels the
  lows instead — a mirror-image tilt for one subtraction instead of a
  second filter.
- **FRV:** exponential-taper rate (0.05–50Hz over X's primary role + CV
  In 2, three decades), computed on core 1 as a genuine `float` control
  loop (real time via `time_us_32()`, not audio-sample counting). Each
  interval picks a new uniform-random target in [0, 6000]mV and glides
  toward it with a time constant of `interval/3`. X's dual-role/pot-
  pickup bookkeeping and its attenuverter application both live on core 1
  too, right next to the loop that owns X — core 0 never touches X's
  state, so there's nothing to synchronise across cores for this knob.
  The attenuverter is applied to the live glide value every iteration,
  not just at the moment a new target is picked, so turning it reshapes
  the current voltage immediately rather than waiting for the next hop.
- **QRV:** on each Pulse In 1 rising edge, one xorshift32 draw scaled into
  [0, Y-primary-scaled-range]mV, latched with no slew. Y's attenuverter
  is applied continuously to whatever value is currently held (in
  `ProcessSample`, every sample), so turning it reshapes the held voltage
  live rather than waiting for the next trigger.
- **Wavefolder:** a direct port of Chris Johnson's Utility Pair wavefolder
  (`github.com/chrisgjohnson/Utility-Pair`), which uses **antiderivative
  antialiasing (ADAA)**. Folding creates harmonics above what the input
  had; naively calling the fold function fresh on every sample pushes
  some of that content above the 24kHz Nyquist limit, where it aliases
  back down into the audible range as inharmonic noise. ADAA fixes this
  without changing the fold's shape at all: instead of calling `fold(x)`
  per sample, it evaluates the fold's antiderivative `F(x)` and returns
  the discrete slope `(F(x) - F(prev_x)) / (x - prev_x)` between this
  sample and the last. That slope is mathematically the average of the
  continuous-time fold output across the inter-sample interval, which
  suppresses exactly the above-Nyquist content a naive evaluation would
  have aliased. Full derivation in `ComputerCard/NOTES.md` under
  "Antiderivative antialiasing", after Parker et al., DAFx-16. The fold
  shape itself (`FoldFunction`) and its integral (`IntegralOfFold`) are
  Chris Johnson's exact formulas; only the surrounding state (member
  variables instead of function-local statics — his version relies on a
  per-channel class template this card doesn't have) is adapted. Drive
  into the fold is Main's primary role plus CV In 1 (passed through
  Main's attenuverter role first) — also his original
  `mult = knob + CVIn` formula, with the attenuverter as the only
  addition. Low drive passes the signal clean; higher drive pushes it
  past the fold's fixed threshold, the way turning up a real wavefolder's
  drive knob works.
- **Pulse alternator:** Pulse In 1 (already read for QRV's trigger) is
  duplicated straight through to Pulse Out 1 and Pulse Out 2, toggling
  which output gets each successive pulse on its rising edge — so a
  downstream clock divider or two separate voices can each get half the
  rate. This is a literal copy of Pulse In 1's own timing (not a
  fixed-width re-trigger): whatever gate width comes in is what goes out,
  alternating jacks. LED 5 lights for the duration of every pulse, on
  both outputs, so what you see always matches the true incoming rate
  even though each jack only carries half of it.
- **Attenuverters (Z Up):** `dsp/soft_takeover.h` provides the pot-pickup
  primitive — `Arm(target, current)` records a value to catch up to,
  `Allows(current)` gates live tracking until the knob gets close to or
  crosses that target. `dsp/dual_role_knob.h` builds on it: each knob
  holds two independent values (`primary_`, `secondary_`), a `bool` for
  which one is currently live, and a `SoftTakeover` per value. On every
  role switch, the value about to become live gets `Arm()`ed against the
  knob's current position; every sample, `Allows()` gates whether the
  live value tracks the knob or stays frozen. `Attenuvert(signal)`
  re-centres the secondary value around 2048 (12 o'clock) and scales
  `signal` by that offset over 2048, giving the -1..0..+1 curve described
  above in one integer multiply and shift.

## Build and flash

From a configured Pico SDK environment:

```sh
cd releases/106_uncertainty
cmake -S . -B build -G "Unix Makefiles"
cmake --build build
```

This produces `build/uncertainty.uf2` and `build/uncertainty.elf`. Flash
by holding BOOTSEL while plugging in USB-C, then copying the `.uf2` onto
the RP2 drive that appears (or via `picotool load`, or SWD).

## Licence

MIT, matching the ComputerCard framework itself.
