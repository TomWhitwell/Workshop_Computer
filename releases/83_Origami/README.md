# 83_Origami

**Dual oversampled wavefolder** for the Music Thing Workshop System Computer.

A wavefolder is the other classic West-Coast timbre tool (the sibling of the
[low-pass gate](../81_West_Coast_LPG/)): instead of *removing* harmonics like a
filter, it *adds* them. Drive a waveform hard enough and it reflects back on
itself at the rails; each fold stacks on a new band of metallic, bell-like
harmonics that bloom and shift as you push the drive. Origami gives you **two
independent folders**, one per column of jacks, with three fold characters and a
bias control for even-harmonic colour.

The fold is computed at **4× oversampling** and band-limited back down, so the
(very harmonically rich) output aliases far less than a naïve folder would.

> Status: **0.1** — DSP verified in offline host simulation (`sim/`), built
> cleanly to a flashable `.uf2`, and tested on the Computer. Please
> open an issue/PR with fixes. The DSP is plain integer ComputerCard code
> (see `src/origami.cpp`).

## Quick start — it needs something to fold

A wavefolder is a *processor*: patch audio into **Audio In** and you'll get it
back, folded, from **Audio Out**. With the drive low and a quiet input it stays
nearly clean — the folding only bites once you push the signal past the rails
with the **drive knob** (X / Y) or with **CV In**. Turn the drive up and the
harmonics pile on.

- Best input is a **simple, loud waveform** — a sine or triangle from an
  oscillator. Folding a sine is the canonical sound; folding something already
  bright (a saw) gets dense fast.
- **Match the column.** Left jacks = channel 1, right jacks = channel 2; the two
  folders are completely independent. Audio, CV and output must be in the **same
  column** to interact.

## Panel / I/O

Left column of jacks = **channel 1**, right column = **channel 2**.

| Control | Function |
| --- | --- |
| **Audio In 1 / 2** | Channel input — the signal to fold. |
| **Audio Out 1 / 2** | Folded output. |
| **CV In 1 / 2** | Adds to that channel's fold drive — **CV over fold depth**. Bipolar: positive CV folds more, negative CV backs it off. |
| **CV Out 1 / 2** | Envelope follower of the folded output (0..+6 V) — handy for self-patching a VCA or gate elsewhere. |
| **Knob X** | Channel 1 fold drive (depth). Low ≈ clean, high ≈ many folds. |
| **Knob Y** | Channel 2 fold drive. |
| **Main knob** | **Bias / symmetry** for both channels. Centre = symmetric (odd harmonics); off-centre offsets the signal into the folder for an asymmetric, hollow/reedy colour (even harmonics). |
| **Switch** | Fold character: **Down = triangle** (classic reflecting fold), **Middle = sine** (smoother, Serge-ish), **Up = hard clip** (saturation/squarewave). |
| **LEDs** | Each column glows with that channel's output level. |

## Patch ideas

- **Classic timbre sweep** — patch an oscillator (sine/triangle) into Audio In,
  set the switch to **Down (triangle)**, and sweep **Knob X**. You'll hear the
  tone go from pure to bright and clangorous as folds pile on. Add **bias** (Main
  knob off centre) for a more vocal/reedy colour.
- **Dynamic / "opening" fold** — send an envelope or LFO to **CV In** so the fold
  depth tracks it. With a slow envelope the timbre brightens as the note evolves
  — the West-Coast "the harder you hit it, the brighter it gets" gesture. Tap the
  follower out of **CV Out** to drive a downstream VCA.
- **West-Coast voice across two cards** — fold an oscillator here, then send Audio
  Out to a [low-pass gate](../81_West_Coast_LPG/) on a second Computer for the 
  full *fold → gate* chain.
- **Stereo motion** — feed the two channels slightly detuned copies of the same
  oscillator and fold both; the independent folders smear into a wide, shifting
  texture.

## How it works (brief)

Each channel scales its input by a **drive** amount (knob X/Y plus CV In), adds a
**bias** DC offset (Main knob), and passes the result through a periodic folding
nonlinearity selected by the switch:

- **Triangle** — integer reflection: the value is wrapped/reflected into ±1.0, a
  triangle transfer function (the classic Serge/Buchla reflecting fold).
- **Sine** — a single-period sine lookup table; smoother folds, gentler at low
  drive.
- **Hard clip** — saturates at the rails (not really folding, but a useful
  squarewave/overdrive contrast).

Folders generate harmonics far above the audio band, which would alias badly at
48 kHz. So the nonlinearity runs at **4× oversampling**: the input is linearly
upsampled, folded per sub-sample, then decimated with a Hamming-windowed sinc
low-pass (`firTab`, built once in the constructor) before going to the DAC. All
hot-path math is integer/fixed-point (no FPU on the RP2040); only the one-time
table setup uses floats.

## Build

Requires the [RPi Pico SDK](https://github.com/raspberrypi/pico-sdk) with
`PICO_SDK_PATH` set (same as the other ComputerCard cards):

```sh
cd releases/83_Origami/src
mkdir -p build && cd build
cmake ..
make
```

This produces `origami.uf2`. Flash it to the Workshop System Computer in the
usual way: hold BOOTSEL, then copy the `.uf2` onto the mounted `RPI-RP2` drive.

> **macOS tip:** if the `RPI-RP2` drive ejects the instant you start copying and
> the firmware never lands, flash over USB instead:
>
> ```sh
> brew install picotool
> # board in BOOTSEL mode:
> picotool load -v -x origami.uf2
> ```

## Hear it without hardware

`sim/` compiles the **real** `src/origami.cpp` against a mock hardware layer and
renders test WAVs (drive sweep, the three fold characters, a bias sweep, and
CV-controlled folding) on any machine with `g++`:

```sh
cd releases/83_Origami/sim
./build.sh
```

See [`sim/README.md`](sim/README.md) for details and the caveats on what host
simulation does and doesn't verify.

Built with [Chris Johnson's ComputerCard library](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard).
