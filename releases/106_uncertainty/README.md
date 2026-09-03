# Uncertainty for Workshop System Computer

A tribute to the Buchla 266 Source of Uncertainty, sharing the card with
an antialiased wavefolder and a Pulse In 1 alternator, for the Music
Thing Modular Workshop Computer.

**Status: flashed and partly confirmed on hardware.** Noise, FRV, and QRV
are working. The wavefolder went through two from-scratch attempts that
both sounded wrong on hardware and has now been replaced with a direct
port of Chris Johnson's proven Utility Pair wavefolder. The card also had
a fixed-window comparator on Audio In 1 for a while — its window/
hysteresis crossing detection worked on hardware, but it's since been
removed outright (see "DSP notes") in favour of a simpler pulse
alternator built from the same toggle logic. None of the most recent
work (wavefolder rebuild, pulse alternator) has been re-tested on the
card yet — see "What still needs checking" below.

## What's here

Self-contained; doesn't depend on anything outside this folder.

```
releases/106_uncertainty/
├── main.cpp              # wiring: panel I/O, switch/LED logic, core1 launch
├── CMakeLists.txt        # Pico SDK firmware build
├── ComputerCard.h         # Workshop Computer hardware abstraction
├── pico_sdk_import.cmake
├── dsp/
│   ├── noise.h            # flat / pink / blue noise (shared xorshift32 PRNG)
│   ├── qrv.h               # Quantized Random Voltage (sample & hold)
│   └── wavefolder.h        # antialiased fold, ported from Chris Johnson's Utility Pair
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
                    │  (fold drive)            │
              ○ 0   │   ○ X        ○ Y         │  ○ 1
   (noise: flat)    │  (FRV rate)  (QRV range)  │  (FRV level)
              ○ 2   │                          │  ○ 3
 (noise: low-biased)│   [ (ON)-OFF-ON  Z ]     │  (QRV level)
              ○ 4   │      tap = cycle noise   │  ○ 5
(noise: high-biased)│                          │  (alternator hit)
                    │                          │
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
| FRV | CV In 2 (rate mod, ±6V) | X (rate, 0.05–50Hz, exponential) | CV Out 1 | 1, brightness 0V off → +6V brightest |
| QRV | Pulse In 1 (new-value trigger) | Y (range, 0 to +6V) | CV Out 2 | 3, brightness 0V off → +6V brightest |
| Wavefolder | Audio In 1, CV In 1 (drive mod, bipolar) | Main (fold drive) | Audio Out 1 | — |
| Pulse alternator | Pulse In 1 (shared with QRV trigger) | — | Pulse Out 1 / 2, alternating each pulse | 5, on for every pulse, both outputs |

CV In 1 adds directly onto the Main knob's drive amount, unclamped —
this is Chris Johnson's original `mult = knob + CVIn` formula verbatim.
Because it's bipolar and unclamped, enough negative CV can push the
combined drive below zero: instead of just cancelling Main out to
silence, that inverts the signal before folding, the way driving a real
wavefolder's input through zero does.

## Hardware reality this build assumes

Voltage range is ~±6V bipolar on Audio/CV jacks (signed 12-bit, -2048 to
+2047), not 0–10V or ±5V. FRV and QRV are described as "0 to +6V" because
that's genuinely their range — they're unipolar signals riding in the
positive half of a bipolar jack, output via `CVOut1Millivolts()` /
`CVOut2Millivolts()` with millivolt arguments clamped to [0, 6000].

The Z switch is (ON)-OFF-ON: Up and Middle hold, Down is
momentary/spring-loaded. Noise-mode cycling triggers once per Down-tap,
detected as `SwitchVal()==Down && SwitchChanged()` (entry into Down, not
level) — the switch always springs back afterward, and the LEDs (not the
switch position) hold the mode memory.

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
- **FRV:** exponential-taper rate (0.05–50Hz over X + CV In 2, three
  decades), computed on core 1 as a genuine `float` control loop (real
  time via `time_us_32()`, not audio-sample counting). Each interval picks
  a new uniform-random target in [0, 6000]mV and glides toward it with a
  time constant of `interval/3`, so the glide is essentially complete by
  the time the next target is chosen.
- **QRV:** on each Pulse In 1 rising edge, one xorshift32 draw scaled into
  [0, Y-scaled-range]mV, output directly — no slew, hard steps.
- **Wavefolder:** two from-scratch attempts here (a hard triangle-
  reflection fold, then a smooth sine-table fold) both sounded wrong on
  hardware, and both were the same underlying mistake: evaluating a
  nonlinear fold function fresh on every sample with no antialiasing.
  Folding creates harmonics above what the input had; a naive per-sample
  evaluation pushes some of that content above the 24kHz Nyquist limit,
  where it aliases back down into the audible range as inharmonic noise —
  regardless of whether the fold's corner is sharp or smooth. Replaced
  with a direct port of Chris Johnson's proven Utility Pair wavefolder
  (`github.com/chrisgjohnson/Utility-Pair`), which uses **antiderivative
  antialiasing (ADAA)**: instead of calling `fold(x)` per sample, it
  evaluates the fold's antiderivative `F(x)` and returns the discrete
  slope `(F(x) - F(prev_x)) / (x - prev_x)` between this sample and the
  last. That slope is mathematically the average of the continuous-time
  fold output across the inter-sample interval, which suppresses exactly
  the above-Nyquist content a naive evaluation would have aliased. Full
  derivation in `ComputerCard/NOTES.md` under "Antiderivative
  antialiasing", after Parker et al., DAFx-16. The fold shape itself
  (`FoldFunction`) and its integral (`IntegralOfFold`) are Chris
  Johnson's exact formulas, unchanged; only the surrounding state (member
  variables instead of function-local statics — his version relies on a
  per-channel class template we don't have here) is adapted for this
  card. Drive into the fold is Main + CV In 1, also his original
  `mult = knob + CVIn` formula verbatim — CV In 1 is bipolar and
  unclamped, so it can push the combined drive negative, inverting the
  signal before folding rather than just cancelling Main out to silence.
  Low drive passes the signal clean; higher drive pushes it past the
  fold's fixed threshold, the way turning up a real wavefolder's drive
  knob works.
- **Pulse alternator (replaces the comparator):** this card used to have
  a sixth block — a fixed ±1V window comparator on Audio In 1, with a
  minimum-retrigger lockout so a VCO driving it didn't fire it at audio
  rate, feeding an alternating Pulse Out 1/2 pair so a downstream clock
  divider or two separate voices could each get half the rate. The window/
  hysteresis crossing detection and the rate limiter both worked as
  designed and were verified numerically (a host-side test showed a
  220Hz tone's firing rate held exactly constant across a 1V-to-4.5V
  window sweep — window width sets sensitivity, not rate — while a
  minimum-retrigger lockout capped it to ~10Hz regardless of the VCO's
  pitch). Removed anyway: the whole comparator idea was dropped, kept
  only the alternator. Pulse In 1 (already read for QRV's trigger) is now
  duplicated straight through to Pulse Out 1 and Pulse Out 2, toggling
  which output gets each successive pulse on its rising edge — the exact
  same edge-detected toggle the comparator used to drive, just fed by a
  different source. Unlike the old comparator pulse (a fixed ~1ms
  re-trigger), this is a literal copy of Pulse In 1's own timing: whatever
  gate width comes in on Pulse In 1 is what goes out, alternating jacks.
  LED 5 lights for the duration of every pulse, on both outputs, so what
  you see always matches the true incoming rate even though each jack
  only carries half of it.

## What still needs checking

Noise, FRV, and QRV have been confirmed working on hardware. The
wavefolder rebuild and the pulse alternator have not — see "DSP notes"
above for what changed and why. Still open:

- **The rebuilt wavefolder needs a re-flash and listening pass.** The
  fold shape and antialiasing math are Chris Johnson's own proven,
  hardware-tested formulas, unchanged. Porting them into member-variable
  state (instead of his function-local statics, which relied on a
  per-channel class template this card doesn't use) introduced one real
  bug, caught before this reached hardware: the state's default
  initial value didn't satisfy the invariant the ADAA math depends on,
  so the very first sample after startup computed a slope against the
  wrong reference point and hard-clipped. Confirmed and fixed with a
  host-side numeric test (`dsp/wavefolder.h` has no hardware
  dependencies, so plain `g++` on the dev machine could run the exact
  fold math against a synthetic sine) — see the comment above
  `lastIntegral_` in `dsp/wavefolder.h`. CV In 1 was added after that fix,
  wired as bipolar per request, additive with Main and unclamped
  (Chris Johnson's original formula) — the numeric tests confirm it stays
  bounded and behaves smoothly across the full range including negative
  CV pushing the drive through zero into inversion, but that's still just
  the math. What none of that can tell you: does low Main pass the signal
  through clean; does turning Main up bring in folds progressively; does
  it sound cleaner than the two previous attempts on a sine input; does
  CV In 1's through-zero inversion sound like a feature or a mistake in
  practice.
- **The pulse alternator needs a re-flash and a scope/ear check.** The
  toggle logic itself is a straight carry-over from the (hardware-
  confirmed) comparator alternator, and was re-checked in isolation
  against a synthetic pulse sequence mimicking real pulse-width/gap
  timing before this shipped — LED-high sample count matched total pulse
  samples exactly, output alternated cleanly across successive firings.
  What that can't confirm: whether a literal passthrough of Pulse In 1's
  timing (rather than a fixed-width re-trigger) is actually what's wanted
  for whatever's patched into Pulse In 1 — very short or very long input
  gates will produce correspondingly short or long output gates on
  whichever jack is active, unlike the old comparator's fixed ~1ms pulse.
- Actual pink/blue noise slope by ear or spectrum analyser — fixed-point
  filter approximations vary in how precisely they hit ±3dB/octave.
- `ProcessSample()` timing margin, via the debug-GPIO/scope method — the
  audio-rate path (noise + wavefolder + pulse alternator + QRV logic) is
  small and branch-light, but hasn't been measured on hardware.
- FRV's glide feel and QRV's range-scaling by ear — the constants
  (glide time = interval/3) are reasoned defaults, not tuned against a
  listening pass.

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
