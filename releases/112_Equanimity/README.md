# Equanimity

A generative ambient program card for the [Music Thing Modular Workshop
Computer](https://www.musicthing.co.uk/workshopsystem/) inspired by Eno and Chilvers'
*Bloom*.

Melody notes repeat on their own loops, a little quieter each time, and the
overlapping loops create the melody. Beneath them a slow drone holds the root. Everything goes into two
delays, a 1.1-second line and a 9.7-second line, whose repeats drift
slowly past each other.

The movement is tied together through its ebb and flow, one slow **tide** rises and
falls. As it rises, notes arrive more often and ring out brighter, the drone
opens up and glows, and the triggers and wander CVs get busier. As it falls,
everything settles into stasis - this is Equanimity.

Every set has a random root, scale and tone. The melody can play in one
of six tones, all from the same two-operator FM pair: harp, marimba,
xylophone, glockenspiel, vibraphone and bell.

## Quick start

Flash `UF2/equanimity.0.3.4.uf2` and press reset. It plays straight away:
a fresh random root, scale and tone on every power-on. Listen to **Audio
Out 1 and 2**, which are stereo: the echoes drift apart left and right.
Tap the switch down whenever you want a new set, and send triggers to
Pulse In 1 to add notes of your own.

## Controls

| Control | Switch middle | Switch up |
| --- | --- | --- |
| **Main** | Wet/dry | Tone (one of six) |
| **X** | Feedback of the 1.1s line (+ CV In 1) | Melody volume |
| **Y** | Feedback of the 9.7s line (+ CV In 2) | Drone volume |

Down on the switch works the knobs as in the middle position.

- **Tap down:** a new set (the same as Pulse In 2). It acts the moment the
  switch goes down; nothing needs it held there.

**Wet/dry** is an equal-power crossfade between the melody and its echoes:
fully left is the melody only, fully right is the echoes only. The drone
stays out of it, set by its own volume. A little of the drone (its
harmonics, not its bass) goes into the delays, for space.

**Feedback** tops out at about 0.97, never unity. A saturator and lowpass
inside each loop keep it from running away. At full feedback a phrase
holds for a few minutes, then fades. CV In 1 and 2 add to the feedback in
both switch positions.

**Pickup.** The knobs change jobs between up and middle, and each setting
waits for its knob to come back to it before following. Flipping the
switch up doesn't change the tone either: the tone only follows Main once
Main moves. At power-on, the knobs on the page the switch is in take effect
straight away; the other page's settings start at their defaults (wet/dry
and both feedbacks in the middle, melody volume about 80%, drone volume
about 55%) until their knobs pick them up.

**Tones.** A tone change applies from each loop's next strike, so it is
heard within a few seconds; notes already ringing finish as they are. The
tide sets how bright each note is, within its tone.

| Main | Tone | FM ratio | Character | LED |
| --- | --- | --- | --- | --- |
| fully left | Harp | 1:1 | Quick pluck, brightness gone fast, ~2s ring. Gentle, Eno-ish | top left |
| | Marimba | 1:4 | A click of brightness, ~1s ring. Warm, woody, round | top right |
| | Xylophone | 1:3 | Short and sharp, an octave up. Drier, more pointed | middle left |
| | Glockenspiel | 1:2.76 | Bright and ringing, an octave up. Sparkly | middle right |
| | Vibraphone | 1:4 | Soft and steady, long ring, 5Hz tremolo. Late-night | bottom left |
| fully right | Bell | 1:3.5 | Bright burst, slow fade. The Bloom sound | bottom right |

**A new set** (Pulse In 2, or tap down): the current loops play at most
two more repeats (no more than 9 seconds apart), fading faster, while the
drone fades out and the delays' feedback falls to nothing. Once the old notes have rung out, the card waits
10 seconds for the delays to empty: the 9.7s line plays its last echo, then
there is true silence. A fresh random root, scale and tone then start, with
a first note and the drone swelling in on the new root, and the feedback
comes back. The whole change takes about 15 to 35 seconds. New notes
(Pulse In 1) and further new-set requests (taps, Pulse In 2) are ignored
until it's done, so nothing in the old key sneaks into the new set.

## Jacks

| Jack | Job |
| --- | --- |
| Audio In 1, 2 | Into both delays (not the dry mix): a field recording, another voice |
| CV In 1 | Added to the 1.1s feedback. Positive adds, negative removes |
| CV In 2 | Added to the 9.7s feedback |
| Pulse In 1 | A new note, straight away |
| Pulse In 2 | A new set (see above) |
| Audio Out 1, 2 | Melody and drone (centred) plus both delays (each side reads its own wobbling tap) |
| CV Out 1 | Ebb: the mirror image of CV Out 2. The two always add up to about 5V |
| CV Out 2 | Flow: a smooth random voltage, 0 to about +5V, faster at high tide |
| Pulse Out 1 | A trigger on every melody note, repeats included: a loose, breathing clock |
| Pulse Out 2 | Tide triggers: one every 20s or so at low tide, clusters at high tide |

The triggers are 10ms long. Patched to two VCAs, CV Outs 1 and 2 crossfade
or pan between two sources. Unpatched inputs contribute nothing. Pulse In
edges count after 20ms, and only if the cable is still in, so pulling a
cable out never triggers anything.

