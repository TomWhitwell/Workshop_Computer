# Temperance

A Mood-style micro-looper card for the Music Thing Modular Workshop Computer.

2 Nov 2026 · Matt Allison

The design brief the card was built from, kept as written, apart from the
card's name (it was Temperament). Where the card differs, README.md says so
and why.

## Overview

Temperance is two micro loopers sharing one clock, feeding a clocked delay
and a washy plate reverb. The clock sets the sample rate of everything
upstream of the reverb. Slow it down and loops get longer, lower and
grittier in one move, exactly like Mood.

**Lineage.** Mood pairs a wet channel with a looper and runs both from one
clock. Temperance keeps the shared clock but makes both sides loopers, then
puts the space at the end of the chain instead of on one side.

**Signal path.** Audio in 1 feeds Loop A. Audio in 2 feeds Loop B, normalled
to in 1 when unpatched. Each loop records at the clock rate and plays back at
one of six speeds. The two loops sum with the dry input, pass through a bit
crusher and the clocked delay, then into the reverb. Audio outs 1 and 2 carry
the stereo result.

The tinted area moves with the clock. The reverb sits outside it, so the tail
stays smooth at any clock setting.

**Hardware limits that shape the design.**

- RP2040, 264KB SRAM, no PSRAM. Memory, not CPU, is the binding constraint.
- 12-bit audio in and out at a fixed 48 kHz through ComputerCard.
- No FPU. Everything runs in fixed point (Q15 with 32-bit accumulators).

## Panel map

The switch has two layers. Middle is the play layer. Up is the alt layer.
Down is tap only, never held, matching the convention from the AMB-1 card.

| Control | Play layer (switch middle) | Alt layer (switch up) |
| --- | --- | --- |
| Main knob | Clock, 10 stepped rates | Wash: reverb send, decay and damping as one macro |
| X knob | Loop A speed and direction, 6 zones | Delay time |
| Y knob | Loop B speed and direction, 6 zones | Crush depth, 12 bits down to 4 |
| Switch down (tap) | Tap to start recording both loops, tap again to stop and play | Same |

Knobs use pickup on layer change. A knob does nothing until it passes its
stored value, so nothing jumps when you flip the switch.

| Jack | Function |
| --- | --- |
| Audio in 1 | Loop A input and dry signal |
| Audio in 2 | Loop B input, normalled to Audio in 1 |
| Audio out 1 / 2 | Stereo mix: dry, loops, delay and reverb |
| CV in 1 | Clock offset, quantised to steps (1 V per step) |
| CV in 2 | Wash amount, added to the knob |
| CV out 1 / 2 | Envelope follower of Loop A / B |
| Pulse in 1 / 2 | Start or stop recording Loop A / B on its own |
| Pulse out 1 / 2 | Trigger at each loop start, A / B |

## Clock and memory

The clock runs from 48 kHz down to 2 kHz in ten steps that alternate fifths
and fourths, so every move is consonant. The ceiling is 48 kHz because that
is the card's fixed rate; Mood's 64k is out of reach.

**How the clock works.** A 32-bit phase accumulator adds clock / 48000 × 2^32
every sample. Each overflow is a clock tick. On a tick the loopers and delay
write one input sample and step their read heads. Between ticks the output
holds its last value. That sample-and-hold staircase is the 2k grit, and it
costs almost nothing.

Changing the clock on a playing loop changes its read rate, so the loop
repitches by the same interval. Both loops move together, in harmony.

| Step | Clock | Pitch vs 48k | Loop length | Delay max |
| --- | --- | --- | --- | --- |
| 1 | 48 kHz | 0 st | 0.68 s | 0.23 s |
| 2 | 32 kHz | −7 st (fifth) | 1.02 s | 0.34 s |
| 3 | 24 kHz | −12 st (octave) | 1.37 s | 0.46 s |
| 4 | 16 kHz | −19 st | 2.05 s | 0.68 s |
| 5 | 12 kHz | −24 st | 2.73 s | 0.91 s |
| 6 | 8 kHz | −31 st | 4.10 s | 1.37 s |
| 7 | 6 kHz | −36 st | 5.46 s | 1.82 s |
| 8 | 4 kHz | −43 st | 8.19 s | 2.73 s |
| 9 | 3 kHz | −48 st | 10.9 s | 3.64 s |
| 10 | 2 kHz | −55 st | 16.4 s | 5.46 s |

