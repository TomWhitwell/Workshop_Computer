# BuzzyBee

BuzzyBee is a hardware-tested Workshop Computer adaptation of the Buddies
Buzzrito stereo saw and sub swarm oscillator. It keeps the original seven-point
XY preset interpolation, comb character, glide, BOC, seeded wobble, pink noise,
note-memory chord behavior, and recordable XY movement while adapting the
instrument to Workshop Computer controls and I/O.

## Install

Flash `uf2/BuzzyBee-1.0.0.uf2` to a Workshop Computer card. Boot with the
switch Middle for normal performance.

## Controls

| Control | Function |
| --- | --- |
| Main | Overall tune. The original Buzzrito base pitch is near noon. |
| X / Y | Virtual XY-pad position. Y is inverted so clockwise follows the intended sound direction. |
| Audio/CV In 1 | Calibrated 1 V/oct pitch input, added to Main. |
| CV In 1 / 2 | X / Y virtual-pad CV. A patched axis overrides that saved-motion axis. |
| Pulse In 1 | Gate input. Leave unpatched for drone operation. |
| Pulse In 2 | Rising edge stops motion playback and restores live X/Y. |
| Switch Up | Latches XY recording after a normal performance boot. Return to Middle to play it. |
| Switch Middle | Plays the saved path, or gives live X/Y when no path is playing. |
| Switch Down tap | Cycles chord modes 1-4. |
| Switch Down hold | With Pulse In 1 patched, forces the gate open; otherwise mutes the drone. |

Switch Up records a new path. A closed gesture loops and an open gesture
ping-pongs. Leaving X/Y stationary records a parked point. Motion is stored in
RAM, lasts about 2.05 seconds at full length, and clears on power cycling.

Chord modes use original Buzzrito note memory, not fixed chord shapes. In
modes 2-4, hold successive pitches briefly using Main or pitch CV. BuzzyBee
retains the most recent 2, 3, or 4 stable notes and distributes its saws across
them. Mode 1 immediately returns to a single live pitch.

## LEDs And Outputs

LEDs 0-3 show the virtual pad's top-left, top-right, bottom-left, and
bottom-right weights. LED 4 follows gate level. LED 5 shows chord mode and is
full brightness while recording.

Audio Outs 1 and 2 are the stereo swarm. CV Out 1 is an approximate pitch
monitor and Pulse Out 1 follows the audio gate. CV Out 2 and Pulse Out 2 are
unused.

## WebUSB Editor

Boot or reset with the latched switch Up to enter the muted `Workshop Buzzrito
WebUSB` editor mode. The [Buzzrito manual and web editor](https://plinkysynth.com/docs/buzzrito-manual/)
can read and edit the seven presets. To save edited presets, move the switch Down and hold it for one
second; the card writes a CRC-checked record to flash and reboots. Power-cycle
with the switch Middle for normal performance.

The editor is separated from performance so USB traffic and flash writes cannot
disturb audio or motion playback.

## Build

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S releases/833_BuzzyBee -B /private/tmp/buzzybee -DCMAKE_BUILD_TYPE=Release
cmake --build /private/tmp/buzzybee -j2
```

## Attribution And Licence

BuzzyBee is released under the
[MIT License](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/833_BuzzyBee/LICENSE).

It adapts MIT-licensed software from the
[public Buddies Buzzrito source](https://github.com/plinkysynth/buddies_public/tree/main/sw/src/buzzrito),
including Buzzrito preset definitions and XY interpolation, pink-noise and
interpolation-noise algorithms, wavetable support, and the original swarm,
comb, wobble, and chord-note behavior. The Workshop per-sample renderer,
knob/CV mapping, motion controls, WebUSB boot separation, pitch-CV conversion,
and release integration were written for BuzzyBee.

No upstream logos, front-panel artwork, documentation graphics, or hardware
design are included. See the
[third-party notices](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/833_BuzzyBee/THIRD_PARTY_NOTICES.md)
for the full provenance record. `ComputerCard.h` is the MIT-licensed ComputerCard
framework by Chris Johnson, distributed by Music Thing Modular.
