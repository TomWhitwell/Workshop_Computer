# Vortex Runner 8mu Activity

This guide applies to the stable 8mu firmware:
`uf2/Vortex_Runner_current_stable_8mu_filter_envelope_hp_20261003.uf2`.

## Connecting

1. Insert the card into a Rev 1.1 Workshop Computer.
2. Connect the 8mu to the Computer USB port.
3. Power the Computer with the 8mu connected. It must boot connected so the
   Computer enters USB-host mode.

The Web MIDI editor and the 8mu cannot be used together: the same USB port is
in host mode while the 8mu is attached.

## Layers

The 8mu buttons choose what its eight faders edit. The 8mu's own LED 1–4 shows
the selected layer.

| Button / LED | Faders 1–8 |
| --- | --- |
| A / LED 1 — Tone | Saw, pulse, sine, noise, pulse width, PWM, LP cutoff, resonance |
| B / LED 2 — Amp envelope | Attack, decay, sustain, release, level, expression, portamento, vibrato |
| C / LED 3 — Filter envelope | Filter attack, decay, sustain, release, HP cutoff, resonance, LFO-to-filter, LFO-to-amp |
| D / LED 4 — Performance | Voice-B detune, ring amount, ring speed, LFO rate, vibrato, LFO-to-PWM, LFO-to-filter, LFO-to-amp |

Changing a layer resets soft takeover for its faders. Move a fader through the
stored parameter value before it takes control; this prevents jumps.

## Workshop Computer LED feedback

With the 8mu attached, the Workshop Computer panel LEDs change from their
ordinary panel-page display to feedback for the most recently moved 8mu fader:

| LEDs | Meaning |
| --- | --- |
| 1–3 | Fader position code: `000` = fader 1 through `111` = fader 8; LED 1 is the low bit. |
| 4 | Soft takeover: dim before capture, bright after capture. |
| 5–6 | Physical fader position as a two-segment brightness bar: LED 5 is 0–50%; LED 6 is 50–100%. |

The fader number and position update even before pickup succeeds. This lets a
dim LED 4 show why the fader is not yet changing the sound. Unplugging the 8mu
restores ordinary Workshop Computer panel LEDs.

## Preset browser

The hardware browser remains available with the 8mu attached. Hold the
Workshop Computer switch Down for about two seconds during play, use Main to
choose a factory or saved patch, release Down, then press Down to load it.
While browsing, the six panel LEDs temporarily show the preset number in
binary; this takes priority over 8mu feedback.
