# Vortex Runner — 8mu Layered Controller Experiment

This is the second experimental 8mu pass for card 580. It is independent of
both the stable Vortex Runner release and the first direct-fader experiment.

Flash `Vortex_Runner_8mu_layered_controller_experimental_20260920.uf2` to test
it. The Computer must boot in USB host mode with an 8mu attached; it cannot
also run the Web MIDI editor from the same USB port.

## Button-selected layers

The four 8mu buttons select what the eight faders edit. A press changes the
firmware's active layer; it does not send a different fader CC from the 8mu.
The corresponding 8mu LED (1–4) lights to show the selected layer.

| Button / LED | Layer | Faders 1–8 |
| --- | --- | --- |
| A / LED 1 | Tone | Saw, pulse, sine, noise, pulse width, PWM, LP cutoff, resonance |
| B / LED 2 | Amp envelope | Attack, decay, sustain, release, output level, expression, portamento, vibrato depth |
| C / LED 3 | Filter envelope | Filter attack, decay, sustain, release, LP cutoff, resonance, LFO-to-filter, LFO-to-amp |
| D / LED 4 | Performance | Voice-B detune, ring amount, ring speed, LFO rate, vibrato, LFO-to-PWM, LFO-to-filter, LFO-to-amp |

The amp and filter envelopes remain shared by the two outputs, consistent with
Vortex Runner's monophonic architecture.

## Soft takeover

Changing layers resets takeover for that layer's faders. A fader takes control
only once its physical position comes within approximately 2.3% of the stored
parameter value. This prevents abrupt changes when a fader's position belongs
to a different layer. Once captured, it responds normally until that layer is
selected again.

## Validation status

- Clean configured build and link completed successfully.
- No hardware test has been performed. Test 8mu identification, layer LEDs,
  each button, fader capture, disconnect/reconnect, and audio behaviour before
  considering this a release candidate.

SHA-256:

```text
71f2c8e978416ec24473de1ffc3e5ff9699fda5f542a8deea4fdb3dcd28bd16a
```
