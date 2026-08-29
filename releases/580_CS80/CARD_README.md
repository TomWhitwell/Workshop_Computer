# CS80 Card Guide

CS80 is a draft split-output synthesiser card inspired by the performance feel
of the Yamaha CS-80.

The firmware is in first-pass bring-up. This guide records the current panel
layout so firmware, metadata, and the Web MIDI editor can grow in the same
direction.

## Panel

Switch up: filter page.

- Main: low-pass cutoff for the selected output voice
- X: high-pass cutoff for the selected output voice
- Y: resonance for the selected output voice

Switch middle: oscillator page.

- Main: base pitch offset for both output voices
- X: pulse width for the selected output voice
- Y: PWM amount for the selected output voice

Switch down hold: performance page.

- Main: Voice B detune relative to Voice A
- X: ring modulation
- Y: LFO-to-pitch depth; PWM, filter, and amp destinations are independently controlled in the Web editor

## Planned Inputs

- `Audio/CV In 1`: selectable 1V/oct pitch CV range: 0 V to +5 V with C4 at +3 V, or -3 V to +3 V with C4 at 0 V
- `CV In 1` / `CV In 2`: combined Web UI filter pairing: `CV1 Contour Sweep + CV2 Expression`, `CV1 High-Pass Focus + CV2 Brightness`, or `CV1 Low-Pass Brightness + CV2 Resonance`
- `Pulse In 1`: gate when patched; unpatched, the voice drones for oscillator bring-up
- `Pulse In 2`: trigger, hold, or alternate gesture

## Outputs

- `Audio Out 1`: Voice A
- `Audio Out 2`: Voice B, detuned relative to Voice A

CV and pulse outputs are reserved while the split-output voice is refined.

## Stable Rollback

The current stable rollback target is the dual-output firmware build from
August 29, 2026. The last passed non-dual mono firmware is kept as a second
rollback option in `uf2/`.

## LEDs

LEDs 0 and 1 show whether Voice A or Voice B is selected for hardware-panel
editing. LEDs 2 and 4 show the current X and Y parameter values for the active
switch page. LEDs 3 and 5 show soft-pickup state for X and Y: dim means the knob
has not picked up the stored value yet, bright means turning that knob will edit
the parameter.

## Web MIDI Editor

The editor should expose patch parameters that do not fit on the hardware
surface: oscillator details, envelopes, LFO routing, filter ranges, performance
response, preset names, readback, apply, and save-to-card actions.
