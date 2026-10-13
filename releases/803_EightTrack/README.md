# EightTrack

An eight-head stereo tape delay for the Music Thing Workshop Computer, played
from a Music Thing 8mu. Loosely inspired by Zlosynth's Kaseta: there's one
loop of tape, recorded from both audio inputs, with eight playback heads along
it, one for each 8mu fader.

Each head has its own **time, volume, pan, stereo width and feedback**. The
feedback goes back onto the tape through a **low-pass and high-pass filter**
and a **tape model** (saturation, hiss, wow, flutter and dropouts). Times
glide, so moving a fader or the Main knob bends the pitch of what's already on
the tape, the way a tape machine does.

It is built on the same structure as [CVSeq](../802_CVSeq) and
[WaveSeq](../801_WaveSeq): the same 8mu paging and fader pickup, the same USB
host/device split, the same style of SysEx protocol and web editor.

## The 8mu

The eight faders are the eight heads. The buttons act **when you let go**:

| Button | First page | Second page (press again) |
|---|---|---|
| A | **Time**: each head's delay | **Reverse**: each head forwards or backwards |
| B | **Volume**: each head's level | |
| C | **Pan**: each head's position | **Width**: each head's stereo width |
| D | **Feedback**: how much of each head goes back onto the tape | **FX**: filter and tape (see below) |

| Gesture | What it does |
|---|---|
| Short press | Chooses the page. Press the same button again for its second page |
| Long press D (½ s) | Freeze on or off |
| Hold A and move a fader | Loads card preset 1-8 (the fader's number). Its LED flashes three times |
| Hold D and move a fader | Saves the current settings as card preset 1-8. Its LED blinks fast |

Faders **pick up**: after a page change, a fader does nothing until you move
it to (or past) the stored value, so nothing jumps. The 8mu's LEDs show the
stored values on the current page. Tilting the 8mu forwards or back bends
every head's time by up to an octave, past a dead zone of about 15°, like a
hand on the reel.

### Reverse page (A, pressed twice)

A fader in the top half plays its head **backwards**; in the bottom half,
forwards. A reversed head plays the tape back in slices as long as the head's
time, each slice backwards, arriving on average about one head-time later.
That's the classic reverse-delay sound. A short fade (4 ms) at each slice
boundary hides the join. Wow and flutter don't apply to reversed heads.

Presets saved before the Reverse page existed load with every head
forwards.

### FX page (D, pressed twice)

| Fader | Setting |
|---|---|
| 1 | **Low-pass** in the feedback path, 150 Hz to 16 kHz, off at the top |
| 2 | **High-pass** in the feedback path, off at the bottom, then 20 Hz to 2 kHz |
| 3 | **Saturation**: tape drive on the way onto the tape (×1 to ×4, with the level brought back down, so loud signals get squashed and quiet ones pass at the same level) |
| 4 | **Hiss**: tape noise, recorded with the feedback so it builds up over the repeats |
| 5 | **Wow**: a slow, drifting pitch wobble (up to ±10 ms) |
| 6 | **Flutter**: a fast pitch wobble (up to ±0.4 ms) |
| 7 | **Dropouts**: random moments where the tape loses level, recorded onto the tape |
| 8 | **Mix**: dry only (down), both at full level (centre), wet only (up) |

## Time

**Free** (nothing in Pulse In 1, no tap tempo): each fader sets its head from
0 to 0.5 s, and **Main** multiplies all of them by ×¼ (fully down) to ×4
(fully up), with ×1 at the centre. The longest delay is about 2.2 s.

**Synced** (a jack in Pulse In 1, or after tapping a tempo): each head is a
whole number of sixteenths of a bar of four, from 1/16 (fader down) to a
whole bar (fader up), with Pulse In 1 taken as quarter notes. Main chooses a
ratio in nine equal zones: /4, /3, /2, ×⅔, **×1** (centre), ×1½, ×2, ×3,
×4. A time longer than the tape is halved until it fits.

Synced mode starts as soon as a jack is in Pulse In 1, detected with the
Computer's normalisation probe. The tempo follows the clock from its second
pulse; until then it uses the last tempo it had (120 bpm at power-up).

## Panel

| Control | Function |
|---|---|
| Main | Time multiplier (free) or clock ratio (synced) |
| X | Volume and feedback multiplier, ×0 to ×2, ×1 at the centre |
| Y | Pan and width multiplier, ×0 (every head mono and centred) to ×2 |
| Switch up | **Freeze**: the input is cut and the tape loops the longest head that's playing |
| Switch middle | Run |
| Switch down | **Tap tempo** (momentary). Two taps up to 3 s apart set the beat and sync the heads to it. Hold it down for 1.5 s to go back to free time. While a jack is in Pulse In 1, the clock takes priority and taps are ignored |

| Jack | Function |
|---|---|
| Audio In 1, 2 | Left and right in. With only Audio In 1 patched, it feeds both sides (mono in) |
| CV In 1 | Added to X: +5 V adds ×1 to every head's volume and feedback |
| CV In 2 | Added to Y: +5 V adds ×1 to every head's pan and width |
| Pulse In 1 | Clock, one pulse a beat. Plugging a jack in syncs the heads |
| Pulse In 2 | Freeze while high |
| Audio Out 1, 2 | Left and right out |
| CV Out 1 | The tape's wow and flutter, ±5 V at full depth |
| CV Out 2 | Envelope of the delayed signal, 0 to 5 V |
| Pulse Out 1 | A trigger every beat (synced), or every free-time length when free |
| Pulse Out 2 | High while frozen |

| LED | Shows |
|---|---|
| 0-3 | With an 8mu or the editor connected: the page's button (A-D), blinking for its second page. Without one: beat, synced, left level, right level |
| 4 | Input level; fully lit while frozen |
| 5 | 8mu or editor connected; blinks when a preset is saved or loaded |

## Presets

**On the card:** there are eight presets in the program card's flash (its
last 4 kB sector, which every card has, so a 2 MB card is enough). Save and
load them with the 8mu alone (hold D or A and move a fader), or from the web
editor. At power-up the card loads whichever preset you last saved or
loaded.

