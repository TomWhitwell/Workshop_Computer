# Weights

Random pitch CV for the Workshop Computer, with the randomness guided by a
[Music Thing 8mu](https://www.musicthing.co.uk/8mu_page/): its faders are
probabilities. Two voices wander at once. At every step each draws a note of
the scale, an octave, a gate length and a ratchet, each weighted by a page of
faders, and plays them as a 1V/oct pitch, a gate and an envelope.

Built from the same template as WaveSeq and CVSeq: the same 8mu handling and
pickup, USB modes, knob pickup, web editor, 8mu sharing between editor
windows, and presets. It works with an 8mu alone, no computer needed.

## The 8mu

The four buttons choose the page. Each button has four pages: press it
again for its **second page** (voice 2's), again for its **envelope page**,
again for voice 2's envelope page, and again to go back to the first.

| Button | First page | Second page | Third page (envelope) | Fourth page |
|---|---|---|---|---|
| A | **NOTES**: the scale's notes, from the root up | **NOTES 2** | **ATTACK**: ÷4 … ×4 the Attack knob's time | **ATTACK 2** |
| B | **OCTAVES**: fader 1 −2 V, fader 3 0 V, up to fader 8 +5 V | **OCTAVES 2** | **DECAY**: ÷4 … ×4 the Decay knob's time | **DECAY 2** |
| C | **GATES**: fader 1 a rest, 2-7 one to six sevenths, 8 tied | **GATES 2** | **PLUCK**: −50% … +50% of pluckiness | **PLUCK 2** |
| D | **RATCHETS**: 1×, 1.5×, 2×, 3× on one note; then 1.5×, 2×, 3×, 4× with a new note at every hit | **RATCHETS 2** | **DEPTH**: the envelope's peak, 1/8 … full | **DEPTH 2** |

**Faders are relative probabilities.** A fader at zero is never chosen, and
one twice as high as another is chosen twice as often. With a first page all
at zero, every choice is equally likely. (The bottom two positions of a fader,
out of 127, count as zero, as faders don't always read exactly 0.)

**Two voices.** Both voices use the first and third pages. The second and
fourth pages belong to voice 2, once they are used:

- While **every fader on a voice 2 page is at zero**, voice 2 draws from the
  matching page of voice 1 as well. You get two different lines with the same
  probabilities.
- **Raise any fader** on a voice 2 page and voice 2 uses that page,
  independently of voice 1.

Each kind of page decides this on its own: voice 2 can have its own notes
while sharing the octaves, gates and ratchets, for example. Pull every fader
on a second page back to zero to share again.

**Envelope pages.** Each note's envelope can stray from the knobs. The
ATTACK and DECAY faders are a spread from a quarter of the knob's time
(fader 1) to four times it (fader 8), with faders 4 and 5 a little either
side (÷4, ÷2.7, ÷1.8, ÷1.2, ×1.2, ×1.8, ×2.7, ×4), kept within each
setting's range. PLUCK goes from half the knob's range less pluck to half
more (−50%, −36%, −21%, −7%, +7%, +21%, +36%, +50%). DEPTH is the
envelope's peak: fader 1 an eighth of full (about 0.6 V) up to fader 8 full
(5 V), for accents. The faders are probabilities as on every page, but an
envelope page **all at zero is off**: the knobs' envelope exactly, at full
depth. They are drawn with the note: once a step, and again at every hit of
a new-note ratchet (faders 5-8 on RATCHETS), so a held-note ratchet repeats
its envelope.

**Notes.** One fader per note of the scale, from the root up. A scale with
fewer than eight notes ignores the extra faders. No scale has more than
eight, so every note has a fader of its own.

**Gates.** Fader 1 is a rest: no gate and no envelope, though the pitch still
moves. Faders 2-7 hold the gate for 1/7 to 6/7 of the time to the next hit.
Fader 8 is **tied**: the gate stays up into the next note, which arrives
without a new envelope (with slew on, it glides in). A gate always falls at
least a millisecond before the next hit, so every new note gets a fresh
rising edge.

**Ratchets.** Faders 1-4 hit 1×, 1.5×, 2× and 3× a step, each hit on the
step's note, octave and gate. Faders 5-8 hit 1.5×, 2×, 3× and 4×, and every
hit draws a new note, octave and gate. 1.5× is two hits, the second two
thirds of the way through the step, so it pushes against the beat.

**Pickup.** After a page change a fader does nothing until it reaches (or
passes) the value already stored, so changing page never changes the
probabilities by accident.

**LEDs.** For **3 seconds** after a fader or button is touched, the LEDs show
the page's probabilities, the likeliest fully lit. After that they show what
is playing:

| Page | For 3 s after a touch | Then |
|---|---|---|
| Notes, Octaves | brightness = probability | the note or octave playing |
| Gates | brightness = probability | a bar up to the gate length: a rest lights the first LED dimly, a tied note all eight, 3/7 the first four |
| Ratchets | each fader flashes its own speed, in time with the steps, brightness = probability | the ratchet in use flashes at every hit |
| Attack, Decay, Pluck, Depth | brightness = probability (dark when the page is off) | the deviation or depth in use |

On a first page that voice 2 shares, voice 2's choice shows dimly too.

Without an 8mu the card plays its default probabilities, still under the
panel controls.

## Panel

| Control | Function |
|---|---|
| Switch up | Main = **scale** (below). X = **clock** ÷8, ÷6, ÷4, ÷3, ÷2, ×1 (centre), ×2, ×3, ×4, ×6, ×8. Y = **slew** on the pitch, two-sided: off in a zone round the centre; turned right, every note glides, 1 ms just past the centre to about 2 s fully right; turned left, only a note that a tied gate leads into glides (the same times, growing to the left), and the other notes jump |
| Switch middle | Main = **decay**, 10 ms to 4 s. X = **attack**, 0.5 ms to 2 s. Y = **pluckiness**: the decay's curve, from a straight line (soft) to a sharp drop with a long tail (a pluck) |
| Switch down | Hold it and turn Main for the **internal tempo**, 40 to 300 BPM (exponential: about 110 in the middle). Like the other settings it picks up: Main does nothing until it reaches the current tempo, and after you let go it has to pick up its scale or decay again. While held, the top four LEDs flash on every step. X and Y keep the settings of the position the switch was pressed from |

Every knob has a dead zone at each end, so the whole range of every setting
can be reached even if a knob doesn't quite read its ends. The six settings
each keep their value: after the switch moves, a knob does nothing until it's
turned to (or past) its new setting's value. LED 6 (bottom right) blinks fast
while a knob is waiting. At power-up the knobs take over straight away for
the switch's position; the others start at Major, ×1 and no slew (centre), or a 300 ms
decay, the shortest attack and pluckiness halfway.

**Scales** (37), in order round Main: Major, Natural minor, Harmonic minor,
Melodic minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major
pentatonic, Minor pentatonic, Blues, Whole tone, Diminished, Bebop dominant,
Hirajoshi, Fifths, Octaves, Raga Bhairav, Raga Todi, Raga Marwa, Raga
Malkauns, Raga Hindol, Maqam Rast, Maqam Bayati, Maqam Hijaz, Maqam Saba, Just
major, Just minor, Harmonic series, Subharmonic series, Slendro, Pelog, Pelog
selisir, 5-TET, 7-TET, 8-TET.
All have their root at C (0 V), and none has more than eight notes, so each
note has its own fader. The ragas are in just intonation (pure ratios, as
they're usually sung): Bhairav and Todi with their flat second and sixth,
Todi's low minor third (32/27) and sharp fourth, Marwa (six notes, no fifth),
pentatonic Malkauns and Hindol. Rast, Bayati and Saba have their neutral
(quarter-tone) steps; Hijaz is in equal semitones. The harmonic series is
harmonics 8-15 folded into an octave; the subharmonic series is its mirror,
subharmonics 16 down to 9. Gamelan tunings vary from one orchestra to the
next; these are typical values. 8-TET divides the octave into eight equal
150-cent steps. When the scale changes the Computer's six LEDs show its
number (1-37) in binary for a second, LED 1 the lowest bit.

| Jack | Function |
|---|---|
| Pulse In 1 | Clock: a step every beat, the clock divided or multiplied by X, for clocks up to a minute apart. Followed from its second pulse. With nothing plugged in, or once four of its periods (at least 2 s) pass without a pulse, the card keeps its own tempo: 120 BPM, until set with the switch held down and Main, or from the web editor |
| CV Out 1, 2 | Voice 1, 2 pitch: 1 V/oct, C at 0 V, calibrated, −2 V to nearly +6 V |
| Audio In 1, 2 | Optional VCA: a signal (audio or CV) plugged in comes out of Audio Out 1 or 2 shaped by that voice's envelope, depth and all, in place of the envelope itself. A full envelope passes it at its own level. Unplug it and the output is the envelope again |
| Audio Out 1, 2 | Voice 1, 2 envelope, 0 to +5 V, or the Audio In through it |
| Pulse Out 1, 2 | Voice 1, 2 gate |

The other inputs are unused. **LEDs**: the top four show the 8mu's page (lit
for a first page, blinking slowly for a second, fast for a third and faster
still for a fourth), or with no 8mu or editor, LEDs 1
and 2 show the gates; the bottom two follow the envelopes.

## Three ways to use it

| Plugged into the Computer's USB socket | The card is | The faders are |
|---|---|---|
| an 8mu | USB host | the 8mu's, read directly. No computer needed |
| a computer | a USB MIDI device called **Weights** | the web editor's, and an 8mu plugged into the *computer* is passed on by the editor |
| nothing | USB host, waiting | (plug an 8mu in any time) |

The card picks its mode once, at power-up, so **after plugging a computer in,
power-cycle the module**. This needs Computer Rev 1.1 hardware; older boards
are always a USB device.

## Web editor

Open [`web/index.html`](web/index.html) in Chrome or Edge, with the computer
plugged into the Computer's USB socket and the module power-cycled, and press
**Connect card & 8mu**. It's a single file and needs no network. The page
reads the faders from the card, then sends every edit as it's made.

- **Now playing.** The scale, clock, slew and envelope settings live from the
  card (a dotted outline while a knob is waiting to pick up), and each voice's
  note, voltage, gate, ratchet and envelope. **Internal tempo** sets the
  card's own clock, 40-300 BPM, used while nothing is plugged into Pulse In 1
  (it follows Main when that sets it with the switch held down).
- **Faders.** The sixteen pages, as on the 8mu, with each fader's meaning in the
  current scale (note names, or cents for microtonal scales) and its share of
  the probability. A second page says whether voice 2 is sharing the first,
  with buttons to copy the first page onto it or clear it to share again. A
  row of dots shows the 8mu's LEDs as the card would light them.
- **Pitch.** The last eight seconds of both voices.
- **Presets.** Six built in, plus your own, kept in the browser: save,
  update, rename, delete, export and import as JSON. A preset is all sixteen
  pages and the internal tempo (the scale, clock and envelopes stay on the
  knobs). Loading one sends it to the card.
- **8mu on the computer.** Its faders and buttons drive the page with the
  same pickup as the card, and the page lights its LEDs as the card would.
  Several 8mus all drive it together. With another Workshop editor open too
  (CVSeq, WaveSeq), one 8mu drives whichever editor you last clicked in; the
  header shows **8mu: here** or **8mu: elsewhere**.

Without a card the page still edits and keeps presets, and plays a simulation
of the card at the internal tempo.

The protocol is documented in [`sysex.h`](sysex.h). The card and the editor
must be the same version (protocol 3). Presets saved before the envelope
pages load with them off.

## Building

```
mkdir build && cd build
cmake .. && make
```

Needs the Pico SDK. `EightMU.h` and `ComputerCard.h` are copied from
`Demonstrations+HelloWorlds/PicoSDK/ComputerCard`.

The engine (`weights.h`) and the 8mu handling (`ui.h`) have no hardware in
them, and `test/run_tests.sh` builds and runs their tests, and a test of the
whole card against stand-ins for the Computer, on a PC with any C++17
compiler.

## Not yet

- Fader settings aren't saved on the card, so they're lost at power-off. Keep
  them as presets in the web editor and send them again after power-up.

Written with AI help (Claude Code).
