# Punk Confusion

A split-brain punk card for the Music Thing Modular Workshop Computer.

`Switch Up` is the word "punk" taken literally: a voltage-controlled
Atari Punk Console-inspired synth voice. `Switch Middle` is Broken Venue, a
dirty room and damaged PA treatment for external audio. `Switch Down` and
`Pulse In 2` fire short venue-linked vocal calls through the same room engine.

## Quick Start

1. Flash `uf2/punk_confusion.uf2`.
2. Patch `Audio Out 1` to your mixer. Patch `Audio Out 2` as well for stereo
   room output in Broken Venue mode.
3. For APC mode, set the switch Up and turn `Main` up.
4. For Broken Venue, patch audio to `Audio In 1`, set the switch Middle, set
   `Main`, `X`, and `Y` near noon, then choose a room with `X`.
5. Hold the switch Down, or patch gates to `Pulse In 2`, to inject the current
   venue's vocal call.

## Modes

### Switch Up: Atari Punk Console

This is a simple dual-555-style APC model rather than an audio effect.

- `Main`: APC output volume.
- `X`: trigger oscillator rate, like APC pot 1. Clockwise is faster.
- `Y`: monostable one-shot time, like APC pot 2. The useful direction is
  matched to the tested hardware feel.
- `CV In 1`: adds to `X`.
- `CV In 2`: adds to `Y`.
- `Pulse In 1`: hard gate when patched. Unpatched, the APC free-runs.
- `Audio Out 1` and `Audio Out 2`: mirrored APC output.

`X` clocks the astable trigger oscillator. `Y` sets the one-shot length. When
the one-shot is still high, incoming triggers are ignored, giving the classic
skipped, stepped APC behaviour.

### Switch Middle: Broken Venue

Broken Venue processes external audio from `Audio In 1` through a fixed 50/50
dry/wet dirty-room engine.

- `Main`: input/room gain with soft pickup. Below noon attenuates hot modular
  signals, noon is about unity, and clockwise boosts quieter headphone or
  line-level sources. Hot Eurorack-level signals may clip above roughly
  2 o'clock.
- `X`: venue selection in room-length order.
- `Y`: audience absorption. Clockwise means more bodies in the room, reducing
  reflected energy and lightly damping the dry side so the 50/50 mix still
  reads as a room change.
- `Audio Out 1`: main processed output.
- `Audio Out 2`: decorrelated stereo room output.

The `Y` curve is inspired by Rummler/Green/Jurkiewicz/Kahle, "Forget About The
Seat Dip Effect" (Forum Acusticum / Euronoise 2025). The firmware uses a cheap
three-band approximation: small low-frequency loss, stronger attenuation around
`400 Hz-3 kHz`, and moderate high-frequency damping.

### Venue Order

`X` selects snapped venue zones:

| X range | Venue | Character |
|---|---|---|
| `0-1023` | `Marquee` | tight, sharp, short, metallic comb bite |
| `1024-2047` | `CBGB` | cramped, abrasive, short slapback |
| `2048-3071` | `100 Club` | denser, darker, warmer low-mid room |
| `3072-4095` | `Whisky a Go Go` | larger, brighter, splashier stage PA with mild comb bite |

These are not acoustic models of the real rooms. They are four punk-venue
personalities built from one compact delay, reflection, saturation, and
filtering engine.

### Switch Down: Vocal Calls

`Switch Down` is a momentary performance layer, not a separate full mode.

- Pressing or holding Down triggers the vocal call for the currently selected
  venue.
- Releasing Down stops playback, so you can stutter the call manually.
- `Pulse In 2` mirrors this behaviour: high gates the call, rising edges
  retrigger it, and low stops it.
- Driving `Pulse In 2` at audio rate can chop the shout into a raw vocal
  texture. That is intentional.
- `Audio In 2` is an experimental vocal slice/reverse CV input in this branch.
  When patched, positive voltage selects a later start slice for the next
  trigger; negative voltage selects a slice and plays it backwards. Unpatched,
  calls play normally from the start.
- The vocal is routed through the same Broken Venue path and gets an extra send
  into the room delay so it sits inside the venue.

`Audio In 2` is sampled only when the vocal is triggered. It does not scrub an
already-playing shout.

| `Audio In 2` voltage | Slice behaviour |
|---|---|
| Unpatched | Normal forward playback from the start |
| Near 0 V | Forward slice 0, starts at 0% |
| Positive CV, low to high | Forward slices 0-7, starting at 0%, 12.5%, 25%, 37.5%, 50%, 62.5%, 75%, and 87.5% |
| Negative CV, low to high magnitude | Reverse slices 0-7, starting around 12.5%, 25%, 37.5%, 50%, 62.5%, 75%, 87.5%, and near the end |

