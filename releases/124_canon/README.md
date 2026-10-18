Play a melody into it from a sequencer and it comes back as a canon: delayed,
tempo-scaled, inverted (strict or tonal) and octave-shifted copies of your line.

Pick the mode with Main at power-on. Everything else is set with Z held Down
while stopped, and saved when you let go.

---

## Basics

**Patch:** sequencer pitch → **CV In 1**, sequencer gate → **Pulse In 1**.

| Output                 | Voice                                     |
| ---------------------- | ----------------------------------------- |
| CV Out 1 + Pulse Out 1 | Voice 1 (the input line as direct output) |
| CV Out 2 + Pulse Out 2 | Voice 2                                   |
| Audio Out 1            | Voice 3 (built-in synth)                  |
| Audio Out 2            | Voice 4 (built-in synth)                  |

**Mode:** set Main to a panel mark, then power on.
1 - BEAT · 
2 - FREE
3 - STEP
4 - TEMPO
5/6 - HETEROPHONY.

At power-on the LEDs show the mode (LED 1–5), then the firmware version in
binary (LED 1 = 1, LED 2 = 2, LED 3 = 4, LED 4 = 8, LED 5 = 16; v0.16 = LED 5 alone).

**Z switch:** Up = play · Middle = stop · Down = settings (only works while stopped).

**Reading the LEDs in settings** — every setting is a position on a 12-step scale:

- Steps 1–6: **one LED lit** (LED 1 … LED 6).
- Steps 7–12: **all lit except one** ("not 1" … "not 6").

**LEDs while playing:** LED 1–4 flash when Voice 1–4 plays a note; LED 5 flashes
on each input pulse; LED 6 stays on while a clock is coming in.
**While stopped:** all outputs follow the input and the LEDs show its note name
(C = LED 1 … F = LED 6, F♯ = not 1 … B = not 6).

## Changing settings

Stop first (Z Middle). There are only three things you do with Z:

| Do this                                                               | Gets you                                                     |
| --------------------------------------------------------------------- | ------------------------------------------------------------ |
| **Hold Z Down and turn X, Main or Y** (immediately)                   | Page 1 — the mode's own settings (below). Release Z = saved. |
| **Tap Z Down quickly** (no knob)                                      | Cycles through inversion patterns (see *In every mode*).     |
| **Hold Z Down 1 second** (no knob) until the LEDs flash, keep holding | Page 2 — voice octaves and strict/tonal. Release Z = saved.  |
The LEDs always show the knob you turned last. Each mode remembers its own settings.

## 1. BEAT

Voices 2–4 come in a set number of steps after Voice 1, locked to the input
pulses. Rests are kept.

| Knob (Z held) | Sets | LEDs |
| --- | --- | --- |
| X | Delay pattern for V2-V3-V4 | LED 2 = 2-4-6 steps (default). See *Lookup tables*. |
| Main | Step size | LED 1 ¼ · LED 2 ⅓ · LED 3 ½ · **LED 4 = 1 step** (default) · LED 5 ×2 · LED 6 ×3 · not 1 ×4 |
| Y | Octave of Voices 3+4 | LED 1 −3 · LED 2 −2 · LED 3 −1 · **LED 4 = 0** · LED 5 +1 |

Example: X = LED 2, Main = LED 4 → V2 2 steps behind, V3 4, V4 6.

## 2. FREE

The same delay patterns as BEAT, but measured in time, not steps. The voices
don't follow the beat grid.

| Knob (Z held) | Sets | LEDs |
| --- | --- | --- |
| X | Delay pattern for V2-V3-V4 | LED 2 = 2-4-6 (default). See *Lookup tables*. |
| Main | Time of one unit | LED 1 100 ms · LED 2 150 · LED 3 200 · LED 4 250 · LED 5 300 · LED 6 400 · **not 1 = 500 ms** (default) · not 2 600 · not 3 750 · not 4 1 s · not 5 1.5 s · not 6 2 s |
| Y | Octave of Voices 3+4 | **LED 4 = 0** (as BEAT) |

Example: X = LED 2, Main = not 1 → V2 1s behind, V3 2s, V4 3s.

## 3. STEP

All voices play together on Voice 1's rhythm. Each plays the pitch from a set
number of notes earlier, producing cycxling chords derived from the melody.

| Knob (Z held) | Sets | LEDs |
| --- | --- | --- |
| X | How many notes back for V2-V3-V4 | LED 2 = 2-4-6 notes (default). See *Lookup tables*. |
| Main | — (not used) | |
| Y | Octave of Voices 3+4 | **LED 4 = 0** (as BEAT) |

## 4. TEMPO CANON

A tempo canon: each voice plays the line at its own constant tempo causing the voices to converge or drift apart.

| Knob (Z held) | Sets                      | LEDs                                                                                                               |
| ------------- | ------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| X             | Tempo ratios for V2-V3-V4 | **LED 4 = 4:5, 3:4, 2:3** (default); LED 1 = gentlest. See *Lookup tables*.                                        |
| Main          | Diverge or converge       | **LED 1 = DIVERGE** (default) · converge at: LED 2 64 steps · LED 3 96 · LED 4 128 · LED 5 192 · LED 6 256 · not 1 384 · not 2 512 · not 3 768 · not 4 1024 · not 5 1536 · not 6 2048 |
| Y             | Octave of Voices 3+4      | **LED 4 = 0** (as BEAT)                                                                                            |

