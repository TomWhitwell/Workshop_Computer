# Abeyance

A generative ambient program card for the [Music Thing Modular Workshop
Computer](https://www.musicthing.co.uk/workshopsystem/), inspired by Brian
Eno's *Music for Airports*.

Abeyance plays on its own from power-up: 13 loops of piano and choir, each
a different length, drifting in and out of step. On Eno's record, tape loops
of different lengths drift in and out of phase, and the pattern between them
keeps changing. Here it's six piano phrases and seven sung choir notes, on
loops of 17.8 to 41.3 seconds, panned across a long stereo reverb.

It joins Impermanence, Constancy and Equanimity.

## Quick start

Flash `UF2/abeyance.1.0.0.uf2` and press reset. Within a second the
original piece begins: the loops come in one by one over the first 23
seconds, in D-flat. Listen to **Audio Out 1 and 2**, which are stereo:
each loop has its own place, the bass in the middle and the high notes far
out. Tap the switch down whenever you want a **new piece**.

## Controls

| Z switch | Main | X | Y |
| --- | --- | --- | --- |
| **Up** | Master volume | Drift (+ CV In 2) | Tape (macro) |
| **Middle** | Reverb (macro, + CV In 1) | Piano level | Choir level |
| **Down** (momentary) | New piece | (no change) | (no change) |

- **Soft takeover.** Each setting keeps its own value. After the switch
  moves, a knob does nothing until it passes its stored value, then takes
  over. Its LED row pulses slowly while it waits.
- **Start-up values.** Volume 75%, reverb mix 65%, drift 30%, tape 40%,
  piano and choir 80%. These hold, wherever the knobs are, until each knob
  takes over.
- **Curves.** Volume, piano and choir levels are squared, for finer control
  at low levels. Drift, tape and reverb mix are linear.
- **Reverb (Main, switch middle)** is a macro. Up to two-thirds of the way
  round it's a plain wet/dry mix (an equal-power crossfade) into the usual
  plate, which takes about 8 seconds to die away. Over the last third the
  plate itself grows. Its tail lengthens to about 18 seconds and darkens
  (its damping falls from about 3.5kHz to 2kHz), so fully wet is a slow,
  dark wash. Its level comes down 2.5dB as it grows, which keeps the
  longer wash as loud as the usual one. CV In 1 adds to the knob, so it
  drives the macro too. The settings are `kReverbMacroStart` and
  `kReverbMax...` in [`src/Config.h`](src/Config.h).
- **Smoothing.** Every setting glides to a new value over about 50ms, so
  there's no zipper noise.
- **New piece.** A tap down fires the moment the switch goes down, with a
  30ms debounce. Holding it does nothing more. Pulse In 1 does the same.
  The LEDs sweep at once; everything then fades to silence over 15 seconds
  before the new piece starts (see below).

There's no layer switch: turn a level to zero to hear the other layer alone.
At zero, nothing of that layer reaches the echo or the reverb either.

### What a new piece changes

- **Root:** one of 12, from A3 to A-flat 4, so every piece sits near the
  opening register.
- **Scale:** one of nine, never the one just playing (see below).
- **Tempo:** 0.7x to 1.4x. It sets the loop lengths, the piano phrases'
  pace, how long the choir sings each note, the gate length and how fast the
  random CVs wander.
- **Loops:** each is re-cut to its original length divided by the tempo,
  times a random 0.88 to 1.12, and comes back in at a random point. One
  loop plays at once.
- **Pan:** each loop's place is worked out again from its new pitch, and it
  glides there over 0.6s.

First the old piece fades out, like a record fading out at the end of a
side. It plays on underneath while everything you hear (voices, echo,
reverb, hiss, crackle and audio inputs) comes down together over 15
seconds, about as long as the reverb's longest tail. It falls with the
square of the time left, so it slips away gently rather than stopping:
about 12dB down halfway through. With the output silent, the
voices stop and the echo and reverb are emptied (27ms), so nothing of the
old piece is left. Then the new piece begins in a clean room, one loop
straight away, with the output coming back up over 250ms. A second tap
during the fade changes nothing more. The timings are
`kNewPieceFadeBlocks`, `kFlushBlocks` and `kFadeInBlocks` in
[`src/Config.h`](src/Config.h).

### Drift and tape

**Drift** changes each pass of each loop as it starts. At full drift:

- a 30% chance the pass is skipped;
- a 20% chance it jumps an octave, up or down, whichever stays inside the
  layer's range;
- a piano phrase of two or three notes has a 35% chance of playing
  backwards, and 35% of losing its last note.

Lower drift scales every chance down in proportion. At zero the loops play
exactly as written.

**Tape** is a macro: the noise of the medium the music is on, from a quiet
tape to a worn-out record. All of it is added after the reverb (it's on
the tape, not in the room), and the master volume turns it down too.

- **All the way up:** every voice's pitch bends together with a slow
  0.23Hz wow (up to 11 cents) and a 5.7Hz flutter (up to 2.2 cents). Hiss
  rises with the square of the setting. The hiss is never quite still: its
  level drifts a few dB either way and its brightness wanders, each on a
  slow random glide (a new target every 1 to 4 seconds).
- **From 50%:** the hiss darkens and narrows. A tape's bright hiss (a band
  around 3.5 to 8.5kHz) becomes the duller, filtered surface noise of a
  record (about 0.9 to 3.2kHz) at full.
