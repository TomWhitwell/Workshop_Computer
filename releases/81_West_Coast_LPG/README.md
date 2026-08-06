# 81_West_Coast_LPG

**Dual vactrol-emulating low-pass gate** for the Music Thing Workshop System Computer.

A low-pass gate (LPG) is the classic West-Coast building block: a combined VCA
and low-pass filter, controlled *together* by a vactrol (an LED bonded to a
photoresistor). The vactrol's slow, nonlinear light response gives the LPG its
signature **fast attack / slow exponential "plong"** - strike it with a trigger
and you get a naturally percussive, plucked decay; feed it CV and it behaves as
a smooth combined VCA/VCF.

This card provides **two independent LPGs**, one per column of jacks.

> Status: **0.1** The continuous
> CV hold and the VCA/VCF-only switch positions are only lightly tested - please
> open an issue/PR with fixes. The DSP is plain integer ComputerCard code
> (see `src/vactrol.cpp`).

## Quick start - the gate starts *closed*

A vactrol low-pass gate makes **no sound on its own**. Each channel's gate sits
shut until something opens it, so if you patch audio in and hear silence - or a
very faint, muffled signal - nothing is broken; you just haven't opened the gate
yet. Open it one of two ways:

- **Ping it** - send a trigger/clock to **Pulse In**. Each pulse strikes the
  vactrol with a fast attack and slow decay (the percussive "plong"). This is
  the classic use; set the switch to **Middle (LPG)**.
- **Hold it open** - send a *positive* CV to **CV In** to keep the gate open
  continuously, turning the channel into a plain VCA/filter. It takes a healthy
  voltage: roughly **+6 V opens it fully**; a couple of volts only cracks it open
  (and stays quiet).

Two things that catch people out:

- **Match the column.** Left jacks = channel 1, right jacks = channel 2, and the
  two channels are completely independent. Your audio, your ping, and your CV
  must all be in the **same column** to interact - audio in the left input is
  unaffected by a pulse in the right.
- **Switch = Up (VCF only) is the quietest "is it broken?" case.** That mode has
  no amplitude gate, but the filter cutoff sits at its ~30 Hz floor until the
  envelope opens it, so an un-pinged channel passes only a faint, muffled rumble.
  Use **Middle (LPG)** to hear the full effect.

> **10-second sanity check:** unplug the audio inputs and send a clock to Pulse
> In. With nothing patched to audio, the channel self-pings an internal noise
> burst, so you'll hear a plucky drum/marimba hit on every pulse - proof the card
> is alive and the gate is working.

## Panel / I/O

Left column of jacks = **channel 1**, right column = **channel 2**.

| Control | Function |
| --- | --- |
| **Audio In 1 / 2** | Channel input. **If nothing is plugged in, the channel self-pings** (see below). |
| **Audio Out 1 / 2** | Channel output (the gated/filtered signal). |
| **Pulse In 1 / 2** | Ping / strike the vactrol - gives the percussive plong envelope. |
| **CV In 1 / 2** | Continuous gate level - positive CV opens the gate (**~+6 V = fully open**; a few volts only partially opens it). Combined with the ping via `max()`, so triggers and a CV "hold" coexist. |
| **CV Out 1 / 2** | The vactrol "glow" envelope as a 0..+6 V CV - use it to modulate other modules. |
| **Knob X** | Channel 1 decay / response time (~10 ms … 3 s). |
| **Knob Y** | Channel 2 decay / response time. |
| **Main knob** | Resonance / "colour" of both filters (low = soft, high = pingy / near self-oscillating). |
| **Switch** | Per-card mode: **Down = VCA only**, **Middle = LPG (VCA + VCF)**, **Up = VCF only**. |
| **LEDs** | Each column glows with that channel's envelope (the vactrol's light). |

## Patch ideas

- **Plucked / bonk voice** - patch an oscillator into Audio In, a clock into
  Pulse In, set the switch to **Middle (LPG)**. Each clock pulse plucks the
  note. Knob X/Y sets how long it rings; Main knob adds resonant "ping".
- **Self-pinging percussion** - plug *nothing* into the audio inputs and send a
  clock to Pulse In. The card excites the channel with a short internal noise
  burst, so a resonant filter rings into a tom/marimba/zap. The switch mode
  changes the flavour: **VCA only** → noisy hat/thump, **LPG/VCF** → resonant
  pitched hit. Main knob = pitch/resonance, Knob X/Y = decay.
- **Envelope follower-ish / VCA duty** - send a CV (e.g. an LFO or another
  envelope) to CV In to use a channel as a plain dynamic VCA/filter, and tap the
  smoothed vactrol envelope back out of CV Out.

## How it works (brief)

Each channel models the vactrol as a one-pole follower with **asymmetric** time
constants: a fast (~3 ms) attack and a slow, knob-controlled exponential decay.
That conductance value (`env`) simultaneously drives the VCA gain and the
cutoff of a resonant state-variable low-pass filter (cutoff sweeps
exponentially ~30 Hz … 12 kHz with the envelope). Filter coefficients are
refreshed at a decimated control rate to keep the per-sample audio path cheap;
the SVF uses 64-bit internals so high resonance can't overflow.

## Build

Requires the [RPi Pico SDK](https://github.com/raspberrypi/pico-sdk) with
`PICO_SDK_PATH` set (same as the other ComputerCard cards):

```sh
cd releases/81_West_Coast_LPG/src
mkdir -p build && cd build
cmake ..
make
```

This produces `vactrol.uf2`. Flash it to the Workshop System Computer in the
usual way: hold BOOTSEL, then copy the `.uf2` onto the mounted `RPI-RP2` drive.

> **macOS tip:** if the `RPI-RP2` drive ejects the instant you start copying
> (in Finder *or* from the terminal with `cp`) and the firmware never lands,
> flash over USB instead - it bypasses the mass-storage drive entirely:
>
> ```sh
> brew install picotool
> # board in BOOTSEL mode:
> picotool load -v -x vactrol.uf2
> ```

Built with [Chris Johnson's ComputerCard library](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard).
