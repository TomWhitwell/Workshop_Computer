# CS80

CS80 is a draft Workshop Computer card for a CS-80-inspired mono synthesiser
voice. The name is a working title and should change before release.

The first target is intentionally modest: make one expressive mono voice sound
good, keep the audio loop lean, and leave the code shaped so a second voice can
be added later.

This card is not affiliated with Yamaha and is not intended to be a circuit
emulation of the Yamaha CS-80. The goal is a playable Workshop Computer
instrument inspired by the broad musical gestures of that synthesiser: two
oscillator lanes, animated pulse/saw tone, high-pass into low-pass filtering,
performance modulation, ring modulation, and expressive envelopes.

## Current Status

This folder contains a first-pass firmware scaffold.

- Firmware: first-pass mono/two-layer voice implemented
- Web editor: placeholder only
- UF2: not yet available
- Release status: draft

Release firmware should go in `uf2/` when a build is ready to publish. Local
hardware-test UF2s should go in `test-uf2/`, which is ignored by git.

## Design Target

- RP2040 clock: 192 MHz
- Firmware should use `pico_set_binary_type(... copy_to_ram)`
- Audio rendering should stay on core 0
- USB MIDI, Web MIDI SysEx, editor communication, and control aggregation should
  run on core 1
- The audio loop should avoid division, float-heavy code, blocking calls, flash
  writes, and MIDI parsing
- Parameters should cross from core 1 to core 0 through a small bounded queue or
  double-buffered snapshot
- The first voice should be mono, with structures laid out so a second voice can
  be added after profiling

## Planned Mono Voice

- two oscillator lanes
- saw, pulse, and mixed oscillator colour
- oscillator spread or detune
- high-pass then low-pass filter character
- amp envelope
- filter envelope
- LFO for vibrato, tremolo, or filter movement
- ring modulation as a performance colour
- gate and MIDI note triggering
- Web MIDI editor for patch editing, readback, and saving

## First-Pass Firmware

`CS80.cpp` is a lean starting point rather than a finished synth. It currently
drives one mono voice and copies it to both audio outputs for easy monitoring:

- `Audio Out 1`: mono voice
- `Audio Out 2`: mono copy / future alternate output reserve

Holding Down exposes performance controls for temporary pitch offset, ring
modulation, and LFO depth. The earlier two-output Voice A/B experiment has been
pulled back until the mono implementation is lean and profiled.
When `Pulse In 1` is unpatched the gate is held open for oscillator bring-up;
when it is patched, `Pulse In 1` gates the mono voice.
`Audio/CV In 1` is the pitch input. `CV In 1` is a wide filter modulation input,
and `CV In 2` is an expression input that pushes brightness and resonance.

Panel controls use soft pickup so switching pages does not immediately overwrite
stored values. LEDs 0/2/4 show the stored Main/X/Y values for the current switch
page, while LEDs 1/3/5 go bright once each physical knob has picked up its stored
value.

The first pass uses small lookup tables in `CS80_LUT.*` for pitch ratios, filter
curves, envelope rates, and LFO/ring sine values. This follows the C1ZZL3 habit
of keeping expensive shaping out of the 48 kHz audio path.

Build locally with:

```sh
cmake -S . -B build -DPICO_NO_PICOTOOL=1
cmake --build build -j2
```

## References

Cards 84 and 101 are the main local references for the Web MIDI/editor pattern,
192 MHz build setup, copy-to-RAM configuration, dual-core USB MIDI worker, flash
storage, and complex synth-card release structure:

- `releases/84_CosmikC1zzl3`
- `releases/101_Gnarly_C1ZZL3`

## Attribution And License Notes

This draft card scaffold and future CS80 card firmware are by Adrian Vos and
are released under the MIT License. See `LICENSE`.

The included `computercard.h` file is ComputerCard by Chris Johnson, version
0.3.0, also MIT licensed. It is copied from:

```text
Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h
```

Future firmware or Web MIDI code adapted from existing Workshop Computer cards
should keep the relevant notices in source files and be called out here before
release.

The included `pico_sdk_import.cmake` file is the standard Raspberry Pi Pico SDK
import helper. It carries the Raspberry Pi (Trading) Ltd. BSD-style notice in
the file itself.
