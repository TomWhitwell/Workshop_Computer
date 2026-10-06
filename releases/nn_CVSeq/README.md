# CVSeq

An eight-step CV sequencer for the Workshop Computer, after the Performer
modulator in Native Instruments' Massive. Each step plays a *shape* rather than
a single voltage, and the steps run on one after another as one continuous
control voltage. It's edited from a
[Music Thing 8mu](https://www.musicthing.co.uk/8mu_page/), with an optional web
editor.

Built from the same template as WaveSeq: the same 8mu paging and pickup, USB
modes, knob pickup, web editor and presets.

## What a step is

Each step has eight settings, on the 8mu's eight pages:

| Button | First page | Second page |
|---|---|---|
| A | **SHAPE 1**: one of 40 shapes | **START**: level at the start of the step |
| B | **SHAPE 2**: one of 40 shapes | **END**: level at the end of the step |
| C | **MORPH**: from shape 1 (down) to shape 2 (up) | **QUANT**: digital stepping of the shape |
| D | **LEVEL**: the step's level | **CHANCE**: the chance the step plays |

A step's voltage is the morph between its two shapes, times its level, times a
ramp from its START level to its END level across the step. So START and END
(both 100% by default) let a step fade in or out, or tilt a flat shape into a
ramp.

**QUANT** turns the step into held stairs: fully down is smooth, then 32 stairs
across the step, fewer as it rises, down to one held value at the top.

**Scales.** With the switch up, the Main knob chooses a scale. Off (fully
down, the default) leaves QUANT as above. Any other scale also snaps the
voltage of every step with QUANT above zero to the nearest note of the scale,
1 V/oct with C at 0 V. The snap comes last, after LEVEL, START and END, depth,
offset and smoothing, so a quantised step only ever gives the scale's notes:
a fade-in climbs through the scale, and smoothing slows a change down by
stepping through the notes in between rather than gliding off the scale. Steps
with QUANT fully down stay smooth, so
melodic and smooth steps can share a sequence. CV Out 2 follows the scale for
every step.

The scales, in order round the knob: Off, Chromatic, Major, Minor, Harmonic
minor, Dorian, Phrygian, Lydian, Mixolydian, Major pentatonic, Minor
pentatonic, Blues, Whole tone, Diminished, Fifths, Octaves. When the scale
changes, LEDs 1-4 show its number in binary for a second (LED 1 the lowest
bit; none lit is Off).

**CHANCE** is rolled each time a step comes round. A step that doesn't play holds
the voltage where it was for its length, and gives no trigger. Default: always.

### The shapes

High, Middle, Low, Ramp up, Ramp down, Triangle, Valley, Hump, Sine, Cosine,
Exp rise, Exp rise x2, Exp rise x3, Exp fall, Exp fall x2, Exp fall x3,
Log rise, Log fall, S rise, S fall, Square, Square late, Pulse, Pulse late,
Stairs up, Stairs down, Saw x2, Saw x3, Saw down x2, Saw down x3,
Triangle x2, Triangle x3, Sine x2, Sine x3, Pluck, Swell, ADSR, Bounce,
Random steps, Random smooth.

The SHAPE faders select from these in order, about three fader positions to a
shape. The "x2" and "x3" shapes repeat two or three times within one step.
Every shape is 0 to 1 across one step; the random shapes are fixed patterns,
the same every time.

## The 8mu

The eight faders are the eight steps. The four buttons choose the page; press a
button again to flip to its second page (and again to flip back). Pressing a
different button always starts on its first page.

**Pickup.** After a page change the faders don't do anything until they reach
the value already stored for their step (or pass it), then take it over. So
changing page never makes the voltage jump.

**LEDs.** The 8mu's LEDs show the stored values on the current page, and the
playing step is lit fully. On the Computer, LEDs 1-4 show which button's page
is selected: lit steadily for its first page, blinking slowly for its second.

**Motion.** Tilting the 8mu forward and back adds to every step's MORPH.

Without an 8mu the card plays a default sequence, still under the panel
controls.

## Directions

**Hold the switch down and turn Main** to choose one: the knob's travel is
split into eight zones, in the order of the table below. Turning Main while the
switch is held doesn't touch the scale or the rate; afterwards Main waits to
pick its scale or rate up again, so neither jumps. A quick tap without turning
still steps to the next direction.

While the switch is held, and for a second after, LED 4 is lit to show it's
the direction, and LEDs 1-3 show its number (0-7, in the table's order) in
binary, LED 1 the lowest bit.

| Direction | Steps | Shapes |
|---|---|---|
| Step forward | 1-8 | forwards |
| Step ping-pong | 1-8, then 7-2 | forwards |
| Step backward | 8-1 | forwards |
| True ping-pong | 1-8, then 8-1 | forwards, then backwards |
| True reverse | 8-1 | backwards |
| Random step | a random step each time, never the same twice running | forwards |
| Random reverse | 1-8 | each at random forwards or backwards |
| True random | a random step each time, never the same twice running | each at random forwards or backwards |

The "step" directions change only the order of the steps; each shape still
plays forwards. The "true" directions play the whole sequence backwards, every
shape reversed too, as if the voltage were recorded and played in reverse. True
ping-pong plays step 8 forwards and then backwards at the turn, so the voltage
turns round without a jump (same for step 1 at the other end). Step ping-pong
doesn't repeat the end steps. The random directions roll again at every
step, so they never settle into a loop.

## Panel

| Control | Function |
|---|---|
| Switch up | Main = scale, X = depth (-100% to +100%), Y = offset (-5 V to +5 V) |
| Switch middle | Main = rate (8 s to 10 ms per step), X = smoothing (off across the bottom of the knob, then 1 ms to about 2 s), Y = morph offset (-100% to +100%, all steps) |
| Switch down | Hold and turn Main to choose the direction; tap to step to the next |

All knobs have a small dead zone at each end, so the full range is reached
even if a knob doesn't quite read its very ends, and the two-sided settings
(depth, offset, morph offset) have one in the middle, so exactly 0 is easy to
find.

The six knob settings each keep their value. After the switch moves, a knob
does nothing until it's turned to (or past) its new setting's value, then takes
over. LED 6 (bottom right) blinks fast while a knob is waiting. At power-up the
knobs take over straight away for the position the switch is in; the others
start at depth +100%, offset 0 V, scale Off, one step a second, smoothing off
and morph offset 0.

| Jack | Function |
|---|---|
| CV In 1 | Morph offset: +5 V moves every step all the way from shape 1 to shape 2 |
| CV In 2 | Rate, 1 V/oct |
| Pulse In 1 | Clock: while clocks keep arriving, each step lasts one clock |
| Pulse In 2 | Restart from the first step |
| CV Out 1 | The sequence: offset + depth x (0 to 5 V), smoothed |
| CV Out 2 | CV Out 1 quantised to the scale (to semitones with scale Off), 1 V/oct |
| Audio Out 1 | CV Out 1, uncalibrated |
| Audio Out 2 | CV Out 1 inverted, uncalibrated |
| Pulse Out 1 | Trigger at the start of every step that plays |
| Pulse Out 2 | Trigger at the start of the sequence |

Audio In 1 and 2 are unused. LED 5 (bottom left) follows CV Out 1's level.

## Three ways to use it

| Plugged into the Computer's USB socket | The card is | The faders are |
|---|---|---|
| an 8mu | USB host | the 8mu's, read directly. No computer needed |
| a computer | a USB MIDI device called **CVSeq** | the web editor's, and an 8mu plugged into the *computer* is passed on by the editor |
| nothing | USB host, waiting | (plug an 8mu in any time) |

The card picks its mode once, at power-up, so **after plugging a computer in,
power-cycle the module**. This needs Computer Rev 1.1 hardware; older boards
are always a USB device.

## Web editor

Open [`web/index.html`](web/index.html) in Chrome or Edge, with the computer
plugged into the Computer's USB socket and the module power-cycled, and press
**Connect card & 8mu**. It's a single file and needs no network. The page reads
the sequence from the card, then sends every edit as it's made.

- **Sequence.** The eight steps as one voltage, step 1 on the left, with a
  playhead that runs backwards through a reversed step. Faint steps have less
  than full chance. Click a step to select it.
- **Readouts.** Step, rate, depth, offset, smoothing, morph offset and
  whether the card is clocked, live from the card.
- **Direction** buttons set the card's direction, and follow it when the
  switch is tapped.
- **CV Out 1.** A scope of the last four seconds of the output.
- **Faders.** Eight pages of eight, as on the 8mu.
- **Shapes.** All 40. Click one to give it to the selected step.
- **8mu on the computer.** Its faders, buttons and tilt drive the page, with
  the same pickup as the card, and the page lights its LEDs as the card would.
- **Presets.** Seven built in, plus your own, kept in the browser. Save,
  update, rename, delete, export and import as JSON. Loading one sends it to
  the card.

Without a card the page still edits, previews and keeps presets, with a
simulated playhead at one step a second.

The protocol is documented in [`sysex.h`](sysex.h).

## Building

```
mkdir build && cd build
cmake .. && make
```

Needs the Pico SDK. `EightMU.h` and `ComputerCard.h` are copied from
`Demonstrations+HelloWorlds/PicoSDK/ComputerCard`.

## Not yet

- Sequences aren't saved on the card, so they're lost at power-off. Keep them
  as presets in the web editor and send them again after power-up.
