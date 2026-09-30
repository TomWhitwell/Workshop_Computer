# Gnarly C1ZZL3 8mu Version

Gnarly C1ZZL3 8mu Version is a dual-oscillator phase-distortion synth card for
the Music Thing Modular Workshop Computer. It is a separate firmware card from
the original Gnarly release, made for direct Music Thing 8mu envelope control.

Gnarly is the most complexVersion of C1ZZL3. It keeps the Web MIDI sound-preset
workflow from the advanced C1ZZL3 experiments, but removes the Turing machine
panel mode so the hardware controls can focus on oscillator editing, recipe wave
banks, ring modulation, and noise/grit.

For the user-facing card guide, see:

```text
CARD_README.md
```

## Status

This folder contains the separate Gnarly C1ZZL3 8mu Version build.

```text
release: 109 / Gnarly C1ZZL3 8mu v1
draft: false
```

Core C1ZZL3 remains card 84. Gnarly is prepared here as a separate card identity because
its hardware behaviour is substantially different.

## Stable Build

Built firmware UF2:

```text
uf2/Gnarly_C1ZZL3_8mu.uf2
```

Checksum:

```text
9852fda859086c7cfd88dcfc2a1446f664a8c6f2325b89655462f30c8c7ceec7
```

## What It Does

- Runs two phase-distortion oscillator lanes.
- Uses separate Amp1/Amp2, PD1/PD2, and Pitch1/Pitch2 envelopes.
- Supports named sound presets saved on the card.
- Saves either envelope-only data or full sound presets with performance
  settings.
- Uses four recipe wave banks for simple and CZ-like oscillator pairings.
- Reads and writes settings through the Gnarly Web MIDI Lab.
- Imports Casio CZ `.syx` patches through the shared C1ZZL3 Import Lab.
- Removes Turing CV, Turing pulse, and generated Turing MIDI behaviour.
- Maps the Music Thing 8mu's factory faders directly to all six envelope
  lanes, so their depth can be performed without the Web MIDI editor.

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

Each oscillator recipe slot chooses one entry from the current bank. In Bank 1
the slots are the eight plain C1ZZL3 wave families. In Banks 2 to 4 the slots
become fixed compound recipes:

- Bank 1:
  Slot 1 `Saw`
  Slot 2 `Square`
  Slot 3 `Narrow pulse`
  Slot 4 `Double sine`
  Slot 5 `Saw pulse`
  Slot 6 `Resonant saw window`
  Slot 7 `Resonant triangle window`
  Slot 8 `Resonant trapezoid window`
- Bank 2:
  Slot 1 `Double sine + Saw`
  Slot 2 `Double sine + Square`
  Slot 3 `Saw + Double sine`
  Slot 4 `Square + Double sine`
  Slot 5 `Narrow pulse + Double sine`
  Slot 6 `Saw pulse + Double sine`
  Slot 7 `Resonant triangle window + Double sine`
  Slot 8 `Resonant trapezoid window + Double sine`
- Bank 3:
  Slot 1 `Resonant saw window + Saw`
  Slot 2 `Resonant triangle window + Square`
  Slot 3 `Resonant trapezoid window + Narrow pulse`
  Slot 4 `Resonant saw window + Saw pulse`
  Slot 5 `Resonant triangle window + Saw pulse`
  Slot 6 `Resonant trapezoid window + Saw pulse`
  Slot 7 `Resonant saw window + Narrow pulse`
  Slot 8 `Resonant trapezoid window + Square`
- Bank 4:
  Slot 1 `Saw pulse + Double sine`
  Slot 2 `Resonant triangle window + Double sine`
  Slot 3 `Saw pulse + Narrow pulse`
  Slot 4 `Resonant trapezoid window + Double sine`
  Slot 5 `Resonant saw window + Saw pulse`
  Slot 6 `Narrow pulse + Resonant trapezoid window`
  Slot 7 `Square + Resonant saw window`
  Slot 8 `Resonant triangle window + Narrow pulse`

These are not equal 50/50 blends. Each recipe leans toward its first waveform,
with the second waveform mixed in at a fixed amount chosen in the firmware.
That is why some pairings feel like a coloured version of one family rather
than a completely even hybrid.

On the switch-down page, LEDs 1 and 2 show the selected recipe bank:

- Bank 1: LEDs 1 and 2 off.
- Bank 2: LED 1 on.
- Bank 3: LED 2 on.
- Bank 4: LEDs 1 and 2 on.

## MIDI

## Music Thing 8mu envelope controls

Plug a factory-configured Music Thing 8mu into the front USB-C jack. Its first
six faders set the live depth of the corresponding envelope lanes. A lane at
full depth is identical to the envelope created in the Web MIDI editor; at zero
depth it is neutralised. The controls are live only and never overwrite saved
envelope shapes or sound presets.

| 8mu fader | Factory CC | Live control |
| --- | --- | --- |
| 1 | CC34 | Amp1 envelope depth |
| 2 | CC35 | PD1 envelope depth |
| 3 | CC36 | Pitch1 envelope depth |
| 4 | CC37 | Pitch2 envelope depth |
| 5 | CC38 | PD2 envelope depth |
| 6 | CC39 | Amp2 envelope depth |
| 7 | CC40 | Envelope selection: Off, the eight factory envelopes, then saved sound-preset slots |
| 8 | CC41 | Master depth for all six envelope lanes |

The 8mu buttons continue to send MIDI notes: use them as note/gate triggers
for the selected envelope. An unmoved 8mu leaves every lane at full depth, so
the card begins exactly like the original Gnarly firmware.

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

The C1ZZL3 Envelope Lab has a hidden Developer-mode MIDI CC Test Suite for checking these messages without a hardware controller. It sends individual CC values, a neutral reset, and sweep tests through the selected Web MIDI output.

## Web MIDI Editor

Hosted editor path after Workshop deployment:

```text
https://computer.musicthing.co.uk/programs/109-gnarly-c1zzl3-8mu/web/index.html
```

Local editor from this release folder:

```sh
python3 -m http.server 5177 --directory web
```

Open:

```text
http://localhost:5177
```

Use Chrome or another browser with Web MIDI and SysEx support. When Web MIDI connects, the editor automatically checks card settings and saved envelope slots so the detected firmware type is shown without requiring a manual read first.

## C1ZZL3 Import Lab

Hosted import lab path after Workshop deployment:

```text
https://computer.musicthing.co.uk/programs/109-gnarly-c1zzl3-8mu/web/import/index.html
```

Local import lab from this release folder:

```sh
python3 -m http.server 5178 --directory web/import
```

Open:

```text
http://localhost:5178
```

Use this page to decode Casio CZ `.syx` patches into C1ZZL3 drafts with separate
oscillator envelopes, pitch lanes, oscillator wave choices, performance values,
and recipe-bank-compatible settings.

## Build

```sh
cmake -S . -B build -DPICO_NO_PICOTOOL=1
cmake --build build -j2
```

The build creates:

```text
build/Gnarly_C1ZZL3_8mu.uf2
```

## License Notes

This project is released under the MIT License. The included `computercard.h`
hardware helper is ComputerCard by Chris Johnson and is also MIT licensed; keep
its MIT notice present when copying firmware files into releases or experiments.

USB MIDI host support includes the MIT-licensed rppicomidi files, copyright
2023 rppicomidi. Their copyright and licence notices are retained in the
corresponding source files.