- **DIVERGE:** all voices start together on the first note; V2–V4 are slower
  and drift further apart the longer it plays.
- **CONVERGE at N steps:** V2–V4 are faster. They come in one by one, catch up
  and land on Voice 1's note at step N, hold it, then everything stops on the
  next note. Z Middle, then Up, to play it again.

## 5. HETEROPHONY

Every note is played by all four voices, each a little early or late within a
time window (heterophony). Each voice's timing drifts smoothly from note to
note, so the voices start in unison and wander apart and back.

| Knob (Z held) | Sets | LEDs |
| --- | --- | --- |
| X | Width of the spread | LED 1 0 ms (unison) · LED 2 50 · LED 3 100 · LED 4 200 · LED 5 300 · **LED 6 = 500 ms** (default) · not 1 750 · not 2 1 s · not 3 1.5 s · not 4 2 s · not 5 3 s · not 6 4 s |
| Main | — (not used) | |
| Y | Octave of Voices 3+4 | **LED 4 = 0** (as BEAT) |

## In every mode

**Inversion — tap Z quickly.** Each tap moves on one pattern. The LEDs show it
for 1.5 s: LED 1 is always on (a marker); LEDs 2, 3, 4 lit = Voices 2, 3, 4
inverted. To just look without changing it, hold Z half a second and let go.

| Tap | Inverted | LEDs lit |
| --- | --- | --- |
| (start) | none | 1 |
| 1 | V2 | 1 2 |
| 2 | V3 | 1 3 |
| 3 | V4 | 1 4 |
| 4 | V2 + V4 | 1 2 4 |
| 5 | V3 + V4 | 1 3 4 |
| 6 | V2 + V3 | 1 2 3 |
| 7 | V2 + V3 + V4 | 1 2 3 4 |

The mirror point is the first note after Play. Voice 1 is never inverted.

**Page 2 — hold Z 1s until the LEDs flash, keep holding:**

(Page 1 settings for each mode are noted above)

| Knob | Sets                | LEDs                                                                                                          |
| ---- | ------------------- | ------------------------------------------------------------------------------------------------------------- |
| Y    | Which voice to edit | LED 1–4 = V1–V4                                                                                               |
| X    | That voice's octave | LED 1 −2 · LED 2 −1 · **LED 3 = 0** · LED 4 +1 · LED 5 +2                                                     |
| Main | Inversion type      | **LED 1 = strict** (exact mirror: C D E → C B♭ A♭) · LED 2 = tonal (within the scale you play: C D E → C B A) |

To set several voices: turn Y to a voice, set X, turn Y to the next voice, set X
again. Tip: an inverted voice sits below the line — set it to +1 to bring it
back up.

Tonal inversion learns the scale from the pitch classes of the last 32 notes
played (kept across Stop/Play). Straight after power-on it only knows the first
few notes, so let a phrase that uses the whole scale play through once.

## Lookup tables

**Delay patterns** (X in BEAT, FREE, STEP) — how far behind Voice 1 each voice
is, in steps (BEAT), units (FREE) or notes (STEP):

| LEDs | V2-V3-V4 | | LEDs | V2-V3-V4 |
| --- | --- | --- | --- | --- |
| LED 1 | 1-2-3 | | not 1 | 1-2-4 |
| **LED 2** | **2-4-6** (default) | | not 2 | 2-4-8 |
| LED 3 | 3-6-9 | | not 3 | 2-3-5 |
| LED 4 | 4-8-12 | | not 4 | 3-5-8 |
| LED 5 | 5-10-15 | | not 5 | 1-3-6 |
| LED 6 | 6-12-18 | | not 6 | 3-5-6 |

**Tempo ratios** (X in TEMPO) — V2, V3, V4 speed against Voice 1 (in CONVERGE
they run the inverse, e.g. 5:4):

| LEDs | V2, V3, V4 | | LEDs | V2, V3, V4 |
| --- | --- | --- | --- | --- |
| LED 1 | 15:16, 7:8, 13:16 (gentlest) | | not 1 | 9:10, 4:5, 7:10 |
| LED 2 | 11:12, 5:6, 3:4 | | not 2 | 3:4, 1:2, 1:4 |
| LED 3 | 7:8, 3:4, 5:8 | | not 3 | 2:3, 1:2, 1:3 |
| **LED 4** | **4:5, 3:4, 2:3** (default) | | not 4 | 1:2, 1:3, 1:4 |
| LED 5 | 5:6, 2:3, 1:2 | | not 5 | 1:2, 1:4, 1:8 |
| LED 6 | 4:5, 3:5, 2:5 | | not 6 | 3:4, 9:16, 27:64 |

---

## Calibrating CV In 1

If you get some tuning issues between the voices, this process wil calibrate four voices.

1. Patch **CV Out 1 → CV In 1**.
2. Hold **Z Down** and power on; keep holding until the LED bar starts.
3. The card steps CV Out 1 through −4 V … +4 V (about 4 s, LED bar filling).
4. All six LEDs lit = saved to flash. LEDs 1 + 6 blinking = failed (check the patch) — the previous calibration is kept.

Repatch your sequencer afterwards.