**Storage.** Samples are packed 12-bit, two samples in three bytes. That is
the ADC's native depth, so nothing is lost. A 48KB loop buffer holds 32,768
samples; a 16KB delay buffer holds 10,922.

| Block | RAM | Notes |
| --- | --- | --- |
| Loop A buffer | 48KB | packed 12-bit, clocked |
| Loop B buffer | 48KB | packed 12-bit, clocked |
| Delay buffer | 16KB | packed 12-bit, clocked |
| Reverb lines | 36KB | int16 at a fixed 24 kHz |
| Total audio | 148KB | leaves about 116KB for ComputerCard, stack and RAM-resident code |

Put ProcessSample and the hot helpers in RAM with `__not_in_flash_func` so
flash cache misses never stall the audio.

## Loopers

Each loop is set by two taps. Tap switch down to start recording, tap again
to stop, and the loop plays straight away. Loop length is whatever you
played, up to the buffer limit at the current clock.

**States.**

1. Empty: no loop. The dry signal passes through.
2. Recording: the buffer writes on every clock tick and you hear the input
   dry. If the buffer fills, recording stops and the loop plays on its own.
3. Playing: the read head loops from the start point to the stop point.
4. Tap while playing: a fresh recording starts over the old loop, which
   fades out over about 20 ms.

Switch down records both loops together. Pulse in 1 and 2 use the same start
and stop logic for one loop each, so A and B can have different lengths.

**Speed zones** (X for A, Y for B). Six equal zones across the knob, with
about 3% hysteresis at each edge so nothing chatters.

| Zone | Direction | Speed | Pitch |
| --- | --- | --- | --- |
| 1 | Reverse | ×2 | +12 st |
| 2 | Reverse | ×1 | 0 st |
| 3 | Reverse | ×½ | −12 st |
| 4 | Forward | ×½ | −12 st |
| 5 | Forward | ×1 | 0 st |
| 6 | Forward | ×2 | +12 st |

Half speed sits in the middle of the knob, so the default resting position is
slow and dark.

**Read heads.** Keep the read position as 16.16 fixed point in buffer
samples. Each clock tick adds speed × 65536, negative for reverse. Half speed
reads every sample twice through the hold, which suits the lo-fi character.
Linear interpolation is an option if it sounds too rough at the top clocks.

**Clicks.** Two places need a crossfade:

- The loop seam: crossfade the last 256 ticks into the first.
- Speed changes: run a second read head for 256 ticks and fade across, so a
  zone change glides rather than pops.

**Crush.** Applied after the loopers on the summed signal, so it can change
live. Mask the low bits: 12 bits is clean, 4 is ruined in the right way.
Optional: let crush follow the clock, dropping one bit for every two clock
steps, as a compile-time choice.

**Anti-aliasing.** None by default. The aliasing is the sound. A one-pole
low-pass before the write, tracking half the clock rate, is worth keeping as
a "smooth" option.

## Delay and reverb

The delay sits on the clock with the loopers; the reverb does not. Clocking
the delay keeps the Mood feel, where slowing the clock stretches the echoes
too. The reverb stays at a fixed 24 kHz so the wash never turns to gravel.

**Delay.**

- Buffer: 16KB packed 12-bit, written and read on clock ticks.
- Time: alt-layer X, from 10% to 100% of the buffer. Slew the time over
  about 150 ms so a change bends pitch like tape instead of clicking.
- Feedback: fixed near 50% to start. One-pole low-pass in the loop, about
  3 kHz, then a cubic soft clip. Each repeat gets darker and never runs away.
- The delay output feeds the reverb with the dry and loop signal.

**Reverb: Dattorro plate at 24 kHz.**

- Decimate by two: average each pair of input samples and run the tank on
  every second sample. Upsample with linear interpolation.
- Scale the published delay lengths by 24000 / 29761, about 0.806. The tank
  totals around 18,100 samples, which is 36KB at int16.
- Modulate the two tank allpasses with slow, offset sine LFOs (about 0.3 and
  0.37 Hz, ±8 samples). That drift is most of the washiness.
- Stereo comes free from the tank's two output taps.

| Dattorro line | Original (29.76 kHz) | At 24 kHz |
| --- | --- | --- |
| Input diffusers | 142, 107, 379, 277 | 115, 86, 306, 223 |
| Left tank | 672 (mod AP), 4453, 1800 (AP), 3720 | 542, 3591, 1452, 3000 |
| Right tank | 908 (mod AP), 4217, 2656 (AP), 3163 | 732, 3401, 2142, 2551 |