**LEDs.** With the switch in the middle, the six LEDs are a tide meter,
filling from the bottom row to the top, with a soft flicker each time a
note is born. With the switch up, one LED shows the tone.

## How it works

- **Melody.** Eight two-operator FM voices. The modulator wobbles the
  carrier's phase; the ratio between them decides which partials sound
  (whole numbers for harmonic tones, 2.76 and 3.5 for metallic ones). The
  brightness (modulation index) dies away faster than the volume, and a
  gentle lowpass follows. A note born at high tide is bright and clear; one
  born at low tide is soft, almost a sine.
- **Note loops.** Each note gets a pitch from the scale, spanning two
  octaves from the root in octave 4. It also gets a loop period of 5 to 25
  seconds (wide apart, so the melody has space and the delays fill the
  gaps) and 4 to 10 repeats, each quieter than the last. New notes
  arrive about every 12s at low tide and every 2.5s at high tide. With all
  eight loops busy, the quietest one is retired.
- **Scales.** Major, Minor, Major Pentatonic, Minor Pentatonic, Dorian and
  Lydian, on any of the twelve roots.
- **Drone.** Built the way Eno describes his own: not one tone but
  "several unstable elements that change in both timbre and volume", each
  on its own long cycle. Four layers on the set's root: the root in octave
  2 (65–123Hz), the 5th, the octave and the 5th above that, each a 1:1 FM
  pair a few cents off the others, so they beat gently. Each layer has a
  volume cycle and a brightness cycle of its own, between 30 and 71 seconds
  long. All eight lengths are unrelated ("incommensurable", like the tape
  loops of *Music for Airports*), so the layers drift in and out of
  prominence and never line up the same way twice. The root never sinks
  below about half, so the key always holds; the upper 5th comes and goes.
  A resonant lowpass over the whole drone drifts on a ninth cycle (83s) and
  opens with the tide, as does each layer's brightest point. The colour
  keeps shifting (root-heavy, open fifth, bright octave) but the drone never
  changes note within a set. Like Bloom's drone, it changes pitch only with
  a new set: it fades out with the old set (2s) and swells in on the new
  root (6s).
- **The tide.** Three slow sines (47s, 113s, 271s) at random phases,
  summed. It's squared before it sets birth rates and trigger density, so
  the still passages last longer and the busy ones feel earned.
- **Delays.** The short line stores 16-bit samples at 24kHz (53KB). The
  long line stores 8-bit mu-law samples at 8kHz (78KB), so it is dark and a
  little grainy, which suits distant repeats. Each read point drifts slowly,
  like tape. Feedback is taken from the left read point of each line
  (averaging both wobbling points would comb-filter the echoes more on every
  repeat), and a ~2Hz DC blocker on the delay input keeps any offset from
  building up in the loops.

Every tuning value is a named constant in [`src/Config.h`](src/Config.h),
so you can adjust it by ear. That covers the six tones, tide periods, birth
rates, the drone's layers, cycles, filter and send, the feedback ceiling,
wobble depths, the changeover timings and the output level.

The RP2040 runs at 192MHz (an officially supported setting, at 1.15V). At
the usual 144MHz there wouldn't be enough margin for eight FM voices, the
drone and two delays. At 192MHz the worst case measures 79% of the time
available per sample.

## Building

You need the Pico SDK (2.1.1) and the ARM GNU toolchain.

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

That gives `build/Equanimity.uf2`.

### Tests and renders

The whole instrument also builds on a computer, driven by a pretend panel.

```bash
make -C test          # host tests
make -C test render   # WAVs in test/out/, to hear before flashing
```

The renders are: each tone in turn, ten minutes of the tide at work, a
full-feedback pad, a change of set, and the drone on its own.

### Timing check

`ProcessSample()` must finish in ~20µs, every sample. `tools/bench/cycles.py`
runs the compiled firmware on an emulated Cortex-M0+ and reports the
cycle count per sample across a scripted performance.

```bash
python3 -m venv venv && venv/bin/pip install unicorn capstone
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DEQUANIMITY_BENCH=ON
cmake --build build-bench
venv/bin/python tools/bench/cycles.py build-bench/Equanimity.elf
```

## Status

0.3.4, released. Every version has been played on hardware, as well as
tested on a computer and measured on the emulator.

Along the way: everything moved inside the Computer in 0.2 (the first
version played its drone on the Workshop System's analog oscillator), with
the six tones, random root and scale, triggers and ebb/flow CVs. 0.3
rebuilt the drone as Eno-style drifting layers and made a tap down start a
new set. Since then the tuning has been by ear: the drone up an octave, and
loop repeats 5 to 25 seconds apart, so the melody has space and the delays
fill the gaps.

## Credits and licence

By Matt Allison. MIT licence (see `LICENSE`).

The firmware was written with Claude (Anthropic), working from Matt's spec
and changes, and from the structure, pickup, random and smooth-random code
of his Impermanence card.

[ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard)
v0.4.0 by Chris Johnson (MIT, `lib/ComputerCard.LICENSE`).
