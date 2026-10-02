# Wave Sequencer

Wavestation-style wave sequencing for the Workshop Computer, edited from a
[Music Thing 8mu](https://www.musicthing.co.uk/8mu_page/) over USB MIDI host.

An eight-step sequence, where every step plays a wave from a bank of 64
single-cycle waves for a set time, at a set pitch and level, crossfading into
the next step.

## The 8mu

Plug the 8mu into the Computer's USB socket. That needs Computer Rev 1.1
hardware, with the Computer acting as USB host.

The eight faders are the eight steps. The four buttons on top choose the page,
which is what the faders edit:

| Button | Page  | Fader sets                                              |
|--------|-------|---------------------------------------------------------|
| A      | WAVE  | Wave position, scanning through the 64-wave bank        |
| B      | TIME  | Step duration, 20 ms to 4 s. Fully down skips the step. In clocked mode, 1-8 clocks |
| C      | PITCH | -12 to +12 semitones, centre is no offset               |
| D      | LEVEL | Step loudness                                           |

**Pickup.** After a page change the faders don't do anything until they reach
the value already stored for their step (or pass it), then take it over. So
switching page never makes the sound jump.

**LEDs.** The 8mu's LEDs show the stored values on the current page, and the
playing step is lit fully. On the Computer, LEDs 1-4 show which page is
selected.

**Motion.** Tilting the 8mu forward and back scans every step's wave together,
up to 16 waves either way. Tilting it left and right detunes Audio Out 2 by up
to 50 cents. Lying flat, neither does anything.

Without an 8mu the card plays a default sequence, still under the panel
controls.

## Panel

| Control     | Function                                                   |
|-------------|------------------------------------------------------------|
| Main knob   | Pitch, C1 to C7                                            |
| X knob      | Speed, 1/8x to 8x                                          |
| Y knob      | Crossfade, from a hard cut to fading over the whole step   |
| Switch up   | Ping-pong                                                  |
| Switch mid  | Forward loop                                               |
| Switch down | Restart from the first step                                |

| Jack        | Function                                                   |
|-------------|------------------------------------------------------------|
| Audio In 1  | Linear FM                                                  |
| Audio In 2  | Wave scan, +/-32 waves, audio rate                         |
| CV In 1     | Pitch, 1V/oct                                              |
| CV In 2     | Speed, 1V/oct                                              |
| Pulse In 1  | Restart                                                    |
| Pulse In 2  | Clock. Steps advance on clocks while they keep arriving    |
| Audio Out 1 | Wave sequence                                              |
| Audio Out 2 | Wave sequence, detuned                                     |
| CV Out 1    | Current step's pitch offset, 1V/oct                        |
| CV Out 2    | Current step's level, crossfaded, 0-5V                     |
| Pulse Out 1 | Trigger on every step                                      |
| Pulse Out 2 | Trigger at the start of the sequence                       |

## The waves

Eight families of eight, ordered so that neighbouring waves morph smoothly:

1. Additive build, sine to saw
2. Saw to square
3. Pulse width, 50% to 4%
4. Hard-sync saw
5. FM, ratio 1 then ratio 3, rising index
6. Vowel formants
7. Wavefolded sine
8. Stepped sines, then random spectra

They are generated at power-up and band-limited at three levels, chosen by
pitch, to keep aliasing down on high notes.

## Building

```
mkdir build && cd build
cmake .. && make
```

Needs the Pico SDK. `EightMU.h` and `ComputerCard.h` are copied from
`Demonstrations+HelloWorlds/PicoSDK/ComputerCard`.

## Not yet

- Sequences aren't saved, so they're lost at power-off.
