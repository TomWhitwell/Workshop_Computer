# ScaryLingo

Card 202 for the Music Thing Modular Workshop Computer.

ScaryLingo is a hardware-tested ring modulator with an internal carrier, an external-carrier input, a voltage-controlled LFO, and a dry/wet crossfade. It is designed for voice processing, metallic sidebands, slow tremolo, and patchable modulation.

## Flashing

Copy [`UF2/ScaryLingo.uf2`](UF2/ScaryLingo.uf2) to a Workshop Computer Program Card in BOOTSEL mode, then insert the card and reset the Computer.

## Patch

| Jack | Function |
| --- | --- |
| Audio In 1 | Programme audio to ring-modulate. |
| Audio In 2 | External carrier. Patching it replaces the internal carrier. |
| CV In 1 | Bipolar carrier-frequency offset. |
| CV In 2 | Bipolar dry/wet mix offset. |
| Pulse In 1 | Resets the LFO phase on each rising edge. |
| Pulse In 2 | While high, applies LFO-shaped tremolo to the wet mix. |
| Audio Out 1 | Dry/wet main output. |
| Audio Out 2 | Ring-only output for parallel patching. |
| CV Out 1 | Internal LFO waveform. |
| CV Out 2 | Half-level carrier monitor. |
| Pulse Out 1 | Positive half-cycle of the LFO. |
| Pulse Out 2 | High when the programme input is active. |

Unpatched audio inputs are muted. With no programme input, both audio outputs are silent.

## Controls

All knob pages use soft pickup: when you enter Middle or Up, a knob begins changing its stored value only after its physical position crosses that value.

### Middle: Performance

| Control | Function |
| --- | --- |
| Main | Internal carrier frequency. The lower half spans 0.6-80 Hz; the upper half spans 80 Hz-4 kHz. |
| X | Dry/ring mix. Fully CCW is dry programme audio; fully CW is ring-only. |
| Y | Input and ring-stage drive. |

### Up: LFO

| Control | Function |
| --- | --- |
| Main | LFO rate, 0.1-25 Hz. |
| X | LFO amount applied to internal-carrier pitch. Fully CCW is off; fully CW gives a three-octave peak-to-peak sweep. |
| Y | LFO waveform morph from sine-like to square. |

LFO pitch modulation is active while the switch is Up. Returning to Middle restores the stable performance sound. The LFO remains available at CV Out 1 and Pulse Out 1.

### Down: Carrier Type

Press the spring-loaded switch down to select the internal carrier waveform:

| LED 6 flash | Type | Carrier waveform |
| --- | --- | --- |
| Dim | Extermin8 | Sine-like |
| Medium | Cyber | Square |
| Bright | Blend | Halfway sine/square |

The mode names suggest classic British TV monsters.

## LEDs

| LED | Meaning |
| --- | --- |
| 1 | Programme input level |
| 2 | Ring-output level |
| 3 | Stored carrier-frequency setting |
| 4 | Active dry/wet mix |
| 5 | LFO waveform and rate |
| 6 | Drive in Middle, LFO waveform in Up, carrier-type and LFO-reset status flashes |

## Build

```sh
cd releases/202_ScaryLingo
PICO_SDK_PATH=/path/to/pico-sdk cmake -S . -B build
cmake --build build
```

The build produces `build/scarylingo.uf2`.

## Credits and License

ScaryLingo firmware and documentation are Copyright 2026 Adrian Vos and released under the MIT License. The card uses the included `ComputerCard.h` framework by Chris Johnson, MIT licensed; see [ATTRIBUTION.md](ATTRIBUTION.md) for the complete attribution record.
