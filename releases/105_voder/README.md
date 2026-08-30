# Voder

*A voice you play with your hands.*

A program card for the Music Thing Modular Workshop System Computer, based
on the **Voder** — the filter and formant based voice-synthesis machine
demonstrated at the 1939 New York World's Fair, invented by Homer Dudley at
Bell Labs.

Play it from the panel, or from a [Music Thing
8mu](https://www.musicthing.co.uk/8mu.html) over USB MIDI Host.

## What it does

- **Two excitation sources.** A bandlimited buzz at F0 for voiced sounds,
  white noise for sibilants.
- **Eight fixed bandpass filters** at 250, 450, 700, 1000, 1400, 1900, 2600
  and 3800 Hz.
- **Plosive bursts** for the stop consonants the original had dedicated keys
  for.
- **A vowel cube** you steer with two or three controls.


## Two modes

| | How to get it | What it is | Boot LEDs |
|---|---|---|---|
| **MAIN BOOT** | Just turn the card on | **BABBLE** — every knob drives several things at once, so it talks from a single gate with nothing else attached | **even** (0, 2, 4) |
| **ALT BOOT** | **Hold the switch down** at power-on | **Deliberate** — one knob, one parameter | **odd** (1, 3, 5) |

---

## MAIN BOOT — BABBLE

### Page 1 — *what is said* (switch middle or down)

| Knob | Controls |
|---|---|
| **Main** | Vowel diagonal **and** pitch — one sweep walks through vowels, rising as it goes |
| **X** | Chatter rate **and** brightness — clockwise is faster and brighter |
| **Y** | Breath **against** rounding — a rounded hum at one end, a breathy whisper at the other |

### Page 2 — *who is saying it* (switch up)

| Knob | Controls |
|---|---|
| **Main** | **Voice size** — pitch, brightness and vowel placement together, from a small bright voice to a large dark one |
| **X** | **Animation** — how much the voice wanders between syllables |
| **Y** | **Consonants** — how many syllables get a plosive, and how loud |

Set a voice on page 2, then play it on page 1.

### Talking

Hold a gate on either pulse input and it chatters, 2 Hz to 20 Hz across
Knob X. Each syllable is voiced for three quarters of its period, which is
roughly what connected speech does — the silences in speech are the stop
closures, not gaps between notes.

### Auto-chatter

**Hold the momentary switch for two seconds** and it starts talking by
itself, no gate needed. The LEDs fill as a progress bar while you hold.
**Tap the switch to stop.**

It generates phrases rather than an even stream. Some syllables get a
consonant, most do not. LED 5 pulses with each syllable.

---

## ALT BOOT

*Hold the switch down while powering on to reach this mode.*

| Switch | Knob 1 (Main) | Knob 2 (X) | Knob 3 (Y) |
|---|---|---|---|
| **Middle / Down** | Openness | Front | Breath |
| **Up** | Pitch | Brightness | Rounding |

Between the two pages the panel reaches every parameter the 8mu can, so the
card is fully playable on its own.

**Unplug the 8mu and the knobs take over again.** Each control follows the
controller only while it is connected *and* has actually been moved — so an
idle 8mu never seizes a knob under your hand, and pulling the cable
mid-session hands everything straight back to the panel.

---

## With an 8mu

Plug it into the front USB-C jack. Nothing to configure — the card reads the
8mu's factory mapping.

| 8mu control | Factory message | Effect |
|---|---|---|
| **Fader 1** | CC 34 | **Openness** — close ↔ open (vowel F1) |
| **Fader 8** | CC 41 | **Front** — back ↔ front (vowel F2) |
| Fader 2 | CC 35 | Click decay — short hit → steady tone |
| Fader 3 | CC 36 | Breath — buzz ↔ noise |
| Fader 4 | CC 37 | Pitch — F0, 50–500 Hz |
| Fader 5 | CC 38 | Brightness — spectral tilt |
| Fader 6 | CC 39 | Click level |
| **Fader 7** | CC 40 | **Volume** — base level |
| Lift back / front | CC 43 / 42 | **Volume** — back swells, front ducks *(deliberate mode only)* |
| Lift left / right | CC 44 / 45 | **Rounding** — the third vowel axis |
| **Button 1** | Note 36 (C2) | **Mute** while held |
| **Button 2** | Note 48 (C3) | **Gate** — sustains the voice while held, and adds breath - the main PERFORMANCE button in Main boot mode |
| **Button 3** | Note 60 (C4) | Plosive burst |
| **Button 4** | Note 72 (C5) | **Freeze** the formants while held, and adds breath |


**Button 2 is a gate.** Holding it opens the voice and keeps it open, so the
card can be played from the 8mu alone with nothing in the jacks. Unlike a
gate patched into Pulse In 2 - a finger on a button is one sustained syllable.

**Button 4 freezes while held, and every LED goes to half brightness while
it does.** Freeze stops openness, front, rounding and the vowel morph all at
once, so a frozen card looks exactly like a broken one; the flat even glow
is unlike any other display on the card and says which it is. Button 1's
mute dims all six further still.

**Buttons 2 and 4 also add breath** — about a seventh of the range each, on
top of wherever the breath fader is parked. Both buttons already did something
and neither used its held state for anything else, so the gesture is free:
press one over a voiced sound and it goes breathy without losing the pitch.
It is an addition, not a setting, so it layers on the fader rather than
replacing it.

### The vowel cube

Three axes, and they are the three things a mouth actually does:

```
              ROUND = 0 (spread)          ROUND = full
      CLOSE    OO ---------- EE            OOr -------- EEr
                |            |              |            |
      OPEN     AH ---------- EH            AHr -------- EHr
              back        front           back        front
```

**Openness** is how far the jaw opens (F1). **Front** is where the tongue
sits (F2). **Rounding** is lip protrusion — it lowers F2 hard and F1 a
little, which is why it is a genuinely separate direction. OH lives on the
rounded face and is unreachable without it.

The vowel axes are on **faders 1 and 8**, the outermost pair, so the two
controls you play constantly span the hand.

### Volume

8mu Fader 7 sets the base level, and **lifting the back or front of the
controller swings a full scale either side of it**. Put the fader halfway
and you have a full swell above and a full duck below.

The accelerometer reports **lift** gestures — zero when level — so a
controller lying flat sits exactly where the fader says. The response is
squared, so a quarter lift costs only 0.6 dB: holding it roughly level is
fine.

**In main boot mode the tilt does not touch the volume at all** — fader 7 alone sets
it. The assumption in the main mode is that you want sound, and a card that
can be silenced by holding the controller at the wrong angle is working
against its player.

---

## Jacks

| Jack | Function |
|---|---|
| Audio In 1 | **External exciter** — summed with the internal buzz and noise |
| Audio In 2 | **Volume CV** — swells and ducks around the base level |
| CV In 1 | **Pitch**, 1V/oct |
| CV In 2 | **Formant CV**, bipolar — sweeps the vowel cube diagonally |
| Pulse In 1 | **Click** trigger |
| Pulse In 2 | **Glottal gate** |
| Audio Out 1 / 2 | Mono output, same signal on both |
| CV Out 1 | Formant energy envelope |
| CV Out 2 | Vowel position — the openness axis, 0–5 V |
| Pulse Out 1 | High while voiced |
| Pulse Out 2 | High while frozen |

### Feeding it random voltages

**Every CV adds to its control rather than replacing it**, so a random
voltage wanders the sound around wherever the knobs are parked.

**CV In 2 is the one to reach for first.** It moves openness up as front
moves down, sweeping the cube along its diagonal, which crosses the middle
where the distinct vowels live. A sample-and-hold here works well.

**Gates chatter without clicking.** Pulse In 2 opens fully up to about
200 Hz and above that becomes amplitude modulation rather than clicks.

A good first patch: slow random into CV In , a clock divided a few ways
into both pulse inputs, and an envelope into Audio In 2.

**Audio In 1 sums, it does not replace.** Patch a drum loop in and it goes
through the same eight bands the card is speaking with — so you can talk
over it. It comes in level with the internal sources, so set the balance at
the source.

---

## Breath is a ratio

Breath changes how *breathy* the voice is.

## The plosive

The click is a short bandpassed burst, roughly 600–2800 Hz.

## Building

```bash
cmake -B build -G Ninja
cmake --build build
```

Drag `build/tract8.uf2` onto the Pico in bootloader mode. A released binary
is committed at `UF2/tract8.uf2`.

Twelve host-side test scripts live in `tools/` and need `numpy`; run them
individually. `docs/BRINGUP.md` is the hardware checklist, and names the
fault each section guards against.

## Credits

- **Homer Dudley** and Bell Labs, for the Voder, 1939.
- **rppicomidi** — the `usb_midi_host` driver, MIT, vendored unmodified.
- **Chris Johnson** — the ComputerCard library and its `midi_host` example.
- **Tom Whitwell / Music Thing Modular** — the Workshop System and the 8mu.

## License

MIT. See `LICENSE`, which records the vendored components and their terms.
