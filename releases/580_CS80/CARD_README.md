# CS80 Card Guide

CS80 is a draft mono synthesiser card inspired by the performance feel of the
Yamaha CS-80.

The firmware is in first-pass bring-up. This guide records the current panel
layout so firmware, metadata, and the Web MIDI editor can grow in the same
direction.

## Planned Panel

Switch up: oscillator page.

- Main: pitch offset
- X: pulse width
- Y: saw/pulse mix and PWM depth

Switch middle: filter page.

- Main: high-pass cutoff
- X: low-pass cutoff
- Y: resonance or brightness

Switch down hold: performance page.

- Main: temporary pitch offset
- X: ring modulation
- Y: LFO depth

## Planned Inputs

- `Audio/CV In 1`: pitch CV
- `CV In 1`: wide filter modulation for high-pass and low-pass cutoff
- `CV In 2`: expression modulation for brightness and resonance
- `Pulse In 1`: gate when patched; unpatched, the voice drones for oscillator bring-up
- `Pulse In 2`: trigger, hold, or alternate gesture

## Outputs

- `Audio Out 1`: mono voice
- `Audio Out 2`: mono copy / future alternate output reserve

CV and pulse outputs are reserved until the mono voice is stable.

## LEDs

LEDs 0, 2, and 4 show the current Main, X, and Y parameter values for the
active switch page. LEDs 1, 3, and 5 show soft-pickup state for those controls:
dim means the knob has not picked up the stored value yet, bright means turning
that knob will edit the parameter.

## Web MIDI Editor

The editor should expose patch parameters that do not fit on the hardware
surface: oscillator details, envelopes, LFO routing, filter ranges, performance
response, preset names, readback, apply, and save-to-card actions.
