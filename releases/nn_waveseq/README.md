# Wave Sequencer

Wavestation-style wave sequencing for the Workshop Computer, edited from a
[Music Thing 8mu](https://www.musicthing.co.uk/8mu_page/) over USB MIDI host.

An eight-step sequence, where every step plays a wave from a bank of 64
single-cycle waves for a set time, at a set pitch and level, crossfading into
the next step.

## Three ways to use it

The card works on its own; the 8mu and the web editor are both optional.

| Plugged into the Computer's USB socket | The card is | The faders are |
|---|---|---|
| an 8mu | USB host | the 8mu's, read directly. No computer needed |
| a computer | a USB MIDI device called **Wave Sequencer** | the web editor's, and an 8mu plugged into the *computer* is passed on by the editor |
| nothing | USB host, waiting | (plug an 8mu in any time) |

The card picks its mode once, at power-up, so **after plugging a computer in,
power-cycle the module**. Telling the two apart needs Computer Rev 1.1
hardware; older boards are always a USB device.

## The 8mu

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

## Web editor

Open [`web/index.html`](web/index.html) in Chrome or Edge. It's a single file
and needs no network. Safari and iOS can't do WebMIDI with SysEx.

Plug the computer into the Computer's USB socket with a data cable,
power-cycle the module, and press **Connect card & 8mu**. The page reads the
sequence from the card (the card is the source of truth), then sends every
edit as it's made.

- **Sequence.** The eight steps as a timeline. Each step's width is its
  duration, its trace is its wave at its level, and the shaded end is its
  crossfade into the next. A playhead follows the card. Click a step to select
  it.
- **Readouts.** Step, pitch (as a note name), speed, crossfade, direction and
  whether the card is clocked, live from the card's knobs, CV and switch.
- **Now playing.** The wave being heard, including part-way through a
  crossfade.
- **Faders.** Four pages of eight, like the 8mu: tabs A-D choose the page.
  Drag, use the arrow keys, or double-click to reset.
- **Wave bank.** All 64 waves. Click one to give it to the selected step.
- **8mu on the computer.** Its faders, buttons and tilt drive the page, with
  the same pickup as the card. The page lights the 8mu's LEDs as the card
  would. A dashed line on a fader shows where the 8mu's fader is while it
  waits to pick up.
- **Presets.** Seven built in, plus your own, kept in the browser
  (`localStorage`). Save, update, rename, delete, and export or import as a
  JSON file. Loading a preset sends it to the card.
- **Direction** buttons (forward, ping-pong, random) set the card's direction,
  and follow it when the switch is tapped. The page also shows what the X and
  Y knobs are doing and whether one is waiting to pick up.
- **Restart** and **Reset** (to the default sequence) act on the card too.

Without a card the page still edits, previews and keeps presets, with a
simulated playhead at 1x speed.

The card and page talk SysEx; the protocol is documented in
[`sysex.h`](sysex.h).

## Panel

| Control     | Function                                                   |
|-------------|------------------------------------------------------------|
| Main knob   | Pitch, C1 to C7                                            |
| Switch down | Tap to step the direction: forward, ping-pong, random      |

The switch's up and middle positions choose what the X and Y knobs do:

| Switch | X knob | Y knob |
|--------|--------|--------|
| Up     | Speed, 1/8x to 8x | Crossfade, from a hard cut to fading over the whole step |
| Middle | FM amount (Audio In 1) | Wave scan amount (Audio In 2) |

Each of the four settings keeps its value. After the switch moves, a knob does
nothing until it's turned to (or past) its new setting's value, then takes
over, so nothing jumps. LED 6 (bottom right) blinks fast while a knob is
waiting. At power-up the knobs take over straight away for the position the
switch is in; the others start at speed 1x, crossfade 25%, and FM and wave scan
off (so Audio In 1 and 2 do nothing until turned up).

**Directions.** Forward loops 1 to 8. Ping-pong plays forwards then
backwards, without repeating the end steps. Random picks any other step that
isn't skipped, never the same step twice in a row. After a tap, the top LEDs
show the new direction for a second: one LED for forward, two for ping-pong,
three for random. The web editor can set it too.

| Jack        | Function                                                   |
|-------------|------------------------------------------------------------|
| Audio In 1  | Linear FM, depth set by X (switch middle)                  |
| Audio In 2  | Wave scan, up to +/-32 waves, audio rate, depth set by Y (switch middle) |
| CV In 1     | Pitch, 1V/oct                                              |
| CV In 2     | Speed, 1V/oct                                              |
| Pulse In 1  | Clock. Steps advance on clocks while they keep arriving    |
| Pulse In 2  | Restart from the first step                                |
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

- Sequences aren't saved on the card, so they're lost at power-off. Keep them
  as presets in the web editor and send them again after power-up.
