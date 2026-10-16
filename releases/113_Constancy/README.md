# Constancy

A generative program card for the [Music Thing Modular Workshop
Computer](https://www.musicthing.co.uk/workshopsystem/) that plays one
Cornish morning, forever.

The morning is 17 April 2024 at Calamansac, on Port Navas Creek off the
Helford River. It was the last day of Tom Whitwell's five-day Music Thing
Modular residency there, part of the Workshop System's origin story. The
card holds that one dawn in memory: the tide, the sun, the wind and the
cloud, from first light to two hours after sunrise. Nothing moves unless
you move through the morning with the **Time** knob.

The voice is a Vangelis-style CS-80 supersaw through a fixed-point port of
the feedback loop from Infinite Digits' [Icarus](https://github.com/schollz/icarus)
for norns. The notes and keys come from Equanimity. Low tide gives short,
plucky strikes; high tide stretches everything into long, washed swells.
The lead only appears once the sun clears the horizon.

It sits in the series beside Uncertainty, Scintillator,
Impermanence and Equanimity.

## Quick start

Flash `UF2/constancy.0.3.2.uf2` and press reset. It starts playing at
once, in a random key. Listen to **Audio Out 1 and 2** (stereo).

Turn **Main** slowly from left to right: night, then the first grey light,
then the sun coming up, then the morning. The lead starts to sing a little
after halfway. Turn **X** (Sun) right of centre to make the loop burn, and
**Y** (Water) left to drown it or right to thin it. Tap the switch down for
a new key.

To play the rest of the Workshop System along with it, patch a clock into
Pulse In 1, **CV Out 2** to an oscillator's pitch and **Pulse Out 2** to an
envelope: a Turing machine plays in the card's key. For its notes to land
in that key, first tune the oscillator to **C2** (65.41Hz, with A at
440Hz) with nothing patched into its pitch input, then leave its tuning
alone. 0V is C, and the card works out every key from there.

## The morning

| BST | The sun | Table position (Main) |
| --- | --- | --- |
| 04:17 | 18° below: astronomical dawn, the window opens | fully left |
| 05:05 | 12° below: nautical dawn | about 20% |
| 05:48 | 6° below: civil dawn | about 37% |
| 06:22 | sunrise | about 51% |
| 06:57 | low water | about 65% |
| 08:21 | 18° above: the window closes | fully right |

The sun climbs evenly from 18° below the horizon to 18° above. This was a
neap morning (first quarter moon was on 15 April). The tide was falling
through most of the window, reached low water about half an hour after
sunrise, and was just turning to flood when the window closed. So the dark
is the high-tide end, with long swells. Sunrise is plucky. The feedback
settles as the tide turns at 06:57. At the end, the swells start to come
back. It was a grey morning: the cloud cleared briefly around sunrise and
closed in again (the card keeps the cloud data, though since 0.3 nothing
uses it). The wind rose from about 18 to 23 km/h.

Each stream is scaled to its full range across the window. Four hours is
only a third of a tide, so without scaling the card would sound timid.
Between its 128 points the card blends smoothly, so nothing steps.

## Controls

| Control | Switch middle: **Play** | Switch up: **Levels** |
| --- | --- | --- |
| **Main** | **Time.** First light to two hours after sunrise (+ CV In 1) | **Turing machine.** Pendulum and locked, random, locked |
| **X** | **Sun.** Feedback and drive: fly close and it burns | **Melody level** |
| **Y** | **Water.** Centre open; left drowns (lowpass), right thins (low-cut) (+ CV In 2) | **Drone level** |
| **Switch down** | Tap: a new key | The same |

**Time** is manual. The morning moves only when Main or CV In 1 moves it.
Parked, the card holds that moment, and the loops and lead keep playing in
it.

**Sun** runs the loop's feedback from 0.4 through 0.8 at the centre up to
1.5, Icarus's top. Its drive into the soft clipper rises with it, from 1.0
through Icarus's 1.25 at the centre to 2.0. At the centre the loop's gain
is just under one: long tails that still fade. Right of centre it passes
one. The clipper and the ladder filter then hold the loop at a burning,
saturated sustain instead of letting it run away. Past that point the wet
level is trimmed gently, so the burn grows without jumping out.

**Water** is a 12dB-per-octave lowpass to the left of centre (16kHz down to
120Hz) and a 12dB-per-octave low-cut to the right (15Hz up to 2.5kHz). Both
are open across a small dead zone in the middle. Everything (voices, loop
and drone) goes through it on the way out. Each filter fades in over the
first eighth of its side, and one only hands over to the other once it has
faded right out, so sweeping through the centre never clicks. CV In 2 adds
to the knob: an LFO there makes the tide wash in and out.

**Turing machine** (Main on the Levels page): how much its eight-step
loop changes, and which way it plays. At the centre every step is random.
Turning either way, steps change less and less often (the loop slips now
and then), until near the end of travel it's locked. Fully clockwise, the
locked loop plays forwards, round and round. Fully anticlockwise, it plays
as a pendulum: forwards to the eighth step, then back (1-8-1). See the
Jacks and How it works.

The **delay mix** (the voices as played, against the Icarus loop) is fixed
at about two-thirds wet: `kDelayMix` in `src/Config.h`.

**Pickup.** The knobs change jobs between the two pages. Each setting keeps
its value, and waits for its knob to come back through it before
following, so nothing jumps. At power-on the page the switch is on takes
its knobs as they are. The other page starts at its defaults until picked
up: Time, Sun and Water in the middle; the Turing machine fully clockwise
(locked); melody at 80% of full volume, drone at 40% (levels are knob
position squared, so their knobs sit at about 89% and 63%).

**A new key** (tap down): every melodic note stops. The
loops end with no further repeats, and the notes still sounding (the
lead's too) fade as they would rather than being cut. A new random root
and scale are picked at once, and the drone glides over four seconds to
the new root, whichever octave of it is nearer. Meanwhile the loop's
feedback dips so the old key drains out, even if Sun has it burning. No
new melodic note starts until the drone has arrived and the old notes
have died away (and at least three seconds have passed). Then the new
key's loops begin, the first almost at once, and the feedback comes back.

## Jacks

| Jack | Job |
| --- | --- |
| Audio In 1, 2 | Into the Icarus loop only: a field recording, another voice |
| CV In 1 | Added to Time: an LFO or a slow random drifts the morning. +5V moves it about the whole window |
| CV In 2 | Added to Water: negative drowns, positive thins |
| Pulse In 1 | Clock for the Turing machine: each pulse is one step |
| Pulse In 2 | A lead phrase now, whatever the sun is doing |
| Audio Out 1, 2 | Stereo mix of the voices, the loop and the drone |
| CV Out 1 | **Tide.** A smooth random voltage, 0 to about +5V, centred on the tide's height; still air wanders slowly, wind makes it wider and choppier |
| CV Out 2 | **Turing pitch.** 1V per octave, in the card's key, two octaves up from the root (-1V to +2V), with 0V as C: tune the oscillator to C2 at 0V. Calibrated: run the Simple MIDI card's calibration first for it to track |
| Pulse Out 1 | A 10ms trigger on every lead note |
| Pulse Out 2 | **Turing trigger.** 10ms, with each step whose bit is 1, like the original Turing Machine's pulse output |

Unpatched inputs contribute nothing. Pulse In edges count after 20ms, and
only if the cable is still in, so pulling a cable out never triggers
anything.

**LEDs.** On the Play page the six LEDs show the sun's height, filling
from the bottom row to the top. They all glow together as the sun crosses
the horizon, and flicker at a new key. On the Levels page the top row is
the Turing machine: dark at the centre (random), the left LED brightening
towards a locked pendulum, the right towards locked forwards. The middle
row is the melody level, the bottom row the drone's.

## How it works

- **The voice.** Each melodic voice is a stack of PolyBLEP sawtooth waves
  a few cents apart, the supersaw: five for the lead, three for each loop.
  Every saw keeps Icarus's random pitch wander, a sine of up to ±10 cents
  whose speed jumps to a new random value every second. Saws alternate
  left and right, so each voice is wide. The envelope is Icarus's cubed
  ADSR: attacks start gently and arrive fast; releases drop quickly and
  then trail off. After the voices comes the CS-80's lowpass. It is
  muffled before dawn (1.5kHz) and opens with the sun (10kHz by morning).
  The lead's own copy swells by up to 1.3 octaves after each attack,
  standing in for the CS-80's aftertouch. A makeup gain follows the
  morning so the melody sounds about as loud throughout: without it, the
  dark plucks around civil dawn measured 16dB quieter than the bright,
  long-sustaining morning (`kMelodyMakeupDbX10` in `src/Config.h`).
- **The loops (voices 2 to 4).** Equanimity's note loops. Each owns one
  note of the scale and repeats it every 5 to 15 seconds, quieter each
  time, for 4 to 9 repeats; then it rests, and a new loop begins. Each
  loop owns its voice, so a repeat only restarts its own note, swelling up
  from wherever its release had got to. A new loop waits for silence: no
  stealing, no clipped tails. Each strike takes its envelope from the
  tide at that moment.
- **The lead (voice 1).** Phrases of three to six notes, mostly moving by
  step through the scale, ending on a long held note, then silence. Some
  notes glide in from the last (portamento). Vibrato eases in after 350ms
  of holding. Now and then the last note sinks 1.5 to 3 semitones as it
  fades, like easing off the CS-80's ribbon. There are no phrases before
  sunrise. Just after sunrise one comes about every 22 seconds, and by the
  top of the window about every 5. Scrub back below the horizon and it
  stops.
- **The drone.** Root and fifth, two octaves below the melody (65–123Hz).
  Two drifting saws on the root, left and right, and one on the fifth in
  the centre. The fifth is tuned pure: exactly 3:2, not the equal-tempered
  fifth a keyboard plays, which beats slowly against the root. Pure, it
  locks, and the drone stays still. It has its own gentle lowpass and a
  47-second breath. It goes around the loop; only a little goes through.
- **The Turing machine.** After Tom Whitwell's Music Thing Modular Turing
  Machine (and his Workshop Computer card of it), written afresh here.
  Eight bits in a loop, one step per clock on Pulse In 1. The eight bits
  read from the playhead onwards make a number, 0 to 255, which picks a
  note of the card's current key from the root in octave 3 up two octaves:
  out of CV Out 2 at 1V per octave (MIDI note 60 is 0V). Pulse Out 2 fires
  when the bit under the playhead is 1, so the rhythm locks and changes
  with the notes. As the playhead lands, that bit may flip: half the time
  at Main's centre (every step random), never in the last 160 counts of
  either end (locked). A new key re-pitches the same pattern from the next
  step; the machine keeps running through a reseed, since it's for the
  rest of the system rather than the card's own voices. With no clock it
  holds its note. Each power-on starts it with a different pattern.
- **Keys.** Equanimity's six scales: Major, Minor, Major and Minor
  Pentatonic, Dorian and Lydian. All have a perfect fifth, so the drone
  always belongs. Notes a semitone above a drone note (only Minor has one,
  its flat 6th) come up a third as often, never on a high-tide swell, and
  never end a lead phrase.
- **The Icarus loop.** A port of the feedback loop in Icarus's
  `Engine_Icarus.sc`. It runs: read the delay line, then three allpasses
  for diffusion (Constancy's stand-in for the Blade Runner hall), then
  LeakDC, then add the new sound, drive and soft-clip, then a four-pole
  ladder lowpass, then the dropouts. That output is fed back through
  `OnePole(0.4)` and `OnePole(-0.08)` and a 36° stereo rotation every lap.
  The delay time follows the sun: 60ms before dawn, 250ms at sunrise,
  500ms by morning. It is lagged by 0.2s as Icarus lags it, so moving Time
  bends the pitch of what's in the loop. The ladder opens with the sun too
  (1kHz to 9kHz).
- **Destruction.** Icarus's random dropouts: below a quarter-second delay
  the loop ducks to half for 0.2s at random moments. They come up to three
  times a second at the shortest delay: the fragile time before dawn.
- **The sunrise swoop.** As the sun crosses the horizon, the delay
  stretches by 150ms and comes back, once. It's like holding Icarus's time
  key: the whole loop bends down in pitch and back up. It re-arms only
  after Time has gone well back into the dark.
- **The tide.** Its height sets every envelope. Pluck: 23ms attack, 450ms
  release. Swell: 2.4s attack, 5s release. How fast it's running nudges
  the loop's feedback up by as much as 0.08; at the turn of the tide,
  nothing.

Every tuning value is a named constant in [`src/Config.h`](src/Config.h),
so it can be adjusted by ear.

### Fitting on the chip

The RP2040 runs at 192MHz (an officially supported setting, at 1.15V), as
Equanimity does. The worst case measured on the emulator is **80%** of the
time available per sample, with every voice sounding, Sun burning, Water
closing and audio coming in. RAM use is 46%.

Two choices got it there, both measured:

- **The loop runs at half rate,** 24kHz. The left side runs on even
  samples and the right on odd ones, so every sample does half the loop's
  work, and the delay line needs half the memory (65KB for 0.67s of
  stereo). The cost is the loop's top octave above ~11kHz, a little grit
  that the ladder mostly hides. The whole loop at 48kHz measured too heavy
  to leave room for the voices.
- **Water runs only the filter that's closing.** The ladder's input clamp
  stands in for a tanh, because the soft clipper just before it already
  does the overdrive. The gain ramps step every other sample.

The card uses no floating point at all. The lookup tables are generated
on a computer (`tools/tables/make_tables.py`), and core 1 works in
integer `Log2`/`Exp2`.

## Building

You need the Pico SDK (2.1.1) and the ARM GNU toolchain.

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

That gives `build/Constancy.uf2`.

### Tests and renders

The whole instrument also builds on a computer, driven by a pretend panel.

```bash
make -C test          # host tests
make -C test render   # WAVs in test/out/, to hear before flashing
```

The renders are:

- `morning`: the whole window in 90 seconds.
- `sun`: X from left to right.
- `water`: Y through both sides.
- `loops`: the pad at dawn, at low water with a new key, and at the end of the window.
- `sunrise`: three minutes across the horizon.
- `levels`: the Levels page (melody and drone levels).

### Timing check

`ProcessSample()` must finish in ~20µs, every sample. `tools/bench/cycles.py`
(from Equanimity) runs the compiled firmware on an emulated Cortex-M0+ and
reports the cycle count per sample across a scripted performance.

```bash
python3 -m venv venv && venv/bin/pip install unicorn capstone
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DCONSTANCY_BENCH=ON
cmake --build build-bench
venv/bin/python tools/bench/cycles.py build-bench/Constancy.elf
```

### The data

`tools/data/build_data.py` computes the sun and smooths the tide, wind and
cloud into `src/DayData.h`. The Open-Meteo responses it used are saved in
`tools/data/raw/`, so it rebuilds offline. Run it with `--fetch` to
download them again.

## Status

0.3.2, released. Built from the design brief (`constancy-brief.md`) in ten
milestones, tested on a computer and measured on the emulator at every
step, and since 0.1 played on hardware and tuned by ear.

Along the way: the drone went down an octave and came back up (octave 1
was too low), and a clocked four-note bass line was tried and taken out
again, so the drone holds root and fifth. CV In 2 moved from Sun to Water,
and Water no longer clicks crossing its centre. A new key now stops every
melodic note until the drone has glided to the new root. In 0.3 a
scale-aware Turing machine took over CV Out 2 and Pulse Out 2 (they were
a light wander and a sun-up gate) and Main on the Levels page (it was the
delay mix, now fixed). Since then: the melody's loudness evened out across
the morning, and the power-on levels set to melody 80%, drone 40%.

## Credits and licence

By Matt Allison. MIT licence (see `LICENSE`).

The firmware was written with Claude (Anthropic), working from Matt's
design brief. It reuses the structure, two-core handoff, pickup, random,
smooth-random and Pulse In code of his Equanimity and Impermanence cards,
and Equanimity's cycle-counting bench.

The feedback loop, oscillator drift and envelope are ported from
[Icarus](https://github.com/schollz/icarus) by Zack Scholl (schollz), MIT
licence, copyright 2021 Zack (`lib/Icarus.LICENSE`).

Weather and tide data by [Open-Meteo.com](https://open-meteo.com/) (CC BY
4.0), from the Deutscher Wetterdienst (DWD) and other national weather
services. The tide comes from the
[Open-Meteo Marine API](https://open-meteo.com/en/docs/marine-weather-api)
(`sea_level_height_msl`) at its nearest sea point, about 15km south of
Calamansac; wind and cloud come from the historical weather archive. Sun
positions use NOAA's solar equations.

The Turing machine follows the idea of Tom Whitwell's
[Music Thing Modular Turing Machine](https://www.musicthing.co.uk/Turing-Machine/)
and his Workshop Computer card of it; the code is Constancy's own.

[ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard)
v0.4.0 by Chris Johnson (MIT, `lib/ComputerCard.LICENSE`).
