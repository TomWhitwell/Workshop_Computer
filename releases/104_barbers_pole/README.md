# BARBER'S POLE

An infinite glissando for the Music Thing Modular Workshop System Computer —
a pitch that rises or falls forever without ever leaving its range.

Named for the illusion it borrows from: a striped pole turning on its axis,
where every stripe travels upward and none of them ever arrives.

## What it does

A Shepard–Risset tone is a stack of components spaced exactly one octave apart,
all sliding together through a fixed spectral envelope. Each fades in at the
bottom, rises through the envelope, and fades out at the top. Because the stack
is octave-spaced, sliding it by one whole octave returns it to exactly where it
started — so the slide can loop forever and the ear never catches the seam.

Turn Main clockwise and the pitch climbs and keeps climbing. Turn it
anticlockwise and it falls without ever reaching the bottom. Park it at noon
and the stack sits still, a drone made of octaves.

The speed range is deliberately wide, and weighted heavily toward the slow end:
about 46 seconds per octave just off centre, roughly a second per octave at
85% of the travel, and up to eight octaves per second at the stops.

The card does this two ways at once, and Y crossfades between them:

- **Internal voice** - a bank of 3 to 11 sine oscillators.
- **Live audio** - incoming audio, octave-stacked: band-passed with every partial transposed into
  the same octave-spaced layers as the oscillators, gliding forever through
  the same window.

## What to feed it

The internal voice needs nothing patched. It is a complete instrument on its
own: the illusion is most obvious with a slow glide and a high layer count.

The live path wants **sustained, harmonically rich** material — pads, drones,
noise, cymbals, feedback, a whole mix. Percussion works but reads as texture
rather than as pitch.

## Controls

The switch's two stable positions select knob pages. Down is spring-loaded and
is a button, not a third page.

**Knobs use pickup across a page change.** Arriving on a page, each knob holds
its previous value until you move it.  While a knob is waiting its LED pulses (0=Main, 1=X, 2=Y), so you can see which ones are still to be picked up.

### Switch MIDDLE — page 1

| Knob | Function |
|---|---|
| **Main** | **Speed and direction.** Centre is stationary. Anticlockwise descends, clockwise ascends. About 80% of the travel sits inside the illusion (46 s down to ~1 s per octave); the last stretch runs out to a deliberate siren at 0.13 s. Summed with CV In 1. |
| **X** | **Density** - 3 to 11 octave layers, stepped discretely with hysteresis. Adding a layer respaces the whole stack, so the change is audible by nature. Summed with CV In 2. |
| **Y** | **Source Mix** — fully anticlockwise is the internal voice, fully clockwise is the live-audio shifter, anywhere between is a blend. |

### Switch UP — page 2

| Knob | Function |
|---|---|
| **Main** | **Quantise** - see the scale table below. |
| **X** | **Stereo width** - Shifts the window slightly between channels, which subtly spreads the image. |
| **Y** | **Output level.** Counter Clockwise = silent. |

#### Scales

| Setting | Degrees (semitones from the root) |
|---|---|
| **Smooth** | none — a continuous glissando |
| **Chromatic** | 0 1 2 3 4 5 6 7 8 9 10 11 |
| **Major** | 0 2 4 5 7 9 11 |
| **Minor** | 0 2 3 5 7 8 10 |
| **Pentatonic** | 0 2 4 7 9 |
| **Diminished** | 0 2 3 5 6 8 9 11 |
| **Whole tone** | 0 2 4 6 8 10 |

The scale repeats every octave, so the glide walks the same degrees forever.
In a *rising* glide the chromatic setting is nearly indistinguishable from
smooth — every semitone is present either way — so the modal settings are what
actually change the character.

The last two are **symmetric**, and suit a barber-pole unusually well: they
repeat *below* the octave — diminished every minor third, whole tone every
whole tone — so the stack is already self-similar at a smaller interval than
the wrap, and the climb has no home key to return to.

### Switch DOWN — momentary

- **Short press** toggles **FREEZE**: the stack stops moving through the
  envelope but keeps sounding. **Audio In 2** does the same from a gate, and
  resumes from exactly where it paused rather than jumping ahead.
- LED 3 lights while frozen; press again to resume.

### Jacks

| Jack | Function |
|---|---|
| Audio In 1 | Source for the octave stack, in both modes |
| Audio In 2 | **HOLD while high** — stops the glide and resumes from exactly where it paused. Above about 1.2 V holds, below 0.6 V releases; unpatched it does nothing |
| CV In 1 | Bipolar speed offset, summed with Main |
| CV In 2 | Bipolar density offset, summed with X |
| Pulse In 1 | **Advances one scale degree** in stepped modes; the strike trigger in Hidden Barber. An event, not a gate — it never holds the glide |
| Pulse In 2 | **Reverse direction while high** |
| Audio Out 1 / 2 | Stereo output |
| CV Out 1 | Master phase — a 0–1 V ramp, **one octave per cycle** on a V/oct input |
| CV Out 2 | Window level — swells and fades as CV Out 1 rises |
| Pulse Out 1 | One trigger per octave wrap — the card's own clock, locked to the glide |

