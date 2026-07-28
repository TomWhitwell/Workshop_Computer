# Two Tracks — Phase Looper

A dual-read-head audio looper inspired by Steve Reich's phase music. Record a mono loop to flash, then play it through two independent outputs with separately controllable read positions and loop lengths. The two heads drift apart and reconverge, building evolving phase patterns and interference textures.

## Setup

1. **Audio In** — plug your instrument or audio source into **CV/Audio 1** (leftmost jack). Recording is mono; only this input is sampled
2. **Audio Out** — the card's main output carries the mixed playback of both read heads. Patch it to your mixer / module input
3. **Power** — the card is powered through the 10-pin ribbon cable from the Workshop System backplane. No external power needed
4. **First boot** — the card starts in PLAY with an empty (silent) loop. LED 0 glows dim as an "alive" indicator
5. **Quick start** — press **Z Down** to arm recording. After the 3-second countdown, play your instrument. Press **Z Down** again to stop. Turn knobs to hear the two heads drift apart
6. **Persistence** — the recorded loop is stored in flash memory. It survives restarting the card and powering off/on. To record a new loop, just press **Z Down** and record over it

## Recording Workflow

Recording is hands-free and runs through three states: **PLAY → ARMED → RECORD → PLAY**.

1. **Z Down** (in PLAY) arms recording. A 3-second countdown starts:
   - A short ~10 ms square blip marks each second remaining (3, 2, 1)
   - All 6 LEDs light on arm. At each tick the next row turns off — bottom row first, then middle — so the display shrinks toward zero as the take approaches
