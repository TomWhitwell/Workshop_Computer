# Origami — offline renderer (host simulation)

Listen to the Origami wavefolder card **without a Workshop Computer**. This compiles
the real `../src/origami.cpp` against a mock hardware layer and renders test signals
to WAV files. Runs anywhere `g++` does — macOS, Linux, Raspberry Pi OS.

> Note: this is *not* the same as running on a Raspberry Pi Pico. The Computer's
> sound comes from an RP2040 **plus** an analog board (DAC/ADC/jacks); a bare
> Pico can boot the `.uf2` but has nothing to play it through. This renderer
> instead runs the DSP natively and writes audio you can play on any computer.

## Use

```sh
./build.sh
```

This builds `render` and writes four files:

| File | What it demonstrates |
| --- | --- |
| `out_drivesweep.wav` | A 110 Hz sine, triangle fold, drive swept from clean to dense |
| `out_modes.wav`      | Same sine through triangle (0–3 s) → sine (3–6 s) → hard clip (6–9 s) |
| `out_biassweep.wav`  | Sine fold with the Main (bias) knob swept — symmetric → asymmetric harmonics |
| `out_cvfold.wav`     | Fold depth opened/closed by a 1 Hz LFO on CV In (CV over fold) |

Play them with any audio app (`afplay out_modes.wav` on macOS, `aplay` on Linux).

Each render also prints how hard the card drove its own output rail — `on-rail
N%` flags where the folded peak would hard-clip on real hardware (back off the
drive or input level if you don't want that).

## How it works

`computercard_mock.h` re-implements the `ComputerCard` API against plain
variables. It `#define`s `COMPUTERCARD_H` so the real (Pico-only) header
self-skips, and `COMPUTERCARD_HOST_SIM` so `origami.cpp` omits its hardware
`main()`. `render.cpp` then includes the actual card source, drives its
`ProcessSample()` 48 000×/second with generated knob/CV/audio values, and
captures `AudioOut` to WAV.

## What this does and doesn't verify

- ✅ Exercises the **real** card DSP (same source the Pico build compiles),
  including the 4x oversampling / decimation path.
- ✅ Lets you judge the *sound* and tune the fold constants by ear pre-hardware.
- ❌ Does not test hardware glue (calibration, jack detection, LED PWM) or CPU
  timing on the actual Cortex-M0+ — the oversampled fold is the heaviest part of
  this card, so on-hardware timing is the real thing left to confirm.
