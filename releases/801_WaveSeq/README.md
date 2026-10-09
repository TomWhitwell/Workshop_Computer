# Wave Sequencer

Wavestation-style wave sequencing for the Workshop Computer, edited from a
[Music Thing 8mu](https://www.musicthing.co.uk/8mu_page/) over USB MIDI host.

A sequence of up to 32 steps, where every step plays a wave from a bank of 64
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

There are 32 steps, in four banks of eight, and the eight faders edit one bank
at a time. The sequence starts 8 steps long (bank A), and its length can be
anything from 1 to 32.

The four buttons are acted on when you **let go**, so how long you held one
decides what it does:

| Do this | To |
|---|---|
| **Short press** A-D | Choose the page, which is what the faders edit (below). Press the same button again for its second page (and again to flip back); another button always starts on its first page |
| **Long press** A-D (half a second) | Choose the bank the faders edit: A = steps 1-8, B = 9-16, C = 17-24, D = 25-32. The LED of fader 1-4 for that bank flashes three times |
| **Hold** A-D **and move a fader** | Set the last step: the button is the bank, the fader the step within it. Hold D and move fader 8 for 32 steps; hold A and move fader 4 for 4 steps. The fader has to move a good way (about a sixth of its travel) to count. The faders up to the last step light briefly. The fader movement doesn't edit anything |

Steps past the end of the sequence are skipped, as a step with TIME fully down
is, and keep their settings, so shortening and lengthening the sequence loses
nothing. On power-up, steps 9-32 start as copies of steps 1-8.

| Button | First page | Fader sets | Second page | Fader sets |
|--------|------------|------------|-------------|------------|
| A | WAVE  | Wave position, scanning through the 64-wave bank | FM   | Per-step FM amount, default 100% |
| B | TIME  | Step duration, 20 ms to 4 s. Fully down skips the step. In clocked mode, 1-8 clocks | SCAN | Per-step wave scan amount, default 100% |
| C | PITCH | -12 to +12 semitones, centre is no offset | GLIDE | Per-step glide time, default 0 (jump) |
| D | LEVEL | Step loudness | GATE | Per-step gate length on Pulse Out 1, default half the step |

**Per-step FM and SCAN** multiply the panel's FM amount (X, switch middle) and
wave scan amount (Y, switch middle) for each step. They crossfade from step to
step along with the wave, pitch and level, so a long crossfade glides between
them. A step at 0% gets no FM (or scan) at all, whatever the knob says.

**GLIDE.** Fully down, a step jumps straight to its pitch. Above that, its
pitch slides from wherever the previous step's pitch was, starting as the step
begins to be heard (the start of the crossfade into it), and taking up to the
whole length of the step at the top. CV Out 1 glides with it.

**GATE.** Sets how long Pulse Out 1 stays high for each step, as a fraction of
the step (never shorter than 5 ms, so short settings make triggers). Fully
down gives no pulse at all for that step, so the page doubles as a rhythm
pattern. At the top the gate stays high into the next step, a tie. The
default is half the step.

**Pickup.** After a page change the faders don't do anything until they reach
the value already stored for their step (or pass it), then take it over. So
switching page never makes the sound jump.

**LEDs.** The 8mu's LEDs show the stored values on the current page, the
playing step is lit fully, and steps past the end of the sequence are dark. On the Computer, LEDs 1-4 show which button's page
is selected: lit steadily for its first page, blinking slowly for its second.

**Motion.** Tilting the 8mu forward and back scans every step's wave together,
up to 16 waves either way. Tilting it left and right detunes Audio Out 2 by up
to 50 cents. Lying flat, neither does anything. Both are gently smoothed (a
lag of about 40 ms), as the 8mu sends its tilt in coarse steps that would
otherwise make the scan and detune zipper.

Without an 8mu the card plays a default sequence, still under the panel
controls.

## Web editor

Open [`web/index.html`](web/index.html) in Chrome or Edge. It's a single file
and needs no network. Safari and iOS can't do WebMIDI with SysEx.

Plug the computer into the Computer's USB socket with a data cable,
power-cycle the module, and press **Connect card & 8mu**. The page reads the
sequence from the card (the card is the source of truth), then sends every
edit as it's made.

From the top (each section has a − button by its heading that folds it away,
and + to bring it back; the browser remembers which are folded):

- **Readouts.** Step, pitch (as a note name), speed, crossfade, direction and
  whether the card is clocked, live from the card's knobs, CV and switch.
- **Bank, length and shift.** Bank buttons, −/+ for the length, and −/+ to
  shift every step in the sequence one place left or right (all its settings
  with it, the end step wrapping round). Just above the sequence, so they stay
  put as the length changes.
- **Sequence.** All 32 steps, eight to a row. In a row with steps in the
  sequence, each step's width is its duration, its trace is its wave at its
  level, and the shaded end is its crossfade into the next; steps past the end
  shrink to small grey boxes, so on load there's one row of eight and three
  rows of boxes. The bank the faders edit has an orange box round its row. A
  playhead follows the card. Click a step to select it (and its row's bank).
- **Copy, paste & randomise.** Select steps on the sequence: click one,
  Shift+click a range, Ctrl/⌘+click to add or remove single steps,
  double-click for a whole row, Alt+click for a whole column (the same fader
  in every bank), or use the buttons. Copy (Ctrl/⌘+C) takes every setting of
  the selected steps; Paste (Ctrl/⌘+V) puts them back: one copied step fills
  every selected step, several fill the same number of selected steps in
  order, or else keep their pattern from the first selected step.
  Shift+paste pastes only the page the faders show. Randomise changes the
  wave; the wave, pitch and level; or everything (time, FM, scan, glide and
  gate too), of the selected steps, a row, a column or the whole sequence.
  Pitch stays within the spread chosen (±2, ±7 or ±12 semitones), level stays
  audible, and a randomised step is never skipped. Undo (Ctrl/⌘+Z) takes back
  pastes, randomising and shifts. The keys are listed on the page.
- **Now playing.** The wave being heard, including part-way through a
  crossfade.
- **Faders.** Eight pages, like the 8mu's two pages per button: tabs A, A2,
  B, B2... choose the page. They show the selected bank's eight steps.
  Drag, use the arrow keys, or double-click to reset.
- **Wave bank.** All 64 waves. Click one to give it to the selected step, or
  to every selected step.
- **8mu on the computer.** Its faders, buttons and tilt drive the page, with
  the same pickup, banks and last-step setting as the card (the **8mu** card
  on the right lists them). The page lights the 8mu's LEDs as the card
  would. A dashed line on a fader shows where the 8mu's fader is while it
  waits to pick up.
- **Presets.** Seven built in, plus your own, kept in the browser
  (`localStorage`), with their length. Save, update, rename, delete, and
  export or import as a JSON file. Loading a preset sends it to the card.
  Presets from before 32 steps still load, playing 8 steps with steps 9-32 as
  copies of 1-8.
- **Direction** buttons (forward, ping-pong, random) set the card's direction,
  and follow it when the switch is tapped. The page also shows what the X and
  Y knobs are doing and whether one is waiting to pick up.
- **Restart** and **Reset** (to the default sequence) act on the card too.

Without a card the page still edits, previews and keeps presets, with a
simulated playhead at 1x speed.

The card and page talk SysEx; the protocol is documented in
[`sysex.h`](sysex.h). The card and the editor must be the same version: this
one (protocol 4, 32 steps) won't talk to older firmware.

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

All knobs have a small dead zone at each end, so the full range is reached
even if a knob doesn't quite read its very ends, and speed has one in the
middle, so exactly 1x is easy to find.

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
| Pulse In 1  | Clock. Steps advance on clocks while they keep arriving, for clocks up to 20 s apart. Followed from the second pulse; the speed knob takes over again after four of the clock's periods (at least 2 s) without one |
| Pulse In 2  | Restart from the first step                                |
| Audio Out 1 | Wave sequence                                              |
| Audio Out 2 | Wave sequence, detuned                                     |
| CV Out 1    | Current step's pitch offset, 1V/oct, including glide        |
| CV Out 2    | Current step's level, crossfaded, 0-5V                     |
| Pulse Out 1 | Gate for each step, length set on the GATE page (D, second page) |
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
