# C1ZZL3 Gnarly

C1ZZL3 Gnarly is a dual-oscillator phase-distortion synth card for the Music
Thing Modular Workshop Computer.

Gnarly is the most complex C1ZZL3 branch. It keeps the Web MIDI sound-preset
workflow from the advanced C1ZZL3 experiments, but removes the Turing machine
panel mode so the hardware controls can focus on oscillator editing, recipe wave
banks, ring modulation, and noise/grit.

For the user-facing card guide, see:

```text
CARD_README.md
```

## Status

This folder is prepared for a separate Workshop Computer PR as card 85.

```text
release: 85 / Gnarly protocol v11 candidate
draft: true
```

Core C1ZZL3 remains card 84. Rad can be added to card 84 in due course as an
additional version. Gnarly is prepared here as a separate card identity because
its hardware behaviour is substantially different.

## Stable Test Build

Current Gnarly UF2:

```text
uf2/C1ZZL3_GNARLY_V11.uf2
```

Checksum:

```text
0f79ef99d8485da4180e049c98085296cb36402e0dd11255db99e4f27fbe5fa5
```

## What It Does

- Runs two phase-distortion oscillator lanes.
- Uses separate Amp1/Amp2, PD1/PD2, and Pitch1/Pitch2 envelopes.
- Supports named sound presets saved on the card.
- Saves either envelope-only data or full sound presets with performance
  settings.
- Uses four recipe wave banks for simple and CZ-like oscillator pairings.
- Reads and writes settings through the Gnarly Web MIDI Lab.
- Imports Casio CZ `.syx` patches through the matching Gnarly Import Lab.
- Removes Turing CV, Turing pulse, and generated Turing MIDI behaviour.

## Hardware Controls

Switch middle: oscillator 1 page.

- Main: shared pitch
- X: oscillator 1 phase distortion
- Y: oscillator 1 recipe slot in the selected bank

Switch up: oscillator 2 page.

- Main: oscillator 2 base interval/spread, centred at unison
- X: oscillator 2 phase distortion
- Y: oscillator 2 recipe slot in the selected bank

Switch down hold: performance and bank page.

- Main: recipe bank
- X: ring modulation
- Y: noise/grit

## Recipe Banks

- Bank 1: simple single-wave families.
- Bank 2: warmer compound pairings with double-sine support.
- Bank 3: brighter resonant/windowed pairings.
- Bank 4: odd/import-faithful CZ-style pairings for translated patches.

On the switch-down page, LEDs 1 and 2 show the selected recipe bank:

- Bank 1: LEDs 1 and 2 off.
- Bank 2: LED 1 on.
- Bank 3: LED 2 on.
- Bank 4: LEDs 1 and 2 on.

## MIDI

Gnarly uses `CC20` to `CC27` as an eight-knob performance block. `CC1` is also
kept as oscillator 1 phase distortion so a mod wheel remains useful.

- `CC1`: oscillator 1 phase distortion, mod-wheel friendly.
- `CC20`: oscillator 1 recipe slot.
- `CC21`: oscillator 2 recipe slot.
- `CC22`: ring modulation amount.
- `CC23`: recipe bank.
- `CC24`: oscillator 2 interval/spread.
- `CC25`: oscillator 2 phase distortion.
- `CC26`: noise/grit amount.
- `CC27`: oscillator 1 phase distortion, for eight-knob controllers.

## Web MIDI Editor

Hosted editor path after Workshop deployment:

```text
https://tomwhitwell.github.io/Workshop_Computer/programs/85-gnarly-c1zzl3/web/index.html
```

Local editor from this release folder:

```sh
python3 -m http.server 5177 --directory web
```

Open:

```text
http://localhost:5177
```

Use Chrome or another browser with Web MIDI and SysEx support.

## Gnarly Import Lab

Hosted import lab path after Workshop deployment:

```text
https://tomwhitwell.github.io/Workshop_Computer/programs/85-gnarly-c1zzl3/web/import/index.html
```

Local import lab from this release folder:

```sh
python3 -m http.server 5178 --directory web/import
```

Open:

```text
http://localhost:5178
```

Use this page to decode Casio CZ `.syx` patches into Gnarly drafts with separate
oscillator envelopes, pitch lanes, oscillator wave choices, performance values,
and recipe-bank-compatible settings.

## Build

```sh
cmake -S . -B build -DPICO_NO_PICOTOOL=1
cmake --build build -j2
```

The build creates:

```text
build/C1ZZL3_CZ_RECIPE_WAVE_BANKS_V11.uf2
```

## License Notes

This project is released under the MIT License. The included `computercard.h`
hardware helper is ComputerCard by Chris Johnson and is also MIT licensed; keep
its MIT notice present when copying firmware files into releases or experiments.
