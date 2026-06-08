# Vactrol — offline renderer (host simulation)

Listen to the Vactrol card **without a Workshop Computer**. This compiles the
real `../src/vactrol.cpp` against a mock hardware layer and renders test signals
to WAV files. Runs anywhere `g++` does — macOS, Linux, Raspberry Pi OS.

> Note: this is *not* the same as running on a Raspberry Pi Pico. The Computer's
> sound comes from an RP2040 **plus** an analog board (DAC/ADC/jacks); a bare
> Pico can boot the `.uf2` but has nothing to play it through. This renderer
> instead runs the DSP natively and writes audio you can play on any computer.

## Use

```sh
./build.sh
```

This builds `render` and writes three files:

| File | What it demonstrates |
| --- | --- |
| `out_selfping.wav` | Self-pinging percussion (no audio input), LPG mode, resonance swept up |
| `out_pluck.wav`    | LPG plucking a stereo oscillator arpeggio |
| `out_modes.wav`    | Same pinged osc through VCA (0–3 s) → LPG (3–6 s) → VCF (6–9 s) |

Play them with any audio app (`afplay out_pluck.wav` on macOS, `aplay` on Linux).

Each render also prints how hard the card drove its own output rail — `on-rail
N%` flags where the resonant peak would hard-clip on real hardware (back off the
input level or resonance if you don't want that).

## How it works

`computercard_mock.h` re-implements the `ComputerCard` API against plain
variables. It `#define`s `COMPUTERCARD_H` so the real (Pico-only) header
self-skips, and `COMPUTERCARD_HOST_SIM` so `vactrol.cpp` omits its hardware
`main()`. `render.cpp` then includes the actual card source, drives its
`ProcessSample()` 48 000×/second with generated knob/trigger/audio values, and
captures `AudioOut` to WAV.

## What this does and doesn't verify

- ✅ Exercises the **real** card DSP (same source the Pico build compiles).
- ✅ Lets you judge the *sound* and tune the constants by ear pre-hardware.
- ❌ Does not test hardware glue (calibration, jack detection, LED PWM, USB) or
  CPU timing on the actual Cortex-M0+. Real hardware is still the final check.