The Computer audio inputs clip at about `+/-6 V`. Hotter control signals, such
as a full-range Workshop System Slopes output, are safe but do not give extra
slice range: high positive voltages hold the last forward slice, and high
negative voltages hold the last reverse slice.

While holding Down, `Main` edits the saved vocal-call trim rather than the main
room input gain. The trim has soft pickup and is multiplied by the saved room
gain, so later input-gain changes still scale the shout level. The default trim
is midpoint.

## Vocal Samples

The embedded calls are original recordings by Adrian Vos, processed for Punk
Confusion as 24 kHz mono signed 16-bit PCM and kept around `-6 dBFS` peak to
avoid extra digital clipping after the Colourbox drive.

| Venue | Sample |
|---|---|
| `Marquee` | `Oi` |
| `CBGB` | `Hey Ho` |
| `100 Club` | `No Future` |
| `Whisky a Go Go` | `Let's Go` |

The source WAVs are kept in `samples/`, matching the organisation used by other
sample-based releases in this repo. Punk Confusion does not load samples onto an
already-flashed card; instead, custom calls are compiled into a new UF2. This
keeps the firmware simple, reliable, and self-contained.

To build with your own calls, replace the four WAVs in `samples/`, keeping the
same filenames:

- `marquee_oi.wav`
- `cbgb_hey_ho.wav`
- `club100_no_future.wav`
- `whisky_lets_go.wav`

Then run:

```sh
python3 tools/generate_vocal_samples.py
```

This regenerates `VocalSamples.h`, which is compiled directly into the firmware.
Keep replacement samples mono, 16-bit PCM, 24 kHz, short, and conservatively
levelled. The card targets a 2 MB program card, so all samples and firmware must
fit in flash.

## Jack Map

| Jack | Role |
|---|---|
| `Audio In 1` | Broken Venue input |
| `Audio In 2` | Experimental vocal slice/reverse CV |
| `CV In 1` | APC timing CV for `X` |
| `CV In 2` | APC timing CV for `Y` |
| `Pulse In 1` | APC hard gate when patched |
| `Pulse In 2` | Vocal trigger/gate, including audio-rate chopping |
| `Audio Out 1` | Main output |
| `Audio Out 2` | Stereo Broken Venue output, mirrored APC output in Switch Up |
| `CV Out 1` | Unused |
| `CV Out 2` | Unused |
| `Pulse Out 1` | Unused |
| `Pulse Out 2` | Unused |

## LED Map

- `LED 1`: APC mode indicator in Switch Up; venue-zone display in room mode.
- `LED 2`: Broken Venue / vocal mode indicator.
- `LED 3`: divided APC trigger-clock blink in Switch Up; venue-zone display in
  room mode.
- `LED 4`: vocal trigger / Switch Down flash.
- `LED 5`: APC gate-open indicator in Switch Up; venue-zone display in room
  mode.
- `LED 6`: clip / chaos indicator.

Middle-mode venue display on `LED 1` / `LED 3` / `LED 5`:

| Venue | LEDs |
|---|---|
| `Marquee` | `LED 1` |
| `CBGB` | `LED 3` |
| `100 Club` | `LED 5` |
| `Whisky a Go Go` | `LED 1 + LED 3 + LED 5` |

While Switch Down is held, `LED 1` / `LED 3` / `LED 5` temporarily become a three-step
meter for the saved vocal-call trim.

## Building

This release includes source, `CMakeLists.txt`, a local `ComputerCard.h`, the
generated `VocalSamples.h`, and the source WAVs used to generate it.

```sh
cmake -S . -B build
cmake --build build -j2
```

The firmware uses `set_sys_clock_khz(192000, true)` and
`PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`. The build uses the default flash binary
type rather than `copy_to_ram`, because the embedded vocal PCM is too large for
a RAM-copy build.

## Credits

- Card concept, samples, hardware testing, and release direction: Adrian Vos.
- Firmware and documentation assistance: Codex.
- `ComputerCard` library: Chris Johnson, MIT licensed.
- Workshop Computer platform: Music Thing Modular and Tom Whitwell.

This card intentionally builds against the local
[`ComputerCard.h`](ComputerCard.h) copy in this release folder. The upstream
`Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h` copy is left
untouched.

## License

Punk Confusion code, documentation, and included vocal samples are released
under the MIT License. See `LICENSE`.