## The octave stack

Feed the live audio stack anything with clear pitch content — a voice, a synth line, a drone, a chord. Percussion works too but the transposed copies smear, since each read head spans a 341 ms window.

## Stepped modes

With Main on page 2 at anything other then fully counter clockwise, the glide snaps to a scale. The transition is **slewed rather than snapped** — about 1.4 ms.

Pulse In 1 advances one degree. Glide and stepping work together: park Main at
centre for pure manual stepping, or leave it off-centre and the pulses nudge an
already-moving glide — the pole keeps climbing between steps.

That combination is why **hold lives on Audio In 2 rather than on Pulse In 1**.
A gate and a step are different gestures, and one jack cannot be both: while
Pulse In 1 also froze the glide, stepping it necessarily stopped it, so the
interesting half of this never worked.

**Pulse Out 1 fires once per octave wrap.** Self-patch it to Pulse In 1 and the
card advances a scale step every time the stack completes a cycle; or use it to
clock something else in time with the illusion.

## The CV outputs

**CV Out 1** is the master phase as a **0–1 V ramp**, one octave per cycle, so
an oscillator patched to it climbs in tune with the pole. It is calibrated per
card rather than scaled by hand.

**CV Out 2** is the window level at that same phase — what the bottom layer of
the stack is doing.

Patch both together and you get one Shepard tone layer made external: send CV Out 1
to an oscillator's V/oct and CV Out 2 to a VCA, and it rises in pitch while
swelling and fading, then the next cycle begins. Add more voices patched the
same way and offset, and you can build the illusion outside the card.

## HIDDEN BARBER (alt-boot)

**Hold the switch DOWN at power-on.**

The pole keeps climbing — but silently, which is where the name comes from:
the pole is still there, still turning, but you only see him when you strike
hit. A trigger on Pulse In 1 voices a **ping** at whatever pitches the pole has
reached.

Trigger repeatedly and each ping comes from a different point on the pole, building a chord out of one silently-rising structure.

**Hold the pole still** — a gate into Audio In 2, or a press of the Down switch
— and every strike gives the *same* chord instead. That turns the mode from an
arpeggio into a fixed voicing you can play, and releasing lets the pole carry
on from where it stopped.

### Switch MIDDLE — page 1

| Knob | Function |
|---|---|
| **Main** | **Pole speed** - how fast the invisible climb moves between strikes, and which way |
| **X** | **Density** - 3 to 11 layers in each strike |
| **Y** | **Source Mix** - internal sines anticlockwise, your own input octave-stacked clockwise |

### Switch UP — page 2

| Knob | Function |
|---|---|
| **Main** | **Quantise** — smooth, 12-ET, major, minor, pentatonic, diminished, whole tone. Strikes land on scale degrees rather than anywhere on the glide |
| **X** | **Ping decay** — 17 ms click to a 1.7 s ring |
| **Y** | **Output level** |

Page 1 is identical to normal boot. On page 2 only X differs: it is decay here
rather than stereo width, and width sits at a fixed modest spread.

**Knobs use pickup across a page change** — arriving on a page, each knob holds
its previous value until moved about 4°, and its LED pulses until it does. Page
2 starts at smooth / shortest decay / full level, so on the first visit the
decay knob does nothing until you nudge X.

Four voices, oldest stolen when all are busy, so the newest four strikes are
always the ones sounding.

> **A caveat about fast triggering.** Four voices, eleven layers each, with the
> live input octave-stacked through all of them, is close to what an RP2040 can
> do at 48 kHz. Trigger fast enough that all four voices are ringing at once —
> especially with a long decay and Y clockwise — and the card will distort.



Even LEDs (0/2/4) indicate a normal boot, odd LEDs (1/3/5) the alt-boot.

## Flashing

Hold BOOTSEL while connecting the Pico, then drag `UF2/shepard.uf2` onto the
drive that appears. It reboots on its own.

To build from source you need the Pico SDK:

```
cmake -G Ninja -B build -S .
ninja -C build
```

`FLASHME/shepard.uf2` is refreshed by every build, so it always matches the
source you just compiled; `UF2/` holds the released binary.

## Credits

Built on Chris Johnson's header-only
[ComputerCard](https://github.com/TomWhitwell/Workshop_Computer) library for
the Music Thing Modular Workshop System.

The name is the visual illusion the sound is usually compared to. The tone
itself is Roger Shepard's (1964); the continuous glissando form is
Jean-Claude Risset's.

## License

MIT — see LICENSE.
