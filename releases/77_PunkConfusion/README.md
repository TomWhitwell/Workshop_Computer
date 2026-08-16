# Punk Confusion

A split-brain punk card for the Music Thing Modular Workshop Computer.

`Switch Up` is the word "punk" taken literally: a voltage-controlled
Atari Punk Console-inspired synth voice. `Switch Middle` is Broken Venue, a
dirty room and damaged PA treatment for external audio. `Switch Down` and
`Pulse In 2` fire short venue-linked vocal calls through the same room engine.

## Quick Start

1. Flash `uf2/punk_confusion_2mb.uf2` for a standard 2 MB card, or
   `uf2/punk_confusion_16mb.uf2` for a 16 MB card.
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
- `Audio In 2` is a beta vocal slice/reverse CV input.
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
sample-based releases in this repo. The beta firmware includes those calls as
factory fallback samples, but the normal user workflow is now WebSerial loading:
flash the card once, then change the four vocal calls from a browser without
rebuilding or reflashing the UF2.

### WebSerial Sample Loader

The WebSerial loader stores uploaded calls in a reserved flash sample bank.
Uploaded samples persist across normal restarts and may also persist after
reflashing, so the web page includes a simple factory-restore button.

Use it like this:

1. Flash `uf2/punk_confusion_2mb.uf2` for a standard 2 MB Workshop Computer
   card, or `uf2/punk_confusion_16mb.uf2` for a 16 MB card.
2. Open `web/index.html` in Chrome, Edge, or another Chromium-based browser
   with WebSerial support.
3. Choose the matching card size in the page.
4. Drop one audio file into each venue slot on the page.
5. Hold the card switch Down while powering or resetting the card.
6. Wait for confirmation: all LEDs flash three times, then LEDs 1, 3, and 5
   stay lit while the card waits for the browser.
7. Press `Connect card`, choose the Workshop Computer serial device, then press
   `Send these sounds to the card`.
8. Restart the card and use it normally with the new shouts.

The uploaded samples are stored in flash, so they can persist even after you
reflash the firmware. To return to the embedded factory shouts, enter loader
mode again and press `Use built-in sounds again` in the web page. That erases
the uploaded sample-bank header, so the firmware falls back to `VocalSamples.h`.

The WebSerial builds use 24 kHz 8-bit µ-law samples in the flash sample bank.
This is deliberately more compact than the embedded 16-bit PCM header and lets
the firmware jump to slice points or play backwards without loading whole files
into RAM. The standard 2 MB build reserves about `1 MB` for uploaded samples;
the 16 MB build reserves about `14 MB`.

The web page converts source files to mono 24 kHz µ-law and shows how much of
the sample bank they will use before upload. If the sounds are too large, the
page will ask you to shorten them or use the 16 MB build. Factory fallback WAVs
and `VocalSamples.h` are backed up in `factory-samples/` for maintainers.

## Jack Map

| Jack | Role |
|---|---|
| `Audio In 1` | Broken Venue input |
| `Audio In 2` | Beta vocal slice/reverse CV |
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

This release builds two beta firmware targets from the same source: one for
standard 2 MB cards and one for 16 MB cards.

```sh
cmake -S . -B build
cmake --build build -j2
```

The generated UF2s are:

- `build/punk_confusion_2mb.uf2`
- `build/punk_confusion_16mb.uf2`

The firmware uses `set_sys_clock_khz(192000, true)`,
`PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`, and the default flash binary type. The
2 MB target reserves `1 MB` at the top of flash for user samples; the 16 MB
target reserves `14 MB`.

To run the WebSerial loader page locally:

```sh
make webui
```

Then open `http://127.0.0.1:8765/` in a Chromium-based browser.

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
