# Bib for Workshop Computer

**Version 1.1.0-beta. Hardware-tested on one Workshop Computer.**

Bib is a stereo dub processor adapted from Plinky Synth's [Bib](https://github.com/plinkysynth/buddies_public/tree/main/sw/src/bib). It runs the original 64-frame Bib DSP directly on the Workshop's second RP2040 core, rather than using `ComputerCard.h`.

## Install

Flash [Bib-1.1.0-beta.uf2](uf2/Bib-1.1.0-beta.uf2) to a Workshop Computer program card, then reset with the switch in Middle.

## Controls

Main selects one of four pages. LEDs 0--1 show the selected page in binary: off/off Drive, on/off Delay, off/on Reverb, on/on Mix. LEDs 2 and 3 show the effective X and Y values, including CV. LEDs 4 and 5 show the relevant state for the current page.

| Page | Middle: X / Y | Switch Down |
| --- | --- | --- |
| Drive | Drive / bipolar delay send | Toggle wavefold |
| Delay | Delay time / feedback | Record a multitap phrase |
| Reverb | Reverb send / feedback | No action |
| Mix | Wet/dry mix / output level | Hold for dub feedback |

On Drive, Y is bipolar: centre is off, clockwise sends normal stereo delay, and counter-clockwise selects Bib's ping-pong behaviour.

### Multitap delay

On Delay, press Switch Down once to start a one-second phrase, then press it again up to eight times within that window. Every later press adds an equal-level relative delay head; the latest sets the full delay period. Turn X deliberately to clear the recorded pattern and restore a single repeat.

### Delay tape page

On Delay with Switch Up, X controls the tape rate from stopped (CCW), through normal speed at noon, to 2x (CW). Y adds slow wow/flutter around that rate. Return Switch Middle for normal delay time and feedback. These controls have their own pickup, so entering the page does not jump the audio.

### Reverb shimmer page

On Reverb with Switch Up, X controls a retained shimmer amount. It starts at a subtle level after reset and is limited to three-quarters depth. Return Switch Middle for ordinary reverb send and feedback; the chosen shimmer stays active.

## Inputs and outputs

- Audio In 1 and Audio In 2 are independent left and right inputs.
- CV In 1 and CV In 2 provide bipolar X and Y modulation after pot pickup.
- Pulse In 1 clock-quantises Delay to Bib-style 3/4, straight, and dotted divisions. After clock loss, the last division is held until X moves.
- Audio Outs 1 and 2 provide the processed stereo signal.

Audio In 1 is not normalised to Audio In 2. Mono duplication was evaluated but raised the idle noise floor in the direct stereo DSP path.

## Build

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -G "Unix Makefiles"
cmake --build build -j2
```

The build creates `build/bib_workshop.uf2`.

## Attribution and licence

The original Bib DSP, reverb, lookup tables, and delay behavior are adapted from the software portion of the [Buddies public repository](https://github.com/plinkysynth/buddies_public/tree/main/sw/src/bib), which is MIT-licensed. The upstream repository separately licenses its logos, panel and other graphic design under CC BY-SA 4.0, and its hardware under CERN-OHL-P v2; none of those non-software materials are included here. The Workshop transport borrows the hardware map, ADC correction, mux cadence, and DAC formatting from MIT-licensed [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard).

The Workshop-specific DMA transport, block scheduler, control mapping, safety limits, pickup, CV/clock support, and tape-rate hand-off were written for this port. No original panel artwork, logos, or hardware design is included. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LICENSE](LICENSE).

The previous `ComputerCard.h` release candidate is preserved in [`archive/1.0.0-computercard-rc1/`](archive/1.0.0-computercard-rc1/).
