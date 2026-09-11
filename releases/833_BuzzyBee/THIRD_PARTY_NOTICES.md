# Third Party Notices

## Plinky Synth Buddies / Buzzrito

This card adapts MIT-licensed software from the
[public Plinky Synth Buddies Buzzrito source](https://github.com/plinkysynth/buddies_public/tree/main/sw/src/buzzrito),
with particular reference to `buzzrito.c` and
`sw/src/buzzrito/buzzrito_dsp.h`.

Copied or adapted material includes Buzzrito preset structures and default
presets, XY interpolation, pink-noise and interpolated-noise algorithms,
wavetables, and the original swarm, comb, wobble, and chord-note behavior.
The Workshop renderer and hardware integration are new adaptation code.

The upstream repository states that Buddies software is MIT licensed. Upstream
logos, front-panel artwork, documentation graphics, and hardware design have
separate terms and are not included in this card.

## Music Thing Modular ComputerCard

`ComputerCard.h` and `pico_sdk_import.cmake` are distributed from the
[Music Thing Modular Workshop Computer repository](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard).
ComputerCard is MIT licensed and retains its source attribution.

## Raspberry Pi Pico SDK And TinyUSB

The build uses the Raspberry Pi Pico SDK and its bundled TinyUSB dependency
from `PICO_SDK_PATH`. They are not copied into this card and remain under their
respective upstream licences.