**In the browser:** the web editor keeps as many presets as you like,
exports and imports them as JSON, copies any of them into a card slot, and
backs up the card's eight presets.

## Web editor

Open `web/index.html` in Chrome or Edge, plug a computer into the Computer's
USB socket, power-cycle the module, then press **Connect card & 8mu**. The
editor draws the tape with each head at its live delay, shows the tempo and
the knobs, and lets you edit every page, freeze and tap. An 8mu plugged into
the computer is passed on to the card through the editor.

The card chooses its USB mode once, at power-up: if the port is supplying
power (an 8mu, or nothing yet), it's a USB host for the 8mu; if a computer is
plugged in, it's a USB MIDI device called **EightTrack** for the editor. The
card runs on its own either way. The protocol is documented in `sysex.h`.

## Tips

- **Ping-pong:** give two heads a width of 0 (so each takes the mono sum),
  pan one hard left and one hard right, and give the right-hand one some
  feedback. The built-in *Ping-pong* preset in the editor does this.
- **Runaway:** with X above the centre, feedback can go past 100%, and the
  tape then settles into its own saturation rather than clipping hard. Pull
  the low-pass down to tame it.
- **Freeze, then play the heads:** freeze the tape, then move the Time faders
  or Main: the heads slide around the frozen loop with tape-style pitch bends.

## How it works

- The tape is a buffer in RAM: stereo, 12 bits a sample, at 24 kHz (about
  2.2 s). Recording at half the output rate is what makes the tape long
  enough, and it gives the echoes a slightly darker sound than the dry
  signal, as on real tape.
- Heads read between tape frames with linear interpolation, and each head's
  delay glides (a one-pole of about 80 ms, limited to 1.5× tape speed), which
  produces the pitch bends.
- All the DSP is fixed-point, and the per-head control work is spread across
  the 32-sample control period so that no single sample has too much to do.
- A 16 MB card doesn't help here. Flash is far too slow to write a delay line
  to in real time, so the tape has to live in the RP2040's 264 kB of RAM,
  which limits it to about 2.2 s. Presets use flash, but they only need 4 kB.

## Building

```
mkdir build && cd build
cmake .. && make
```

Needs the Pico SDK (built and tested with 2.1.1). `EightMU.h` and
`ComputerCard.h` are copied from `Demonstrations+HelloWorlds/PicoSDK/ComputerCard`.
The binary runs from RAM (`copy_to_ram`), so the USB core can write presets
to flash while the audio core keeps running.