**Wash macro** (alt-layer Main, plus CV in 2). One control moves three things
together:

| Wash | Send | Decay | Damping |
| --- | --- | --- | --- |
| 0% | 0 | 0.50 | 0.20 |
| 50% | 0.5 | 0.80 | 0.40 |
| 100% | 1.0 | 0.93 | 0.60 |

More wash is longer and darker at once, which is the ambient direction.
Recurrence's shimmer could replace the plate later, but its pitch shifter
needs extra buffer; check the RAM budget first.

## LED design

The six LEDs read as two columns, one per loop, with three overlay views for
moments that need the whole grid. Assumed layout from ComputerCard: LEDs 0
and 1 on the top row, 2 and 3 in the middle, 4 and 5 at the bottom. Left
column is Loop A, right column is Loop B.

**Home view** (switch middle, nothing moving). Each column tells you state,
speed and where the loop is.

| Row | Loop A / B LED | What it shows |
| --- | --- | --- |
| Top (0 / 1) | State | Off when empty. Fast blink (about 6 Hz) while recording. Solid on while playing. |
| Middle (2 / 3) | Speed | Brightness: 25% at ×½, 60% at ×1, 100% at ×2. |
| Bottom (4 / 5) | Playhead | Forward: flash at loop start, then decay (falling saw). Reverse: ramp up to the loop end, then snap off (rising saw). Empty or recording: shows input level instead. |

The playhead saw is the key idea. Direction reads from the shape of the
light, and the loop's length reads as rhythm. When the clock drops, both
bottom LEDs visibly slow together.

**Clock view** (Main turned in the play layer). Takes over the whole grid for
1.5 s after the last movement, then returns home.

- Fills from the bottom row upwards: 4, 5, 2, 3, 0, 1.
- Each step adds half an LED. 48 kHz shows one full LED; 2 kHz shows five
  and a half.
- Slower clock looks heavier, which matches how it sounds.

**Alt view** (switch up). Each row becomes a two-LED bar for one knob: top
row is Wash, middle is Delay time, bottom is Crush. The left LED covers 0 to
50%, the right LED 50 to 100%.

- Pickup feedback: a row breathes slowly (about 1 Hz) until its knob catches
  the stored value, then goes steady.

**Events.**

| Event | Pattern |
| --- | --- |
| Record start | Top LED of each loop starts its fast blink |
| Record stop | Top LED flashes full for 80 ms, then holds solid |
| Pulse in start or stop | Same patterns, on that loop's column only |
| Input clipping | That loop's bottom LED holds full for 100 ms |
| Boot | One slow chase down the grid, 0 to 5, so you know the card loaded |

**Implementation notes.**

- Update LEDs at 1 kHz from a counter in ProcessSample, not every sample.
- Square the brightness before calling LedBrightness so fades look even to
  the eye.
- Overlay priority: event flash, then clock view, then alt view, then home.

## Build plan

Build in ten stages, each one playable before the next starts. The order
puts the risky part, packed storage at a variable clock, early.

- [ ] 1. Skeleton: ComputerCard passthrough, boot chase on the LEDs.
- [ ] 2. One looper at a fixed 48 kHz with int16 storage: tap to record, tap
  to stop, forward only.
- [ ] 3. Packed 12-bit storage, the six speed zones, reverse, seam and speed
  crossfades.
- [ ] 4. Phase-accumulator clock, ten steps on Main, clock LED view.
- [ ] 5. Loop B, pulse in record control, Audio in 2 normalling.
- [ ] 6. Crush and the alt layer with knob pickup.
- [ ] 7. Clocked delay with filtered, soft-clipped feedback.
- [ ] 8. Dattorro plate at 24 kHz and the Wash macro.
- [ ] 9. CV ins, envelope followers on CV outs, loop-start pulses.
- [ ] 10. LED polish, then profile CPU and tune.

**CPU budget.** At 133 MHz there are about 2,770 cycles per 48 kHz sample.
Rough estimates: both loopers with crossfades around 200 cycles, the delay
around 80, the plate around 250 averaged over its half-rate frames, LEDs and
controls under 50. That leaves plenty of headroom. If the reverb runs tight,
move it to core 1 and pass blocks through a small FIFO.
