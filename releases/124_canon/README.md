Play a melody into it from a sequencer and it comes back as a canon: delayed,
tempo-scaled, inverted (strict or tonal) and octave-shifted copies of your line.

Everything is set up on the **web editor** and stored as **12 presets** on the
card. On the card you just play, stop and choose a preset.

---

## Basics

**Patch:** sequencer pitch → **CV In 1**, sequencer gate → **Pulse In 1**. A keyboard
works too (pitch → CV In 1, gate → Pulse In 1): set Pulse In 1 to *Keyboard gate* in the
editor (below).

| Output                 | Voice                                     |
| ---------------------- | ----------------------------------------- |
| CV Out 1 + Pulse Out 1 | Voice 1 (the input line as direct output) |
| CV Out 2 + Pulse Out 2 | Voice 2                                   |
| Audio Out 1            | Voice 3 (built-in synth)                  |
| Audio Out 2            | Voice 4 (built-in synth)                  |

**Z switch:** Up = play · Middle = stop · Down = choose a preset (only works while stopped).

**Choosing a preset:** stop (Z Middle), then hold **Z Down** and turn **Main**.
The LEDs show the preset; let go of Z to keep it. Main does nothing until you
turn it, so holding Z on its own changes nothing. X and Y aren't used.

**Reading the LEDs** — the 12 presets are shown like this:

- Presets 1–6: **one LED lit** (LED 1 … LED 6).
- Presets 7–12: **all lit except one** (not 1 … not 6).

**At power-on** the LEDs show the active preset, then the firmware version in
binary (LED 1 = 1, LED 2 = 2, LED 3 = 4, LED 4 = 8, LED 5 = 16; v0.23 = LEDs 1 + 2 + 3 + 5).

**While stopped:** the LEDs show the active preset, and all outputs follow the input.
**While playing:** LED 1–4 flash when Voice 1–4 plays a note; LED 5 flashes
on each input pulse; LED 6 stays on while a clock is coming in.

## Web editor

1. Connect the card to your computer by USB and open the editor in **Chrome or Edge**.
2. Click **Connect to card** and allow MIDI (including SysEx) when the browser asks.
3. Click a preset to edit it. Changes are sent to the card straight away and
   stored in its memory once the card is stopped, so editing never interrupts playing.
