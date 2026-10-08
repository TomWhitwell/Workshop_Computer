# You spin me round for Workshop Computer

**Version 0.1.0-rc1. Hardware-tested release candidate.**

You spin me round is a mono-to-stereo Leslie-style rotary speaker effect by
Adrian Vos. Separate bass-drum and treble-horn motion creates a slow roll or a
fast shimmer, with Doppler pitch movement, changing loudness and tone, cabinet
drive, and different acceleration and braking times for each rotor.

Choose the familiar **Outside** perspective or an exaggerated **Inside the
cabinet** perspective with opposing stereo motion and deeper Doppler movement.
The controls stay immediate: three knobs, slow/fast, and a hold-to-brake gesture.

Card **122** is a nod to the [Leslie 122 speaker](https://hammondorganco.com/products/leslie-speakers/122-147-981-147a).
The release folder is `122_You_Spin_Me_Round`. This is a creative rotary
model, rather than a component-by-component recreation of that specific cabinet.

## Install

Flash [You_Spin_Me_Round_0.1.0-rc1.uf2](uf2/You_Spin_Me_Round_0.1.0-rc1.uf2)
to a Workshop Computer program card using BOOTSEL, then reset with the switch
in Middle. Patch a mono source into Audio In 1 and listen to Audio Outs 1/2 as
left/right. Start with Main and X at noon and Y low.

RC1 uses the exact firmware bytes of the alpha3 build that passed the author's
hardware tests and listening checks. This folder contains the release candidate,
source, documentation and previews for the Workshop Computer catalogue.

## Controls

| Control | Function |
| --- | --- |
| Main | Rotation speed within the selected slow or fast range |
| X | Rotary movement and stereo spread; a curved response brings movement into the lower knob range |
| Y | Cabinet drive into soft saturation |
| Z Up | Fast range, unless Pulse In 1 is patched |
| Z Middle | Slow range, unless Pulse In 1 is patched |
| Z Down | Hold to brake; release to resume |

X fully left removes movement and gives matching digital left/right outputs;
the stationary cabinet signal path remains, so this is not a dry bypass.
Y increases compression and grit rather than simply multiplying output volume.

The horn accelerates and slows faster than the drum. When braking, audio
continues through the stationary cabinet. Releasing Down returns the switch to
Middle and therefore the slow range, unless Pulse In 1 selects fast. Either a
held Down switch or a high brake gate keeps the brake active.

Approximate steady rotation ranges:

| Range | Treble horn | Bass drum |
| --- | --- | --- |
| Slow | 0.4–1.4 revolutions/second | 0.3–1.1 revolutions/second |
| Fast | 4.5–8.5 revolutions/second | 3.8–7.2 revolutions/second |

## Listening perspective

Normal startup always selects **Outside**. To choose a perspective:

1. Hold Z Down while starting or resetting the Computer, and keep holding.
2. Turn Main left for Outside or right for Inside. The left or right LED column
   shows the choice; a small centre deadband prevents flicker.
3. Release Z to confirm. Main returns to its speed function and audio fades in.

The choice lasts until reset and is not saved to flash. There is no web editor.
Inside has wider opposing ear viewpoints, a deeper Doppler sweep, different
horn tone/path and stronger short cabinet reflections. It is a creative
close-up perspective, not a physically exact inside-cabinet simulation.

## Inputs

| Input | Function |
| --- | --- |
| Audio In 1 | Mono source |
| Audio In 2 | Unused |
| CV In 1 | Bipolar speed modulation added to Main |
| CV In 2 | Bipolar intensity modulation added to X |
| Pulse In 1 | When patched, high selects fast and low selects slow, overriding Z Up/Middle |
| Pulse In 2 | High holds brake; low releases it unless Z Down is still held |

Both pulse inputs use held gate levels, not trigger toggles or tap tempo. Levels
must last at least 2 ms to be reliably sampled. CV values are doubled in ADC
units and clamped to the knob range: approximately ±3 V reaches the endpoints
from a centred knob, subject to the Computer's input scaling. Removing a patched
speed gate restores switch control. Audio and CV jack detection use the
ComputerCard normalisation probe.

## Outputs

| Output | Function |
| --- | --- |
| Audio Out 1 | Processed left output |
| Audio Out 2 | Processed right output |
| CV Out 1 / CV Out 2 | Unused; nominal zero |
| Pulse Out 1 / Pulse Out 2 | Unused; low |

## LEDs

| LED | Meaning |
| --- | --- |
| LED0, top left | Horn rotation, following actual phase and inertia |
| LED1, top right | Drum rotation, following actual phase and inertia |
| LED2, middle left | Fast range selected, including gate override |
| LED3, middle right | Brake commanded by either held source |
| LED4, bottom left | Inside perspective; off for Outside |
| LED5, bottom right | Latched callback timing warning; reset to clear |

Motion LEDs slow and freeze at their rotor's final phase when braking. LED5
lights if this card's callback takes at least 18 microseconds. It measures the
callback rather than the full ComputerCard interrupt; it is not a complete-ISR
timing certification.

## Test

The author reports all alpha3 hardware tests pass and the sound works nicely.
RC1 is byte-identical to that tested firmware. The repeatable test protocol is
in [docs/HARDWARE_CHECK.md](docs/HARDWARE_CHECK.md), and the validation record is
in [docs/BUILD_STATUS.md](docs/BUILD_STATUS.md). A measured complete-ISR maximum
has not been recorded.

The [Outside](previews/outside.wav), [Inside](previews/inside.wav) and
[dry source](previews/dry-organ.wav) previews are synthetic host renders through
the same DSP, not recordings from the hardware. Slow starts at 0 s, fast is
selected at 4 s, and brake at 12 s. All use the same source and gain.

## Build

Set `PICO_SDK_PATH` to an installed Raspberry Pi Pico SDK checkout, then run with
CMake and the ARM embedded toolchain installed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

This produces `build/you_spin_me_round.uf2`. Vendored ComputerCard **0.4.0**
handles hardware at a 48 kHz audio rate. The CPU runs at 144 MHz, the program
runs from RAM, oscillator startup multiplier is 64, and USB/UART stdio are off.
Audio is processed per sample; motor targets and UI update at 1 kHz and geometry
targets every 32 samples. USB has no MIDI or editor function.

Run `sh tests/run.sh` for host sanitizer tests, and
`python3 tools/render_previews.py` to regenerate previews. These need a host
Clang C++ compiler; preview generation also needs Python 3. The original
phase-accumulator lookup-table approach follows ComputerCard's sine_wave_lookup
example. [docs/PROCESSING.md](docs/PROCESSING.md) records the processing choice.

## Credits

- You spin me round code, documentation and synthetic preview audio by Adrian Vos
  (soveda), 2026.
- ComputerCard 0.4.0 by Chris Johnson, MIT. Its unmodified header and license are
  preserved in `vendor/ComputerCard/`.
- Music Thing Modular Workshop Computer by Tom Whitwell / Music Thing Modular.
- Pico SDK and its import helper by Raspberry Pi; applicable notices are preserved
  in `vendor/PicoSDK/LICENSE.TXT`.
- Rotary-speaker inspiration: Leslie speakers. The name and card number identify
  the inspiration, without claiming affiliation or endorsement.

## License

Original code and documentation are released under the [MIT License](LICENSE).
Dependencies retain their own licenses and notices. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for source references and credits.
