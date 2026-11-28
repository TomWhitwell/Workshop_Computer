# Abeyance

Nov 27, 2026 · @Matt

## Overview

Abeyance plays on its own from power-up: 13 loops of piano and choir drifting in and out of step, panned across a stereo reverb. It's inspired by Brian Eno's Music for Airports, where tape loops of different lengths drift in and out of phase and the pattern between them keeps changing.

**How it plays**

- The 13 loops, written as scale degrees: 6 piano phrases and 7 choir notes, with the original lengths of 17.8 to 41.3 s.
- New piece logic: random root, one of 9 scales, tempo 0.7× to 1.4×, every loop re-cut to a fresh length.
- Drift: skipped passes, octave jumps, reversed or shortened piano phrases.
- Tape: slow wow, a little flutter and hiss.
- Per-loop stereo spot by pitch: bass in the centre, higher loops further out, alternating sides.
- The dark piano echo and the long stereo reverb.

**Built for the hardware**

- The choir is a wavetable voice with sung vowel shapes built in, so it needs no live filters.
- The piano is a two-partial sine voice with lookup-table decays.
- There's no layer switch. Turn a level to zero to hear the other layer alone.
- The LEDs show notes and random voltages as they happen (see LEDs).

**New on the card**

- Two smooth random CVs for patching the rest of the system.
- A trigger per note and a gate while notes sound.
- External audio into the reverb, so the room can hold other voices too.

The card is named Abeyance, joining Impermanence, Constancy and Equanimity.

## Pitch, key and the rub

The opening piece has no real tonal centre. Its notes all fit D♭ major, so D♭ is the root the card uses to move them, but nothing in the music insists on it.

**Tonal centre, or the lack of one**

