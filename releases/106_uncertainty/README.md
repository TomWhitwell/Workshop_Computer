# Uncertainty for Workshop System Computer

A tribute to the Buchla 266 Source of Uncertainty, sharing the card with a
Buchla-lineage triangle wavefolder and a fixed-window comparator, for the
Music Thing Modular Workshop Computer.

**Status: not yet flashed to hardware.** This is source-verified — it
builds cleanly against the Pico SDK and `ComputerCard.h` — but nobody has
patched it into a live rack yet. See "What still needs checking" below
before trusting the numbers.

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
│   ├── wavefolder.h        # triangle-reflection wavefolder
│   └── comparator.h        # hysteresis window comparator
├── info.yaml
└── UF2/uncertainty.uf2    # built firmware
```

FRV (Fluctuating Random Voltage) doesn't have its own header — it's a
member function of the card class in `main.cpp`, run on the RP2040's
second core, following the same pattern as the ComputerCard `second_core`
example (a slow, `float`-based control loop feeding a value that
`ProcessSample()` just outputs each 48kHz sample).

## Panel

```
                    ┌─────────────────────────┐
                    │   ●  MAIN (unused)      │
                    │                          │
              ○ 0   │   ○ X        ○ Y         │  ○ 1
   (noise: flat)    │  (FRV rate)  (QRV range)  │  (FRV level)
              ○ 2   │                          │  ○ 3
 (noise: low-biased)│   [ (ON)-OFF-ON  Z ]     │  (QRV level)
              ○ 4   │      tap = cycle noise   │  ○ 5
(noise: high-biased)│                          │  (comparator hit)
                    │                          │
                    │ AudioIn1  AudioOut1      │  fold/comparator in, fold out
                    │ (unused)  AudioOut2      │  noise out
                    │ CVIn1     (unused)       │  fold intensity
                    │ CVIn2     CVOut1          │  FRV rate mod, FRV out
                    │ PulseIn1  CVOut2          │  QRV trigger, QRV out
                    │ (unused)  PulseOut1      │  comparator trigger out
                    └─────────────────────────┘
```

| Section | Input(s) | Control(s) | Output | LED |
|---|---|---|---|---|
| Noise source | — | Z tap cycles flat → low-biased (pink) → high-biased (blue) | Audio Out 2 | 0/2/4, one lit = active mode |
| FRV | CV In 2 (rate mod, ±6V) | X (rate, 0.05–50Hz, exponential) | CV Out 1 | 1, brightness 0V off → +6V brightest |
| QRV | Pulse In 1 (new-value trigger) | Y (range, 0 to +6V) | CV Out 2 | 3, brightness 0V off → +6V brightest |
| Wavefolder | Audio In 1, CV In 1 (fold intensity) | — | Audio Out 1 | — |
| Comparator | Audio In 1 (shared with wavefolder in) | — (fixed ±1V window, 0V-centred) | Pulse Out 1 | 5, brief flash on trigger |

Main knob is currently unused — reserved rather than wired to anything, so
a future revision can add it without moving existing controls.

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
- **Wavefolder:** triangle-wave reflection. Multiple reflections off
  ±threshold mirrors are algebraically a triangle wave of period
  `4×threshold`, so one integer modulo produces the fully-folded shape in
  constant time — no lookup table, no iteration. CV In 1 shrinks the
  threshold from full-scale (no folding) down to a floor of 96 codes
  (heavy folding).
- **Comparator:** fixed ±1V (0V-centred) window with a hysteresis deadband
  (~60mV) around each edge, so noise sitting on the threshold doesn't
  chatter. Fires a ~1ms pulse each time the signal *leaves* the window, in
  either direction.

## What still needs checking

This was built and reviewed against `ComputerCard.h` and the AI directive,
and compiles clean (`-Wall -Wextra -Wdouble-promotion -Wfloat-conversion`,
zero warnings) — but none of the following has been confirmed on a real
card:

- Actual pink/blue noise slope by ear or spectrum analyser — fixed-point
  filter approximations vary in how precisely they hit ±3dB/octave.
- `ProcessSample()` timing margin, via the debug-GPIO/scope method — the
  audio-rate path (noise + wavefolder + comparator + QRV logic) is small
  and branch-light, but hasn't been measured on hardware.
- FRV's glide feel and QRV's range-scaling by ear — the constants
  (glide time = interval/3, fold floor = 96 codes) are reasoned defaults,
  not tuned against a listening pass.
- The wavefolder/comparator input-sharing on Audio In 1 sounds right in
  theory but hasn't been patched and played.

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
