# Vortex Runner — 8mu Controller Experiment

This is an experimental firmware branch for card 580. It is separate from the
stable Vortex Runner release and has its own UF2:

`Vortex_Runner_8mu_controller_experimental_20260920.uf2`

It integrates the Workshop Computer `EightMU` helper with Vortex Runner's
existing combined USB host/device implementation. When the Computer is the USB
host (Rev 1.1 hardware, with an 8mu connected to its USB port), the firmware
identifies the 8mu, requests its fader positions, and uses its eight faders as
an immediate Vortex Runner controller.

## 8mu fader mapping

| 8mu fader | Vortex Runner control |
| --- | --- |
| 1 | Saw level, both voices |
| 2 | Pulse level, both voices |
| 3 | Sine level, both voices |
| 4 | Noise level, both voices |
| 5 | Pulse width |
| 6 | PWM amount |
| 7 | Low-pass cutoff, both voices |
| 8 | Resonance, both voices |

The mapping deliberately corresponds to the card's existing CC20–27 source
and tone controls. The 8mu itself emits CC34–41, so this experiment intercepts
those messages after the 8mu identity handshake; it avoids the stable
firmware's CC34/35 envelope collision.

## Important limitations

- USB is mutually exclusive: use the 8mu in host mode **or** use the Computer
  USB connection for the Web MIDI editor. The current hardware has one USB
  port and host mode is selected at boot.
- This build has passed a clean compile and link only. Test 8mu connection,
  fader pickup, disconnect/reconnect, and audio behaviour on hardware before
  treating it as a release candidate.
- The 8mu buttons, accelerometer, and LEDs are not assigned in this first
  experiment. The helper still performs its normal identify/fader-position
  protocol.

## Build

```sh
cmake -S . -B build
cmake --build build -j2
```

SHA-256 of the included UF2:

```text
2d8e3a1955b29ebcaa6533e761753d62ce1e9312de3b65594ecfecc39dd5dec3
```