- Eno never gave 2/1, the choir track behind the opening piece, a key. Breakdowns of the track describe it as shapeless, with the voices only sometimes forming chords ([Reverb Machine](https://reverbmachine.com/blog/deconstructing-brian-eno-music-for-airports/)).
- Every loop in the opening piece uses the same five notes: D♭, E♭, F, A♭ and C. As scale steps, that's 1, 2, 3, 5 and 7. There's no 4th and no 6th, so nothing pulls towards a cadence.
- Two notes compete for home. D♭ is the root and the longest choir loop. A♭ is the lowest choir note and the piano's bass. As the loops drift, first one then the other sits on top. You could fairly hear the piece in D♭ major, A♭ or F minor.
- D♭ is a working label, needed to turn notes into scale steps so New piece can move them.

**The 13 loops in the opening piece** (low to high)

| Loop | Notes | Scale steps | Length | Pan |
| --- | --- | --- | --- | --- |
| Piano | D♭3, A♭3 | low 1, low 5 | 41.3 s | C |
| Choir | A♭3 | low 5 | 17.8 s | L 11 |
| Choir | C4 | low 7 | 20.1 s | R 23 |
| Choir | D♭4 | 1 | 31.8 s | L 25 |
| Choir | E♭4 | 2 | 24.6 s | R 31 |
| Choir | F4 | 3 | 19.6 s | L 37 |
| Piano | E♭4, A♭4 | 2, 5 | 29.4 s | R 38 |
| Piano | A♭4 | 5 | 26.1 s | L 45 |
| Choir | A♭4 | 5 | 21.1 s | R 45 |
| Choir | C5 | 7 | 23.6 s | L 56 |
| Piano | C5, E♭5 | 7, high 2 | 22.3 s | R 60 |
| Piano | F5, A♭5, E♭5 | high 3, high 5, high 2 | 31.7 s | L 71 |
| Piano | C6 | high 7 | 37.9 s | R 90 |

The choir's C at about 20.1 s, its high F at about 19.6 s and D♭ as the longest loop follow the Reverb Machine breakdown. The other choir pitches and lengths are close approximations of 2/1. The piano phrases are not Eno's: his piano track, 1/1, is mostly in D major. They're written in D♭ here so the two layers share the same five notes.

**How the shifts work**

1. Each loop stores scale steps, not notes. The table's steps are all the card keeps.
2. New piece picks a root from 12 keys, A3 to G♭4, so every piece sits near the opening register. It picks one of 9 scales, never the one just playing, and a tempo from 0.7× to 1.4×.
3. Steps become notes in the new scale. For the four five-note scales, the three pentatonics and Hirajoshi, each step is scaled by 5/7 and rounded. That keeps each phrase's span, though two steps can land on the same note.
4. Loop lengths become the original length divided by tempo, times a random 0.88 to 1.12.
5. Pitches change, so each loop's stereo position is worked out again and glides there.

Across every possible piece, drift's octave jumps included, the piano covers A2 to G6 and the choir E3 to A5.

**The tonal rub**

The opening piece has one semitone clash: C against D♭, the 7th against the root. It sits between two choir loops, C4 every 20.1 s and D♭4 every 31.8 s, and the high Cs in both layers. Because those lengths don't divide into each other, the rub comes and goes as the loops drift. The same clash is audible in Eno's original whenever its C and D♭ loops overlap.

The rub moves, or vanishes, with the scale:

| Scale | Rub |
| --- | --- |
| Major | 7 against 1 (C against D♭ in the opening piece) |
| Lydian | 7 against 1, the same as major |
| Dorian | 2 against ♭3 |
| Minor | 2 against ♭3 |
| Hirajoshi | 5 against ♭6 |
| Mixolydian | None |
| Major pentatonic | None |
| Minor pentatonic | None |
| Suspended pentatonic | None |

So five scales keep a rub and four are clash-free. Lydian only differs from major in its 4th, which these loops never use, so the two sound identical here.

## Panel map

The Z switch picks which pair of settings X and Y control, and Main changes with it. Six settings share three knobs across two modes.

| Z position | Main | X | Y |
| --- | --- | --- | --- |
| Up | Master volume | Drift | Tape |
| Middle | Reverb mix | Piano level | Choir level |
| Down, momentary | New piece: new root, scale, tempo and loop lengths | (no change) | (no change) |

**Knob behaviour**

- **Soft takeover.** Each mode keeps its own stored values. After a switch change, a knob does nothing until it passes its stored value, then it takes over. That stops drift jumping to the piano level's position when you flip Z.
- **Curves.** Volume, piano and choir levels use a squared curve, for finer control at low levels. Drift, tape and reverb mix are linear.
- **Start-up values.** Volume 75%, reverb 65%, drift 30%, tape 40%, piano and choir 80%.
- **Down tap.** Fires on press, with a 30 ms debounce. Holding it does nothing more, so a long press can't fire twice.

**Smoothing**

All six controls glide to new values over about 50 ms. That kills zipper noise from the 12-bit knob reads.

## Jacks

Every jack is assigned except Pulse in 2. CV inputs add to their knob, so the knob sets the centre and the CV moves around it.

| Jack | Function | Range | Notes |
| --- | --- | --- | --- |
| Audio in 1 + 2 | Into the reverb, centred | Audio level | Summed to mono, then fed to both sides of the reverb. Level follows the reverb mix |
| Audio out 1 | Left of the stereo mix | Audio level | Synth plus reverb plus any input reverb |
| Audio out 2 | Right of the stereo mix | Audio level | As above |
| CV in 1 | Reverb mix | 0 to +5 V | Adds to Main's middle-mode value, clamped at full |
| CV in 2 | Drift | −2.5 to +2.5 V | Adds to X's up-mode value, clamped at 0 and full |
| CV out 1 | Smooth random | 0 to +5 V | Unipolar |
| CV out 2 | Smooth random | −2.5 to +2.5 V | Bipolar, independent of CV out 1 |
| Pulse in 1 | New piece | Rising edge | Same action as a Z down tap |
| Pulse in 2 | Unassigned |  | Left open |
| Pulse out 1 | Trigger on every piano note | 10 ms pulse | Choir notes don't trigger it |
| Pulse out 2 | Gate while a piano note is held | High while held | Choir notes don't affect it |

**CV scaling**

ComputerCard reads and writes CV as −2048 to +2047 for roughly −6 to +6 V, so 1 V is about 341 counts.

- CV in 1: 0 to 5 V is 0 to about 1706 counts, mapped to 0 to 100% added mix.
- CV in 2: ±2.5 V is about ±853 counts, mapped to ±50% drift. Full positive from a centred knob takes drift from 30% to 80%.
- CV out 1: 0 to about 1706 counts. CV out 2: about −853 to +853.

The outputs are not precision-calibrated. Measure one unit with a meter and adjust the scale constant if 5 V matters.

**Random CV generators**

Each output has its own random number generator, so they never move together.

- Every few seconds the generator picks a new random target.
- A two-stage smoothing filter glides towards it, so the voltage curves rather than ramps.
- The time between targets is itself random, 3 to 12 s, scaled by the piece's tempo. Faster pieces wander faster.
- The output never reaches the limits in a hard line. Targets stay inside 5% to 95% of range.

**Pulse logic**

- **Pulse out 1** fires 10 ms on each piano note start. Choir notes never fire it. A piano phrase of three notes gives three triggers. Two notes inside 15 ms merge into one trigger, so the pulse always drops low in between.
- **Pulse out 2** goes high when a piano note starts and low 1.5 s later, scaled by tempo. Overlapping piano notes keep it high until the last one's 1.5 s is up. The choir is ignored: its notes sustain for seconds and would hold the gate high most of the time.

## Engine

The engine splits across the RP2040's two cores. Core 1 renders the voices. Core 0 runs the reverb and every jack at 48 kHz.

&#91;embedded content: signal flow · two cores, one reverb\]

Notes and voices fill blocks on core 1. Core 0 adds the room, external audio and hiss, then sends the stereo pair out.

**Scheduler**

- Runs once per audio block. Each loop holds its length, next start time, pan and scale degrees.
- Counts time in samples as a 64-bit number. A 32-bit counter would wrap after about 25 hours.
- Degrees become notes like this: scale the degree by notes-per-octave over 7, round, add octaves and the root.
- A small xorshift random generator runs everything random. The original piece always plays first after power-up; the generator is seeded from knob-read noise for every new piece after that.

**Voices**

- **Piano:** 12-voice pool. Three sines per note: the fundamental, a copy 3 cents sharp, and the octave. One shared 2048-point sine table, 32-bit phase counters. Decay times come from a table by pitch. When the pool is full, the quietest voice is taken.
- **Choir:** 8-voice pool, one per loop plus a spare for drift's octave jumps. Two oscillators per note, about 7 cents apart, playing single-cycle wavetables. The tables are built in RAM at power-up from "ah" and "oo" vowel sounds (see Choir wavetables). Swell, sustain, release and late vibrato follow set timings, scaled by tempo.

**Choir wavetables**

Decision: build them in RAM at power-up by adding sine waves together. Nothing is generated offline, and nothing is read from flash while the card plays.

- **Why this way.** RAM reads never miss the RP2040's 16 KB flash cache, so eight voices can't cause clicks. Changing a vowel is a number change in the card's code. There's no Python step or generated header to keep in step. Copying a header into RAM would play just as fast but adds that build step.
- **The set.** 2 vowels × 5 tables, one every half octave, centred on G3, D♭4, G4, D♭5 and G5. That covers the choir's whole range in every possible piece, E3 to A5, drift's octave jumps included.
- **Size.** 1024 16-bit points per table: 2 KB each, 20 KB in all.
- **Recipe.** Each table sums the harmonics of its centre note. Each harmonic is weighted like a sawtooth, then shaped by the vowel's three resonances: ah at 730, 1090 and 2440 Hz, oo at 320, 870 and 2250 Hz. A little low body is added under them. Harmonics stop below 16 kHz at the table's highest note, so nothing folds back into the audio: 68 harmonics in the lowest table, 17 in the highest.
- **Boot cost.** The weights are about 500 numbers, worked out once with decimal maths. The sums read a single integer sine table, so the heavy loop is whole-number multiply-adds: about 390,000 of them, roughly 20 ms at boot.
- **Level.** Each table is scaled to a peak of 30,000. Peaks are only 1.4 to 2.2 times the average level, so the tables sound equally loud and the voice level does the rest.
- **Playback.** A note picks its table once, at note-on: the nearest centre, in the vowel chosen for that note. The vowel's resonances sit at most 3 semitones from where they were rendered, which a choir hides well. There's no crossfade, so each oscillator makes one table read per sample, with linear interpolation between neighbouring points.

**Mix and effects**

- **Pan:** equal-power left and right gains per loop, worked out at each new piece. They glide to new positions over 0.6 s.
- **Piano echo:** 1.15 s stereo delay with feedback, darkened at 2.2 kHz. It runs at 16 kHz to save memory. With that low cut, the lower rate costs nothing you'll hear.
- **Reverb:** a fixed-point stereo plate running at 24 kHz, with linear interpolation back up to 48 kHz. Audio in 1 + 2 is halved, summed and added to its input.
- **Tape:** a 0.23 Hz wow and a 5.7 Hz flutter bend every voice's pitch, up to 11 and 2.2 cents. Hiss is filtered noise around 5.5 kHz, rising with the square of Tape.
- **Output:** a gentle soft clip in place of a compressor. It's cheaper and keeps peaks inside the 12-bit outputs.

**Memory and CPU budget (estimates)**

| Item | RAM |
| --- | --- |
| Reverb delay lines at 24 kHz | about 86 KB |
| Piano echo, stereo at 16 kHz | about 74 KB |
| Sine table, choir wavetables (20 KB), voice state, buffers | about 28 KB |
| **Total of 264 KB** | **about 188 KB** |

CPU is the unknown. Measure it in build stage 5 with all 20 voices running. If it's short, in this order: run the RP2040 at 200 MHz, cut piano voices to 8, drop the choir to one oscillator per note, run the reverb at 16 kHz.

## LEDs

The six LEDs show the music as it plays: notes flash on the side they're panned to.

| LEDs | Shows |
| --- | --- |
| Top pair | Piano notes. A strike lights the left or right LED by the loop's pan, then fades over about 3 s |
| Middle pair | Choir notes. Brightness follows each note's swell and fade |
| Bottom pair | CV out 1 on the left, CV out 2 on the right. Brightness follows the voltage; CV out 2 shows its distance from 0 V |

Centred loops, such as the bass piano loop, light both LEDs of their pair at half brightness.

**Moments that take over the LEDs**

- **Z switch moved:** the row for the new mode lights for 0.8 s, top for up and middle for middle.
- **Knob waiting to take over:** its row pulses slowly until the knob passes its stored value.
- **New piece:** all six sweep top to bottom once, about 0.5 s.

## Build plan

Build it in six stages, each one testable on the module before the next starts. Stage 5 is the risky one: measure CPU there before adding polish.

1. **Panel and I/O skeleton.** Z modes, soft takeover, smoothing, LEDs, both random CVs, Pulse in 1 and the down tap. No sound yet.
2. **Scheduler.** The 13 loops, scale degrees to notes, the 9 scales, tempo, new piece. Test with plain sine blips. Wire both pulse outs here.
3. **Piano voice.** Sine partials, decay tables, per-loop pan, piano level, master volume.
4. **Choir voice.** Build the 10 wavetables at power-up from the vowel settings in Choir wavetables. Add the swell envelope, vibrato and choir level.
5. **Reverb, echo and audio in.** The plate, the piano echo, the input send, the reverb mix and CV in 1. Measure CPU with all voices running.
6. **Character.** Drift and CV in 2, tape wow and flutter, hiss. Then tune levels by ear.

## Decisions

- **Main in the up position:** master volume.
- **CV in 2:** bipolar, −2.5 to +2.5 V, moving drift up or down from the X knob's setting.
- **Input level:** follows the reverb mix. At zero mix the inputs are silent.
- **Remember the piece:** no. Abeyance always powers up on the original piece in D♭ major, and nothing is written to flash.

## Test checklist

- [ ] Powers up playing the original piece in D♭ major within 2 s, with no button press
- [ ] Each Z mode moves only its own three settings; flipping Z causes no jumps
- [ ] Down tap and Pulse in 1 both give a new piece; a long hold fires once
- [ ] Piano and choir levels at zero silence their layer fully, echo and reverb tails included
- [ ] CV in 1 at 5 V with Main at zero gives full reverb mix; CV in 2 at ±2.5 V moves drift 50% either way
- [ ] CV out 1 measures 0 to 5 V and CV out 2 measures −2.5 to +2.5 V on a meter over 10 minutes; they never move together
- [ ] Pulse out 1 gives one trigger per piano note, including each note of a phrase, and none for choir notes
- [ ] Pulse out 2 drops low between piano notes, whatever the choir is doing
- [ ] Audio into either input comes out centred in the reverb
- [ ] Lowest loop sits centre and C6 piano sits well right, checked on headphones
- [ ] Runs an hour with drift at full and no clicks, dropouts or stuck notes