- **From 60%:** a worn record's crackle fades in. Fine ticks, and now and
  then a soft pop, each at its own place in the stereo field, growing with
  the square of how far past 60% Tape is. At full there are about 25 ticks
  and one pop a second. Most are small, and the biggest peak around -24dBFS
  (ticks) and -20dBFS (pops) at the start-up volume, under the music.
  Ticks are clicks through a low-pass at about 5kHz; pops are short bursts
  through one at about 1kHz, so they thump softly rather than snap.

At the start-up 40% there's only a little hiss (about -58dBFS) and gentle
wow. The settings are at the top of [`src/Surface.h`](src/Surface.h).

## Jacks

| Jack | Job |
| --- | --- |
| Audio In 1 + 2 | Into the reverb, halved and summed, so it comes out centred. Heard only through the reverb, so it follows the reverb mix: at zero mix the inputs are silent |
| CV In 1 | Reverb mix: 0 to +5V adds 0 to 100% to Main's setting, clamped at full |
| CV In 2 | Drift: -2.5 to +2.5V moves it 50% down or up from X's setting |
| Pulse In 1 | New piece (rising edge) |
| Pulse In 2 | Unassigned |
| Audio Out 1, 2 | Left and right of the stereo mix |
| CV Out 1 | Smooth random voltage, 0 to +5V |
| CV Out 2 | Smooth random voltage, -5 to +5V, centred on 0V, independent of CV Out 1 |
| Pulse Out 1 | A 10ms trigger on every piano note (each note of a phrase). Two notes within 15ms share one trigger, so the pulse always drops low in between |
| Pulse Out 2 | A gate: high from each piano note until 1.5s (/ tempo) after the last |

The choir never touches the pulse outs. Its notes sustain for seconds and
would hold the gate high most of the time.

The **random CVs** each pick a new target every 3 to 12 seconds (divided by
the tempo) and curve towards it through two smoothing stages. Targets stay
inside 5% to 95% of the range, so the voltage never runs into a limit. Each
has its own random generator, so the two never move together. They use
ComputerCard's calibrated millivolt outputs, so on a calibrated Computer
0 to 5V means 0 to 5V.

Unpatched inputs add nothing. Pulse In 1 edges count after 20ms, and only
if the cable is still in, so pulling a cable out never starts a new piece.

## LEDs

```
0 1   piano: a strike lights its side, then fades over about 3s
2 3   choir: brightness follows each note's swell and fade
4 5   CV Out 1, and CV Out 2's distance from 0V
```

Notes light the side their loop is panned to. A centred loop, such as the
bass piano loop, lights both LEDs of its pair at half brightness.

Some moments take the LEDs over:

- **Z moved:** the new mode's row lights for 0.8s (top for up, middle for
  middle).
- **A knob waiting to take over:** its mode's row pulses slowly.
- **New piece:** all six sweep top to bottom, about 0.5s.

## The music

Each loop stores scale steps, not notes, and that's all the card keeps.
The opening piece is in D-flat major. These are its 13 loops, low to high:

