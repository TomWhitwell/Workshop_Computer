# 433 Sense of Space - Mayakovsky CC0 Version

A whimsical loop card for the Music Thing Workshop Computer, inspired by John
Cage's **4'33"**: a small fragment of theatre ambience is looped for about 91
seconds and placed in imaginary performance spaces.

It is not silence. It is the room, the audience, the building, and the slightly
ridiculous act of listening very hard.

It is not a recording or reconstruction of a 4'33" performance. It is a small
sound work about the anticipation, room tone, and incidental human activity around
the idea of a performance.

## Controls

| Control | Function |
|---------|----------|
| Main | Performer restlessness: automatic fidgets from none to occasional |
| X | Performance space, from small dry room to a restrained cathedral-like hall |
| Y | Reverb amount, from almost dry to fully washed |
| Switch Up | Stop and arm the performance |
| Switch Middle | Start from the beginning and play three loops |
| Switch Down | Trigger musician-shifts-in-seat one-shot |
| Pulse In 1 | Trigger musician-shifts-in-seat one-shot on rising edge |
| Pulse In 2 | Restart the three-loop performance from the beginning |

The card powers up armed rather than playing. Move the switch Up to stop, reset
the audio position, clear the reverb tail, and relight the three countdown LEDs.
Move the switch to Middle to begin a complete 4'33" performance: three passes
through the 91 second ambience loop, then automatic stop and mute. Moving the
switch Down during a performance, or sending a rising edge to Pulse In 1, triggers
the chair/stool creak one-shot; returning to Middle does not restart the performance
while it is already running. Sending a rising edge to Pulse In 2 restarts the
three-loop performance immediately, as if the card had been stopped/armed and then
started again.

The creak is intentionally quiet and sits behind the room ambience, as if the
performer has shifted slightly rather than knocked over the furniture.

Main adds a little unattended human presence. Fully anticlockwise keeps the card
manual-only: creaks happen only from Switch Down or Pulse In 1. Turning Main up
allows occasional automatic creaks during the three-loop performance, with shorter
random waits as the knob is turned clockwise.

Automatic restlessness waits for about 20 seconds after the performance starts
before the first possible fidget. At maximum restlessness, automatic creaks are
spaced by roughly 5-15 seconds; lower settings stretch the random waits toward
about 90 seconds. Automatic creaks play at 25% gain, while manual/Pulse-triggered
creaks play at 37.5% gain, so the unattended fidgets still sit slightly farther
back in the room.

## Outputs

| Jack | Function |
|------|----------|
| Audio Out 1 | Left stereo output |
| Audio Out 2 | Right stereo output |

## LEDs

The left column is a simple 4'33" countdown. Three LEDs are lit before playing and
at the start of the performance. One LED goes out after each 91 second loop. After
the third loop, the card stops and the audio mutes.

The right column glows dimly while the performance is running.

## Builds

This CC0 version uses a Mayakovsky Theatre audience ambience sample from
approximately 4:45-6:16, avoiding speech heard around 6:25. It has both 2 MB and
16 MB builds:

| File | Card size | Audio asset |
|------|-----------|-------------|
| `UF2/433_sense_of_space_2mb_mayakovsky_cc0.uf2` | 2 MB | 91 seconds, 10 kHz stereo signed 8-bit, sourced from 4:45-6:16 |
| `UF2/433_sense_of_space_16mb_mayakovsky_cc0.uf2` | 16 MB | 91 seconds, 24 kHz stereo signed 16-bit, sourced from 4:45-6:16 |

The two builds use the same code, controls, LED countdown, reverb, restlessness
automation, loop length, 2 second loop crossfade, and chair creak one-shot. The only
intentional difference is the ambience sample format:

| Version | Difference |
|---------|------------|
| 2 MB | Uses a 10 kHz stereo ambience asset so the complete firmware fits on a 2 MB card. Current binary payload is about 1.88 MB, leaving roughly 218 KB spare. |
| 16 MB | Uses a 24 kHz stereo 16-bit ambience asset for much lower sample grain and cleaner room detail. Current binary payload is about 8.80 MB, leaving about 7.98 MB spare. |

The UF2 files are larger than their flash payloads because UF2 adds block metadata.
The flash payload inside each UF2 is the relevant storage size.

The 16 MB UF2 can take much longer to copy/flash than the 2 MB UF2. This is normal:
the higher-quality 16-bit ambience makes the UF2 wrapper substantially larger even
though the actual flash payload still fits comfortably on a 16 MB card.

## Pulse Input

Pulse In 1 triggers the chair/stool creak on a rising edge. It retriggers the same
one-shot used by Switch Down, with no extra audio data. Pulse-triggered creaks use
the same level as Switch Down creaks, so they are more deliberate than the quieter
automatic restlessness fidgets.

Pulse In 2 restarts the full 4'33" performance from the beginning. It resets the
loop position, relights the three countdown LEDs, clears the reverb tail, starts
playback immediately, and restarts the 20 second restlessness grace period.

## Seat Creak Candidate

Selected candidate:
https://freesound.org/people/unkleceeg/sounds/276473/

`Chair creaking back and forth 01.wav` by `unkleceeg`, licensed Creative Commons 0.
The one-shot currently baked into the firmware is a 1.35 second stereo signed 8-bit
12 kHz excerpt from around 7.95 seconds in the original WAV. It is mixed into the dry
loop before the reverb, so it appears inside the selected performance space.

## Licence And Source Audio

See `LICENSE` for the mixed-license terms covering this folder.

The embedded ambience is derived from:

`Mayakovsky theatre main stage - spectators walking in, taking their seats, ambience OMNI mics.wav`
by `gladkiy`, licensed Creative Commons 0.

Source page:
https://freesound.org/people/gladkiy/sounds/347832/

## Technical Notes

The source is baked into the firmware as a 91 second stereo loop taken from
approximately 4:45-6:16 of the Mayakovsky Theatre recording. Playback is linearly
interpolated to the Workshop Computer's 48 kHz audio rate, then sent through the
integer Dattorro-style reverb from Reverb+. The X control deliberately avoids the
near-freeze end of the reverb algorithm so the largest setting remains musical
rather than turning the quiet ambience into noise.

The integer reverb implementation is based on Jon Dattorro's reverb design and
the [el-visio/dattorro-verb](https://github.com/el-visio/dattorro-verb)
reference implementation; the source retains its attribution comment.