4. **Make active on card** switches the card to that preset (only while it's stopped).

The editor also sets the **internal tempo** (used when nothing is patched into
Pulse In 1, or when it's a keyboard gate), saves and loads all 12 presets as a file, and
restores the factory presets.

**Pulse In 1** can be set in the editor (it applies from the next Play):

- **Clock** (default) — for a sequencer: each pulse is a step, and the pitch is read on
  each pulse. BEAT and STEP delays count pulses.
- **Keyboard gate** — for a keyboard such as a KeyStep: each key you press is a note
  (even the same key again), and letting go is a rest. In STEP each key is a step (the
  chord moves on with every key). In BEAT, TEMPO and FREE the delays are timed from the
  internal tempo, starting from the first key, so you can play freely.

**Input tuning** is centred automatically. Keyboards and sequencers put out exact
semitones, so the card measures how far each held note sits from the nearest semitone
and slowly takes that difference off (up to 40 cents). A source whose CV sits about half
way between this card's semitones would otherwise flicker between two notes. The editor
shows the correction; if it is large, recalibrating CV In 1 (or the source's own CV
tuning) is the better fix. It starts again from zero after a calibration.

Each preset holds:

- **Mode** — BEAT, FREE, STEP, TEMPO or HETEROPHONY (below), and that mode's settings.
- **Inverted voices** — any of Voices 2–4 (Voice 1 is never inverted).
- **Inversion type** — strict, tonal following the melody's key, or tonal in a key you set.
- **Octave** for each voice, −3 to +3.
- **Name**, up to 12 characters.

## The Score

While the editor is connected, the bottom of the page writes out what the card
plays, live: four staves, one per voice, with inverted voices in red.

- **Metric** notation (the default for BEAT, STEP and TEMPO) puts the notes on the beat
  grid from Pulse In 1, with rests, ties, beams and accidentals. Set what one step
  is (crotchet, quaver or semiquaver), the time signature (2/4 to 12/8), and how finely to
  round the rhythm. *Note lengths* can follow the step (clean notation) or the
  actual gate lengths. Notes and rests are grouped by the beat, the way they'd be
  printed (for example a dotted-crotchet rest in 6/8, and no rest hiding the middle
  of a 4/4 bar). The bar being played is tidied up once each beat is complete.
- **Tempo canons** are written the way Nancarrow writes them: each voice exactly like
  Voice 1 (same time signature, same note values) but at **its own tempo**, marked at the
  start of its stave (e.g. ♩ = 150 for a voice at 5:4 against Voice 1 at ♩ = 120). Each
  voice has its own bar lines, staggered against the others, and the page is laid out in
  real time so notes played together line up. The spacing is set by the fastest voice, so
  a voice at 2:1 or 3:1 isn't squashed (the page shows fewer of Voice 1's bars instead).
- **Heterophony** in metric notation isn't rounded to a grid: each note keeps its real
  timing, and each beat is written in whichever division fits it best — plain values, or a
  tuplet (3, 5, 6, 7…) where that is closer — weighing accuracy in milliseconds against
  simplicity (typically within about 10 ms, 35 ms at worst). A beat can be rewritten as more
  notes arrive and its context becomes clear; the bar being played is filled in once each
  beat is complete. The *Rhythm to the nearest* setting is hidden in this mode.
- **Proportional** notation (the default for FREE and HETEROPHONY) places each
  note at its time, with a line for how long it sounds, and faint step and gold
  bar lines behind.
- **Key**: accidentals are spelled from the key the notes are in (up to four
  sharps or flats), or written without a key signature.
- **Pause** to scroll back through the take while it plays; when it stops (Z Middle, or a
  CONVERGE canon finishing) the slider is ready to drag back through the whole take; **Save MIDI** (one track per voice,
  real timing) or **Save MusicXML** (the metric score, for Sibelius, Dorico or
  MuseScore). Notation programs need one tempo and shared bar lines, so in a tempo canon
  the MusicXML is written at Voice 1's tempo and bar lines, with each voice in tuplets
  from its speed (e.g. 5:4 semiquavers in each crotchet for a 5:4 voice; a 4:3 voice comes
  out in dotted values, a 3:2 voice in triplets). It plays back correctly; the staggered
  layout stays in the editor.

Each Play starts a new take. Whenever the card is connected by USB, each voice's
notes are also sent as ordinary MIDI on channels 1–4 (the port is called
"Canon"), so a DAW can record the canon, with or without the editor open.

The metric score needs an internet connection the first time to load its
notation library (VexFlow); the proportional view works without it.

## Modes

### 1. BEAT

Voices 2–4 come in a set number of steps after Voice 1, locked to the input
pulses. Rests are kept.

Set each voice's delay as a count of whole, ½, ⅓ or ¼ steps. For example,
4 × ½ step = 2 steps behind Voice 1.

### 2. FREE

The same idea as BEAT, but measured in time, not steps. The voices
don't follow the beat grid. Each voice's delay is set in seconds.

### 3. STEP

All voices play together on Voice 1's rhythm. Each plays the pitch from a set
number of notes earlier, producing cycling chords derived from the melody.

### 4. TEMPO CANON

A tempo canon: each voice plays the line at its own constant tempo causing the
voices to converge or drift apart. Each voice's speed is a ratio N:D against
Voice 1 (for example 4:5); the editor can fill in the ratio sets from earlier versions.

- **DIVERGE:** all voices start together on the first note; V2–V4 are slower
  (N/D of Voice 1's speed) and drift further apart the longer it plays.
- **CONVERGE at N steps:** V2–V4 are faster (D/N). They come in one by one, catch up
  and land on Voice 1's note at step N: Voice 1 plays its step-N note and all four
  voices hold it together, then everything stops on the next note. Z Middle, then Up, to play it again. N can be anything from 4 to 2048 steps.

### 5. HETEROPHONY

Every note is played by all four voices, each a little early or late within a
time window (0–4000 ms). Each voice's timing drifts smoothly from note to
note, so the voices start in unison and wander apart and back.

## Inversion

The mirror point is the first note after Play. Voice 1 is never inverted.

| Type | What it does |
| --- | --- |
| **Strict** | Exact mirror by semitones: C D E → C B♭ A♭. This usually takes the voice out of the key. |
| **Tonal — follow the melody's key** | Mirrors by scale steps in the key the line is playing in, worked out from its notes: C D E in C major → C B A. Works from the first few notes; an ambiguous opening (e.g. just A B) may take a note or two to settle. |
| **Tonal — set key** | Mirrors by scale steps in the key and scale you choose (major, minor, modes, pentatonic, whole tone). |

Tip: an inverted voice sits below the line — set its octave to +1 to bring it
back up.

## Factory presets

| Preset | LEDs | Name | Settings |
| --- | --- | --- | --- |
| 1 | LED 1 | Beat 2-4-6 | BEAT, 2-4-6 steps |
| 2 | LED 2 | Beat 1-2-3 | BEAT, 1-2-3 steps |
| 3 | LED 3 | Beat inv 2+4 | BEAT, 2-4-6 steps, V2 + V4 inverted (tonal) and up an octave |
| 4 | LED 4 | Beat 4-8-12 | BEAT, 4-8-12 steps |
| 5 | LED 5 | Free 1-2-3 s | FREE, 1-2-3 s |
| 6 | LED 6 | Free .5-1.5s | FREE, 0.5-1-1.5 s |
| 7 | not 1 | Step 2-4-6 | STEP, 2-4-6 notes back |
| 8 | not 2 | Step 1-2-3 | STEP, 1-2-3 notes back |
| 9 | not 3 | Diverge | TEMPO, diverge, 4:5 3:4 2:3 |
| 10 | not 4 | Converge 64 | TEMPO, converge at 64 steps, 4:5 3:4 2:3 |
| 11 | not 5 | Converge 128 | TEMPO, converge at 128 steps, 2:3 1:2 1:3 |
| 12 | not 6 | Heterophony | HETEROPHONY, 500 ms window |

---

## If a keyboard or sequencer reads a semitone out, or wobbles

The card reads pitch in absolute terms, so the source's 0 V has to be the Workshop
System's 0 V. If the source and the Workshop System share a power supply in the wrong way —
for example both on one multi-port USB charger, with the Workshop System on a USB step-up
cable — the step-up's current shifts the source's ground by a few tens of millivolts and
adds switching noise. Notes then arrive flat (C reads as B) and waver between two
semitones, even though they sound in tune going straight into an oscillator.

Fix it on the power side: give the keyboard its own supply (a different charger, a
computer's USB port, or a power bank), or use a USB isolator on its cable. Sources with their
own battery, such as an OXI One, aren't affected.

## Calibrating CV In 1

If you get some tuning issues between the voices, this process will calibrate the input.

1. Patch **CV Out 1 → CV In 1**.
2. Hold **Z Down** and power on; keep holding until the LED bar starts.
3. The card steps CV Out 1 through −4 V … +4 V (about 4 s, LED bar filling).
4. All six LEDs lit = saved to flash. LEDs 1 + 6 blinking = failed (check the patch) — the previous calibration is kept.

Repatch your sequencer afterwards.

## Updating

v0.23 adds the *Keyboard gate* setting for Pulse In 1 and centres the input tuning
automatically. Presets and calibration are kept; Pulse In 1 starts as *Clock*.

v0.22 fixed the end of a CONVERGE canon: Voice 1 now plays its note at the meeting step
and the other voices land on that note (before, they took the note before it and Voice 1's
meeting note was cut off). Presets and calibration are kept.

v0.21 renamed HETERO to HETEROPHONY. A card that already has presets keeps the old name
for preset 12 ("Hetero 500ms") until you rename it or restore the factory presets. v0.20
added the live score.

### From v0.18

v0.19 stores its settings differently: the first time it starts, the card loads
the 12 factory presets (calibration is kept). The mode is no longer chosen with
Main at power-on — it's part of each preset.
