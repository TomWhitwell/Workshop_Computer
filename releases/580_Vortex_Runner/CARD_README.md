# Vortex Runner Card Guide

Vortex Runner is a Beta split-output synthesiser card inspired by the performance
feel of the Yamaha CS-80. This guide covers the shared panel layout; the main
README lists the standard and 8mu firmware editions and their build targets.

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
- `Pulse In 2`: not currently used by this firmware

## Outputs

- `Audio Out 1`: Voice A
- `Audio Out 2`: Voice B, detuned relative to Voice A

CV and pulse outputs are reserved while the split-output voice is refined.

## Firmware Editions

The catalogue offers two 0.1.2 Beta downloads: standard and 8mu. Both use the
current synth/filter/preset implementation and shared Web MIDI editor.
The standard edition omits 8mu controller layers; the 8mu edition adds them.
Both new binaries still require hardware verification. Full source and separate
build targets for both are in this folder; see
[the main README](README.md#building-both-editions).
Older UF2s are retained in the [firmware archive](uf2/archive/README.md) for
recovery and excluded from catalogue downloads.

## LEDs

LEDs 0 and 1 show whether Voice A or Voice B is selected for hardware-panel
editing. LEDs 2 and 4 show the current X and Y parameter values for the active
switch page. LEDs 3 and 5 show soft-pickup state for X and Y: dim means the knob
has not picked up the stored value yet, bright means turning that knob will edit
the parameter.

With an 8mu connected at boot, these LEDs instead provide 8mu fader feedback:
fader number, soft-takeover state, and fader position. See
[8MU_ACTIVITY.md](8MU_ACTIVITY.md) for the complete 8mu workflow.

## Filter Architecture

Both firmware editions uses a 12 dB/octave high-pass path feeding a 12 dB/octave
low-pass path for each output. Voice A and Voice B retain independent cutoff,
resonance, and filter state; the filter envelope and modulation routings are
shared. Resonance is bounded below self-oscillation and affects both paths.

## Web MIDI Editor

The editor should expose patch parameters that do not fit on the hardware
surface: oscillator details, envelopes, LFO routing, filter ranges, performance
response, preset names, readback, apply, and save-to-card actions.

The Web UI has separate shared amp ADSR and filter ADSR controls. MIDI `CC32`
to `CC35` act as linked envelope controls: attack, decay, sustain, and release
move both envelopes together while preserving the relative values set in the Web
UI. `CC31` is portamento.

## Attribution

This card includes `computercard.h`, the ComputerCard hardware library by
Chris Johnson (version 0.3.0), used under its MIT licence.