2. **Countdown expires** — recording starts on its own. All 6 LEDs flash for ~200 ms (the "GO" signal), then become a 3-tier audio level meter
3. **Z Down** (in RECORD) stops recording and returns to PLAY (after a ~2 ms / 100-sample debounce so the same press can't re-arm instantly)

Notes:
- Press Z Down again during the countdown to **restart** the 3-second timer
- Z Middle and Z Up during the countdown do nothing — the countdown always runs to completion
- Audio passes through unchanged while armed
- Boot starts in PLAY with an empty (silent) loop. LED 0 glows dim as an "alive" indicator until you record something
- Recording is **mono** (Audio In 1). Both read heads play back the same loop
- The Z Down edge that stops recording gets consumed by the debounce, so you have to release Z and press again to re-arm for the next take

## Features

- **Flash-backed loop** at 48 kHz, IMA-ADPCM compressed to 4 bits per sample. Capacity depends on the card: ~70 seconds on 2 MB, ~11 minutes on 16 MB
- **Two independent read heads** with separate position and loop-length control
- **Armed countdown** — 3-2-1 tick beeps before recording, hands-free start
- **Two play modes**, selected by the Z switch in PLAY:
  - **Offset mode** (Z Middle): each knob sets a position offset within the loop (0 = start, max = end)
  - **Phasing mode** (Z Up): the right loop runs shorter than the left, so the two heads naturally drift in and out of phase
- **Loop window** (Big Knob, Z Middle): sets what fraction of the recorded loop plays back — from tiny rhythmic fragments up to the full loop
- **Right loop shortening** (Big Knob, Z Up): sets how much shorter the right loop is — CCW = short (fast phasing), CW = full length (no phasing)
- **Click-free loop boundaries** — a 64-sample crossfade blends the live signal out and a pre-decoded loop-start preview in, plus a 32-sample bridge served straight from the preview while the head re-seeks
- **Cubic Hermite interpolation** (4-tap Catmull-Rom) for sub-sample read positions
- **Asynchronous decode** — core 1 refills each head's 4096-sample ring from flash, off the audio path. Core 0 only reads decoded PCM and interpolates. On underrun the head holds its last sample (a momentary freeze, not a click)
- **CV modulation** of both offsets (Z Middle) and right loop length/speed (Z Up)
- **Pulse reset** — Pulse 1 resets the left read head, Pulse 2 resets the right
- **CV outputs** track each read head's phase position

## Controls

| Control | Z Down | Z Middle (Offset) | Z Up (Phasing) |
|---------|--------|-------------------|----------------|
| Big Knob | — | Loop window (tiny → full) | Right loop length (short → full) |
| Knob X | — | Left position offset | Right loop window start |
| Knob Y | — | Right position offset | Right playback speed (0.9×–1.1×, centre = 1.0×) |
| CV In 1 | — | Modulate left offset | Modulate right loop length |
| CV In 2 | — | Modulate right offset | Modulate right speed (Y) |
| Pulse In 1 | — | Reset left head to loop start | Reset left head to loop start |
| Pulse In 2 | — | Reset right head to loop start | Reset right head to loop start |

**Z Down behaviour by state:**
- **PLAY** → ARMED (start the 3-second countdown)
- **ARMED** → restart countdown
- **RECORD** → stop recording, return to PLAY

## LED Feedback

```
0 1
2 3
4 5
```

| LED | Armed | Record | Play (Offset) | Play (Phasing) |
|-----|-------|--------|---------------|----------------|
| 0 | On → off at countdown end (top row) | Level meter (top, loud) | Left output level | Left output level |
| 1 | On → off at countdown end (top row) | Level meter (top, loud) | Right output level | Right output level |
| 2 | On → off at ~2s remaining (middle row) | Level meter (middle, medium) | Left offset indicator | Right loop window start (X) |
| 3 | On → off at ~2s remaining (middle row) | Level meter (middle, medium) | Right offset indicator | Right speed deviation |
| 4 | On → off at ~3s remaining (bottom row) | Level meter (bottom, soft) | Left loop position | Left loop position |
| 5 | On → off at ~3s remaining (bottom row) | Level meter (bottom, soft) | Right loop position | Right loop position |

During the armed countdown all 6 LEDs start on. The bottom row turns off at the 3-second tick, the middle row at the 2-second tick, and the top row when recording begins — so the display shrinks toward zero as the take approaches. When recording starts, all 6 LEDs flash briefly (the "GO" signal), then become a 3-tier audio level meter: soft sounds light only the bottom row, louder sounds add the middle row, and the loudest sounds light the top row.

In play mode, LEDs 4 and 5 show actual read position in the loop (bright = start, dark = end). In phasing mode you can watch them slowly separate and reconverge.

## CV Outputs

| CV Out | Armed | Record | Play (Offset) | Play (Phasing) |
|--------|-------|--------|---------------|----------------|
| CV Out 1 | 0 | Buffer fill position | Left phase position | Left phase position |
| CV Out 2 | 0 | Recording length so far | Right phase position | Right phase position |

In PLAY, phase position maps across the full bipolar CV range relative to each head's loop window: window start ≈ −6 V, window end ≈ +6 V. In RECORD, both CV outputs rise as the recording progresses (showing how full the buffer is and how long the take has run).

## Flash

Hold BOOTSEL on the Pico, plug in USB, mount the RPI-RP2 drive, then copy `two_tracks.uf2` to the mounted drive.

## Creative Possibilities

- **Classic Phase** — Z Middle, both knobs at 0. Slowly turn Knob X to walk the left head into a canonic phase shift against the right
- **Natural Phasing** — Z Up, Big Knob CCW for a short right loop (fast phasing), CW for full length (no phasing). The two heads drift apart, then lock back together
- **Speed + Length Phasing** — Z Up, add a slight speed offset with the Y knob on top of the length difference for a less mechanical, more organic phasing feel
- **Window Position** — Z Up, Knob X sets where the shortened right window starts within the loop — play the beginning, the end, or anywhere between
- **Rhythmic Fragments** — Z Middle, turn Big Knob down to a small window. Tiny loop fragments phasing against each other build intricate rhythmic patterns
- **Window Sweep** — Z Middle, slowly turn Big Knob from min to max. The loop gradually reveals more material — a powerful performance gesture
- **CV Modulation** — patch an LFO into CV1 or CV2 for continuously evolving phase
- **Independent Resets** — use Pulse 1 and Pulse 2 to restart each read head separately for polyrhythmic effects
- **Hands-Free Recording** — Z Down starts the 3-2-1 countdown. Step away, and recording begins on its own — handy for capturing room tone or live performances

## Technical Notes

- **Storage**: IMA-ADPCM (4 bits/sample) in the program card's flash, mono. Audio gets the full flash budget after the 384 KB firmware reserve + ~36 KB header — ~70 s on 2 MB, ~11 min on 16 MB
- **Adaptive keyframe interval** (512 samples on 2 MB → 4096 on 16 MB) keeps the in-RAM decoder-state index at ≤ 8192 entries (~32 KB), so any position can be reached by seeking to the nearest keyframe and decoding forward
- **Dual-core RP2040**: core 0 runs the audio DSP (state machine, dual-head playback, knob/CV mapping); core 1 runs the flash I/O engine — erase-ahead with erase-suspend programming (0x75/0x7A) so the flush frontier keeps advancing during erases, per-head 4096-sample ring refill, and preview/seek decode
- **192 MHz system clock** (a multiple of 48 kHz) with raised core voltage for the dual-core ADPCM workload
- **Fixed-point 24.8 phase tracking** (8 fractional bits) for sub-sample positioning, with cubic Hermite (4-tap Catmull-Rom) interpolation at read positions
- **Loop-boundary crossfade**: 64-sample blend of the live signal (fading out) with a pre-decoded loop-start preview (fading in), then a 32-sample bridge served straight from the preview while the head re-seeks. No clicks at wrap
- **Knob values slewed `>>6`** (slower than typical) to reduce audible ADPCM boundary crossings when turning knobs
- **`copy_to_ram` binary type** — all code lives in RAM so core 0 never stalls on XIP while core 1 is mid-erase. All core-0 audio functions are marked `__not_in_flash_func`
- **`PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`** for reliable reset behaviour

## Source

Full source code, build instructions, and the `ComputerCard.h` framework are at:

**https://codeberg.org/johantv/two-tracks**

- **Storage engine**: `two_tracks_stream.c/.h` — forked from Goldfish 2.0 (releases/11_goldfish) by Dune Desormeaux, adapted for mono, no CV stream, and no delay mode. Erase-suspend programming, page ring, keyframe snapshots, and per-head ring refill preserved verbatim
- **ADPCM codec**: `adpcm.h` from the Music Thing MLRws card (releases/15_MLRws)
- **Flash-size detection**: `flash_size.h` from Goldfish (JEDEC ID probe). Two Tracks currently uses the SDK's compile-time `PICO_FLASH_SIZE_BYTES` instead of the runtime probe, since the JEDEC query can hang the SSI on some cards after prior flash writes. Override `PICO_FLASH_SIZE_BYTES` for 16 MB cards
- **DSP pattern reference**: Goldfish (releases/11_goldfish)
- **Framework**: `ComputerCard.h` (header-only, by Chris Johnson)

## Credits

Inspired by Steve Reich's phase music — *It's Gonna Rain* (1965) and *Piano Phase* (1967) — where identical musical lines drift out of sync and reconverge, revealing evolving patterns from the interference.

- Storage engine forked from Goldfish 2.0 by Dune Desormeaux (releases/11_goldfish)
- IMA-ADPCM codec from the Music Thing MLRws card by Dune Desormeaux (releases/15_MLRws)
- Built with the Workshop Computer ComputerCard library by Chris Johnson
- Built by Joep Vermaat using opencode and GLM 5.2

Many thanks to Tom Whitwell and the Music Thing Modular community for making this highly addictive and inspiring platform.

## License

This project is released under the [MIT License](LICENSE). Use it, modify it, fork it, break it, improve it. No warranties, no liabilities. If you build something from this, you don't owe me anything — but I'd love to hear about it.