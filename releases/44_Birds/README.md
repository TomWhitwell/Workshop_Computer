# Birds

Card 44. Version 0.5.0.

Birds is a birdsong card and sequencer. It is not super accurate but sounds fairly bird-ish, with clock input and two channel CV/pulse sequencer. 

[![Birds demo video](https://img.youtube.com/vi/LJqv-TFYq6Q/hqdefault.jpg)](https://youtu.be/LJqv-TFYq6Q)

Birds was inspired by Hunter Adams' [Synthesising Birdsong with the RP2040 ](https://vanhunteradams.com/Pico/Birds/Birdsong.html) - Hunter was a big help during the early stages of designing the Workshop System so I'm very grateful for that. 

The control sequencer is based on my Turing Machine random looping sequencer. The Turing Machine shuffles bits, and those bits choose families of calls, rests, short chirps, sweeps, trills and coos. The two outputs are two birds sharing the same small patch of memory. One calls, the other may answer, copy, invert, overlap or ignore it.

Birds was entirely written and compiled by Codex. I haven't looked at one line of the code, but advised it how to fix several basic bugs around floating point (don't use that) and slow routines in the 48k sample loop (don't to that) and loading the program into ram (do that). 

## Controls

Main knob: lock/change. Centre is unstable. Clockwise locks the pattern. Anticlockwise locks an inverted double pass. Sequences seem to be a fairly arbitrary length. 

X knob + CV 1: bird voice oscillator pitch.

Y knob + CV 2: sequence playback time. Left is slow. Right is fast. 

Switch middle: normal two-bird mode.

Switch up: wild mode - more likely to sound like R2D2 

Switch down: reseed. Randomises the sequence. 

Pulse input 1: external clock. Leave it empty for the internal clock.

Audio outputs 1 and 2: bird one and bird two.

CV outputs 1 and 2: pitch traces for bird one and bird two. These are quite low, so crank up the main knob on your oscillator. 

Pulse outputs 1 and 2: onset pulses. Trills and runs generate extra pulses inside the phrase.

LEDs 0/2 and 1/3: pitch height for each bird. LEDs 4/5 show pulse output activity.

Audio inputs are unused in version 0.5.0.

## Patches

Untested suggestions from the coding robot: 

- Patch Pulse Out 1 into Pulse In 1. The first bird clocks the whole card, so the conversation starts to trip over itself. Take Audio Out 2 as the main voice and use CV Out 1 to open a filter.
- Clock Pulse In 1 from a slow divider. Send CV Out 1 and CV Out 2 to two oscillators, not to pitch this time but to wavefolder amount or filter frequency. Keep the audio outputs in the mix, quietly, like the original birds behind the modular ones.
- Put Y under a slow triangle LFO. The same locked pattern becomes territorial at one end and half-asleep at the other.

## Building

This is a Pico SDK / ComputerCard C++ project.

```sh
cmake -S . -B build
cmake --build build
```

The UF2 appears at `build/bird_card.uf2`.

## Files

`main.cpp` is the card.

`ComputerCard.h` is Chris Johnson's Workshop Computer helper library.

`info.yaml` is the Workshop Computer release metadata.

`HANDOVER.md` records the design thinking for version 0.5.0.