| Loop | Notes | Steps | Length | Pan |
| --- | --- | --- | --- | --- |
| Piano | D-flat 3, A-flat 3 | low 1, low 5 | 41.3s | centre |
| Choir | A-flat 3 | low 5 | 17.8s | L 14 |
| Choir | C4 | low 7 | 20.1s | R 24 |
| Choir | D-flat 4 | 1 | 31.8s | L 26 |
| Choir | E-flat 4 | 2 | 24.6s | R 31 |
| Choir | F4 | 3 | 19.6s | L 37 |
| Piano | E-flat 4, A-flat 4 | 2, 5 | 29.4s | R 38 |
| Piano | A-flat 4 | 5 | 26.1s | L 45 |
| Choir | A-flat 4 | 5 | 21.1s | R 45 |
| Choir | C5 | 7 | 23.6s | L 56 |
| Piano | C5, E-flat 5 | 7, high 2 | 22.3s | R 60 |
| Piano | F5, A-flat 5, E-flat 5 | high 3, high 5, high 2 | 31.7s | L 71 |
| Piano | C6 | high 7 | 37.9s | R 90 |

Every loop uses the same five notes: D-flat, E-flat, F, A-flat and C
(steps 1, 2, 3, 5 and 7). There's no 4th and no 6th, so nothing pulls
towards a cadence, and the piece has no firm tonal centre: D-flat and
A-flat take turns on top as the loops drift.

