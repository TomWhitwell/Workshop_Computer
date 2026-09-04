# Uncertainty for Workshop System Computer

A tribute to the Buchla 266 Source of Uncertainty, sharing the card with
a wavefolder (in the spirit of the 259 Complex Oscillator's timbre
section) and a pulse alternator (in the spirit of the Model 140 Timing
Pulse Generator), for the Music Thing Modular Workshop Computer.

**Status: confirmed working on hardware.**

## What it does

Five things share this one card:

- **Noise source** — a source of hiss/static in three flavours: flat
  (even across the spectrum), low-biased (darker, bass-heavy), and
  high-biased (brighter, treble-heavy). Tap the switch down to cycle
  between them.
- **FRV (Fluctuating Random Voltage)** — a voltage that wanders
  continuously and unpredictably, sliding smoothly from one random value
  to the next rather than jumping. Speed is adjustable from a slow drift
  (one change every twenty seconds or so) up to a fast, buzzy wobble.
- **QRV (Quantized Random Voltage)** — a voltage that jumps to a fresh
  random value each time it receives a trigger, then holds perfectly
  still until the next one. The stepped, "sample and hold" counterpart to
  FRV's continuous drift.
- **Wavefolder** — takes an incoming audio signal and folds it back on
  itself once it gets loud enough, the way a real Buchla-style folder
  does. At low settings it's transparent; turn the drive up and the
  signal gets progressively more complex, bright, and eventually harsh.
- **Pulse alternator** — takes a pulse/clock/gate signal and echoes it
  out, alternating between two separate outputs each time a new pulse
  arrives. Useful for sharing one clock between two things, each getting
  half the rate.

## Panel layout

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
                    │ CVIn1     CVOut1          │  fold drive mod, FRV out
                    │ CVIn2     CVOut2          │  FRV rate mod, QRV out
                    │ PulseIn1  PulseOut1      │  QRV trigger + alternator in, alternator out A
                    │ (unused)  PulseOut2      │  alternator out B
                    └─────────────────────────┘
```

## Inputs and outputs

| Jack | What it does |
|---|---|
| Audio In 1 | The signal you want folded. |
| Audio Out 1 | The folded signal comes out here. |
| Audio Out 2 | The noise source's output. |
| CV In 1 | Modulates the wavefolder's drive amount, on top of the Main knob. This is bipolar — depending on the polarity of the incoming voltage, it can push the drive up or down (and even flip the signal upside-down, which just sounds like another way of folding it). |
| CV In 2 | Modulates FRV's speed, on top of the X knob. Also bipolar, so it can speed FRV up or slow it down. |
| CV Out 1 | FRV's output voltage. |
| CV Out 2 | QRV's output voltage. |
| Pulse In 1 | Two jobs at once: a pulse here makes QRV jump to a new random value, and that same pulse is echoed straight out through the pulse alternator below. |
| Pulse Out 1 / Pulse Out 2 | The alternator's two outputs. Every pulse that arrives at Pulse In 1 comes out one of these two jacks — alternating which one each time — so a steady stream of pulses in produces two interleaved, half-speed streams out. |

All the audio and CV jacks here work across roughly ±6V.

## Controls

**Main, X, and Y** each do two jobs, depending on the switch:

| Knob | Normal job | Job when the switch is flipped up |
|---|---|---|
| Main | Sets how hard the wavefolder is driven | Attenuverter for CV In 1 |
| X | Sets FRV's speed | Attenuverter for FRV's output |
| Y | Sets QRV's output range | Attenuverter for QRV's output |

**What "attenuverter" means here:** turn the knob to 12 o'clock and that
control has no effect at all — it's silent/off. Turn it left (counter-
clockwise) and the signal it's attenuverting comes out flipped upside
down, growing stronger the further left you go. Turn it right (clockwise)
and it comes out the normal way round, growing stronger the further
right you go. It's a way of dialling in "how much, and which direction"
with one knob. Because FRV and QRV are normally positive-only voltages,
this is also what lets their attenuverters push them negative — turn one
CCW of centre and that output starts swinging below 0V instead of only
above it.

**No jumps when you switch modes.** Since each of Main/X/Y is doing two
jobs, you'd normally expect a value to jump the moment you flip the
switch and the knob's physical position suddenly means something
different. That doesn't happen here — each knob remembers where it was
left in *both* of its jobs. Flip to attenuverter mode, dial in an amount,
flip back, and the knob's normal job picks up exactly where you left it;
it won't start changing again until you turn the knob back to roughly
where it was. Same going the other way. So you can freely flip back and
forth to tweak an attenuverter setting without ever disturbing whatever
the knob was doing a moment ago.

**The Z switch:**
- **Down (tap):** a quick press-and-release cycles the noise source
  through its three colours. It springs back up on its own.
- **Middle:** normal operation. Main/X/Y do their everyday jobs.
- **Up:** attenuverter mode. Main/X/Y switch to the jobs described above.

**LEDs**, top-to-bottom, left-to-right:
1. Lit when the noise source is set to flat.
2. Brightness follows FRV's current output — off at 0V, brightest the
   further it swings from there in either direction.
3. Lit when the noise source is set to low-biased.
4. Brightness follows QRV's current output the same way LED 2 follows
   FRV's.
5. Lit when the noise source is set to high-biased.
6. Lights up for the duration of every pulse arriving at Pulse In 1,
   regardless of which of the two alternator outputs it's currently
   being sent to.

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

## Credits

- **[Chris Johnson](https://github.com/chrisgjohnson)** — wrote
  ComputerCard, the hardware library this whole card is built on (part of
  the [Workshop Computer](https://github.com/TomWhitwell/Workshop_Computer)
  repo this card lives in), and
  [Utility Pair](https://github.com/chrisgjohnson/Utility-Pair), whose
  wavefolder this card's wavefolder is a direct port of, antialiasing
  technique and all.
- **[Eric Gao](https://github.com/Ericxgao)** — the pot-pickup
  ("nothing jumps when you switch modes") logic is ported from his
  [Alloy](../97_alloy) card, also in this repo.
- **Parker et al., DAFx-16** — "Reducing the aliasing of nonlinear
  waveshaping using continuous-time convolution," the paper behind the
  antialiasing technique the wavefolder uses.
- **[Tom Whitwell](https://github.com/TomWhitwell) / Music Thing
  Modular** — the Workshop Computer platform this card runs on.
- **Don Buchla** — the 266 Source of Uncertainty, the 259 Complex
  Oscillator, and the Model 140 Timing Pulse Generator, whose ideas this
  card is a tribute to rather than a copy of.

## Licence

MIT, matching the ComputerCard framework itself.
