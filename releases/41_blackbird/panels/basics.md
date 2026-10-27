# Blackbird basics

Blackbird speaks the same language as [monome crow](https://monome.org/docs/crow/): it runs crow's Lua scripts, and anything that talks to crow over USB (druid, [web-druid](https://dessertplanet.github.io/web-druid/), norns, Max for Live) talks to blackbird the same way, without knowing the difference. Crow's [documentation](https://monome.org/docs/crow/) and [script reference](https://monome.org/docs/crow/reference/) apply as they are, with the jacks mapped as shown here.

## Getting started

1. Connect the Computer's USB to a computer and open [web-druid](https://dessertplanet.github.io/web-druid/) (or [druid](https://monome.org/docs/crow/druid/)). Wait until blackbird has started: its welcome message appears and the bottom-left LED flashes.
2. Type Lua at the prompt, for example `output[1].volts = 3`.
3. Upload a script with druid's `u` (saved to the program card and run at power-up) or `r` (run without saving). It starts straight away. Make the first line start with `---` to name it: the name is printed at startup.

With no script uploaded, blackbird runs [First.lua](https://computer.musicthing.co.uk/programs/41-blackbird/?panel=first-lua), crow's "unique melody box".

## Jacks

| Lua | Jack | |
|---|---|---|
| `input[1]`, `input[2]` | CV In 1, 2 | crow's inputs: `.volts`, `stream`, `change`, `window`, `freq`, ... |
| `output[1]`, `output[2]` | CV Out 1, 2 | crow's outputs, calibrated |
| `output[3]`, `output[4]` | Audio Out 1, 2 | crow's outputs, not calibrated: best for audio-rate ASL and envelopes |
| `bb.audioin[1]`, `bb.audioin[2]` | Audio In 1, 2 | `.volts`, read only |
| `bb.pulsein[1]`, `bb.pulsein[2]` | Pulse In 1, 2 | `.state` and `change` callbacks ([pulse inputs](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/PULSEIN_DETECTION.md)) |
| `bb.pulseout[1]`, `bb.pulseout[2]` | Pulse Out 1, 2 | `:high()`, `:low()`, `pulse()` actions and `:clock(division)` ([pulse outputs](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/PULSEOUT_ACTIONS.md)) |
| `bb.knob.main`, `.x`, `.y` | Main, X, Y knobs | 0 to 1 ([knobs and switch](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/KNOB_SWITCH_API.md), [knobs in ASL](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/KNOBS_WITH_ASL.md)) |
| `bb.switch.position` | Z switch | `'up'`, `'middle'` or `'down'`, with a `bb.switch.change` callback |

The knobs and switch do nothing until a script uses them. The LEDs always show the outputs: the four CV and audio outputs light with their positive voltage, and the pulse output LEDs while the pulse is high.

## Blackbird extras

- **`bb.noise(level)`**: an ASL action that generates noise in the audio loop, for example `output[4](bb.noise(0.5))`.
- **`bb.asap`**: a function run as often as possible on the control thread, for following knobs smoothly. Keep it light. `bb.asap = nil` stops it.
- **`bb.connected`**: whether a cable is plugged into each input (`cv1`, `cv2`, `pulse1`, `pulse2`, `audio1`, `audio2`), from the Computer's normalisation probe.
- **MIDI** over USB, as a device or a host: see the [MIDI tab](https://computer.musicthing.co.uk/programs/41-blackbird/?panel=midi).
- **ii (I2C)** on the Computer's UART pads: see the [ii tab](https://computer.musicthing.co.uk/programs/41-blackbird/?panel=ii).

## More

- Crow scripts to try: [bowery](https://github.com/monome/bowery), and [bbbowery](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/bbbowery), versions adapted to use the Computer's knobs, switch and pulse jacks.
- [Sharing scripts as a single UF2](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/UF2_SCRIPT_INJECTION.md).
- [Integer-only Lua, for heavy DSP-style scripts](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/INT_MATH.md).