The choir follows Eno's 2/1: its C at about 20.1s, high F at about 19.6s
and D-flat as the longest loop come from [Reverb Machine's breakdown of the
track](https://reverbmachine.com/blog/deconstructing-brian-eno-music-for-airports/). The other choir pitches and lengths are close approximations. The
piano phrases are new, written in D-flat so both layers share the same five
notes. (Eno's piano track, 1/1, is mostly in D.)

**The nine scales**, and the semitone rub each gives these loops. In the
opening piece the rub is C against D-flat, and it comes and goes as those
two loops drift:

| Scale | Rub |
| --- | --- |
| Major | 7 against 1 |
| Lydian | 7 against 1 (identical to major here: no loop uses the 4th) |
| Dorian | 2 against flat 3 |
| Minor | 2 against flat 3 |
| Hirajoshi | 5 against flat 6 |
| Mixolydian | none |
| Major pentatonic | none |
| Minor pentatonic | none |
| Suspended pentatonic | none |

In the five-note scales each step is scaled by 5/7 and rounded. That keeps
each phrase's span, though two steps can land on the same note. Across
every possible piece, octave jumps included, the piano covers A2 to G6 and
the choir E3 to A5.

**Pan by pitch.** The lowest loop sits in the centre and higher loops sit
further out, alternating sides. The distance is 0.9 x ((pitch - lowest
note) / (highest - lowest))^1.15, a curve fitted to the build guide's
table.

## How it works

The engine is split across the RP2040's two cores. **Core 1** renders the
20 voices, 32 samples (0.67ms) at a time. Once per block it also runs
everything control-rate: knobs, scheduler, drift, LEDs, random CVs, pulse
outs, and the tape's hiss and crackle. **Core 0** is ComputerCard's 48kHz
audio interrupt. It plays the blocks back a sample at a time, adds the
piano echo, the reverb, the audio inputs and the noise, and drives every
jack. Blocks pass between the cores
through a four-block queue, each carrying its own settings, so the mix and
the jacks change exactly in step with the audio.

- **Scheduler.** Each loop holds its length, its next start time, its pan
  and its notes. Time is counted in samples as a 64-bit number, so it never
  wraps (a 32-bit count would after about 25 hours). Everything on core 1
  is whole-number maths.
- **Piano.** 12 voices, each three sines: the fundamental, a half-level
  copy 3 cents sharp (the pair beat slowly, like a piano's slightly
  mistuned strings), and the octave, which dies away 2.5 times faster.
  Decay times come from a table by pitch: 16s at A2 down to 3s at G6. When
  all 12 voices are busy, the quietest is taken.
- **Choir.** The web sketch's voice, in wavetables. 8 voices, each three
  oscillators, like three singers on one note: 8 cents flat, in tune and 7
  cents sharp, each a random 2 cents further out. They read single-cycle
  wavetables with the vowel built in. There are ten tables: "ah" and "oo",
  each centred on G3, D-flat 4, G4, D-flat 5 and G5. They're built in RAM at
  power-up (about 20ms) by adding harmonics:
  - each harmonic is weighted like a sawtooth;
  - each is then weighted by the web sketch's filters: three narrow
    band-passes at the vowel's resonances ("ah" 730, 1090 and 2440Hz;
    "oo" 320, 870 and 2250Hz; Q 8, 10 and 12), plus a low-pass at 2.2 times
    the note for the body. The card works out the browser's own filter
    responses and adds them with their phases, so it gets the same peaks
    and the same dips between them. The dips are what make it a vowel
    rather than a filtered saw;
  - harmonics stop below 16kHz at each table's top note.

  A note picks its nearest table at note-on. Each note swells straight up
  over 2.4 to 3.4s and is held until 4.5s before its end, 9 to 12s after it
  began. Then it fades, falling by e every 1.1s. All of these are divided
  by the square root of the tempo, as in the web sketch. Vibrato at 4.6 to
  5.2Hz grows from nothing to ±5 cents by 1.5s after the swell.
- **Piano echo.** 1.15s, stereo, with feedback, darkened at 2.2kHz, at
  16kHz to save memory (74KB). Each repeat comes back first on its own side,
  then crosses.
- **Reverb.** A Dattorro plate at 24kHz with a modulated tank (82KB),
  adapted from Temperance's and made 2.3 times bigger. Its tail falls 60dB
  in about 8 seconds in the mids, or 18 at the end of Main's macro; core 1
  sends its decay, damping and level with each block. Every multiply inside the tank rounds
  towards zero, so the tail always dies to true silence.
- **Output.** A gentle soft clip instead of a compressor. Measured over
  long runs, the mix peaks around -6dBFS at the start-up volume.

Every tuning value is a named constant: timings and levels in
[`src/Config.h`](src/Config.h), voice settings at the top of
[`src/Piano.h`](src/Piano.h) and [`src/Choir.h`](src/Choir.h), and drift's
chances in [`src/Scheduler.h`](src/Scheduler.h).

The RP2040 runs at **192MHz** (officially supported, at 1.15V). I measured
CPU with all 20 voices running, as the build guide's stage 5 asks. At the
usual 144MHz core 1 needed up to 83% of each block, too little margin. At
192MHz the worst cases are 74% of a block on core 1 (every voice sounding,
new notes starting and the crackle at full, all at once) and 58% of a
sample on core 0. RAM use is 245KB of 256KB.

## Building

You need the Pico SDK (2.1.1) and the ARM GNU toolchain (14.2).

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

That gives `build/Abeyance.uf2`.

### Tests and renders

The whole instrument also builds on a computer, driven by a pretend panel.

```bash
make -C test          # host tests: the build guide's checklist, as far as a computer can check it
make -C test render   # WAVs in test/out/, to hear before flashing
```

The renders are:

- `opening.wav`: the first 3 minutes after power-up;
- `new_piece.wav`: 10 seconds of the opening, a tap (the fade-out), then 2
  minutes of the new piece;
- `choir.wav`: 2 minutes of the choir alone;
- `drift_full.wav`: 2 minutes with drift and tape at full (the worn record);
- `wash.wav`: 2 minutes with Main fully wet (the long, dark reverb);
- `input.wav`: a plucked tone into the audio inputs.

### Timing check

`ProcessSample()` must finish in ~20µs, every sample, and core 1 must keep
up with the blocks. `tools/bench/cycles.py` runs the compiled firmware on
an emulated Cortex-M0+. It reports core 0's cycles per sample and core 1's
cycles per block across a scripted performance, including every voice
sounding at once.

```bash
python3 -m venv venv && venv/bin/pip install unicorn capstone
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DABEYANCE_BENCH=ON
cmake --build build-bench
venv/bin/python tools/bench/cycles.py build-bench/Abeyance.elf 1.0 4000
```

## Status

1.0.0, released. Every revision has been checked on hardware and tuned by
ear. Each also builds cleanly, passes the host tests, has run an hour at
full drift and full tape with no dropouts or clipping, and is measured on
the emulator.

The build guide's six stages each have a UF2 in `UF2/milestones/`, to step
through the build on the module in order. They were built for 0.1.0, so
stages 4 to 6 have the old two-oscillator choir and no fade before a new
piece:

1. panel and LEDs, with no sound;
2. scheduler, with sine blips;
3. piano;
4. choir;
5. reverb, echo and audio in;
6. drift and tape.

## Credits and licence

By Matt Allison. MIT licence (see `LICENSE`).

The firmware was written with Claude (Anthropic), working from Matt's build
guide. It reuses code from his own cards:

- from Equanimity and Impermanence: soft takeover, random, smooth random
  and the two-core structure;
- from Temperance: the plate reverb.

The Eno loop timings (the choir's C at about 20.1s, its high F at about
19.6s, and D-flat as the longest loop) come from Reverb Machine's
[Deconstructing Brian Eno's Music for Airports](https://reverbmachine.com/blog/deconstructing-brian-eno-music-for-airports/).

[ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard)
v0.4.0 by Chris Johnson (MIT, `lib/ComputerCard.LICENSE`).
