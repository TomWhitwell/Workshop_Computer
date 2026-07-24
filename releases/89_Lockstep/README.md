# Lockstep

**Dual quantized pitch-mover for the Music Thing Modular Workshop Computer.**

Two CV outputs wander together through a scale. You tune two oscillators by hand to
set the interval; Lockstep walks them in **parallel**, so the harmony you dialled in
moves as one — clocked, quantized, and always in key. One knob sets how *often* it
moves, another how *far*. The Main knob is a resonant low-pass filter that processes
the two oscillators returned through the audio inputs, so the card is a complete
generative two-voice instrument on its own.

---

## Why this card exists

It came out of a long dead-end: driving oscillators from another card's bassline that
output pitch CV in *negative* territory, which some oscillator inputs (a SineSquare's
Pitch jack, for one) simply won't track. Lockstep is built the opposite way — its walk
is **centered high (around MIDI 72) and floored so the CV never drops below ~0 V**. It
drives positive-only pitch inputs cleanly, and you tune the oscillators down to taste.

---

## I/O

| Jack | Direction | Function |
|------|-----------|----------|
| **Pulse In 1** | Input | Clock. Each rising edge can advance the walk (see X knob for division). Disconnected → internal free-running clock. |
| **Pulse In 2** | Input | Re-seed. A rising edge jumps the walk to a fresh pattern (and recenters it). |
| **CV Out 1** | Output | Voice 1 pitch CV. V/Oct, hardware-calibrated, quantized to the scale. |
| **CV Out 2** | Output | Voice 2 pitch CV. Follows CV Out 1 (parallel motion), except when it takes a mid-beat passing move (see **Doubling** below). |
| **Pulse Out 1** | Output | Gate (~10 ms) on every step where voice 1's quantized note changes. Drive an external envelope for voice 1. |
| **Pulse Out 2** | Output | Same gate for voice 2 — fires on the main step *and* on any mid-beat double. |
| **Audio In 1 / 2** | Input | The two oscillators, patched back in for processing. |
| **Audio Out 1 / 2** | Output | The oscillators through the resonant low-pass filter. |
| **CV In 1** | Input | Doubling probability (bipolar). Unpatched → the ~25% default. Patched: full negative = never, 0 V ≈ 50%, full positive = nearly every interval. Patch a slow LFO or random CV to make the ornamentation itself drift. |
| **CV In 2** | Input | Scale offset (bipolar). Shifts the knob-selected scale up or down the 12-scale table; a full swing reaches any scale. 0 V = no shift. Sweep it slowly to morph dark→bright, or step it to change key/mode per phrase. |

---

## Controls

### X — Movement rate

How often the pitch moves.

- **Clocked** (Pulse In 1 patched): X is a **clock divider** — fully CW = move on every clock pulse, fully CCW = hold for up to 60 pulses.
- **Free** (Pulse In 1 unpatched): X is the **internal rate** — fully CW ≈ fast (many moves/sec), fully CCW ≈ slow (~2 sec per move).

### Y — Movement range

How far it wanders from the center.

- Fully **CCW** = frozen on a single note (no movement).
- Fully **CW** = up to ±1 octave of quantized wander.

Everything in between scales the span; small settings drift gently around the center, large settings roam.

### Doubling — voice B passing tones

Both voices normally step together on every beat. But *sometimes* **voice B (CV Out 2)
takes one extra step at the midpoint of the interval** — a passing tone of ±1–2
scale steps — then re-locks with voice A on the next beat. Voice A holds the pulse; B
adds a little melodic flourish over it.

- Probability is set by **CV In 1** (unpatched ≈ 25% of intervals). See the I/O table.
- It only happens when there's room: the interval must be at least 2 clocks long (so
  **not** at X fully CW / every-clock) and **Y** must be above zero.
- **Pulse Out 2** gates on the double too, so an external envelope re-articulates the
  passing note.

### Main knob — Filter cutoff *(switch-dependent)*

- **Switch UP or MIDDLE:** resonant low-pass cutoff for the two oscillators returned on Audio In 1/2. Left = dark, right = bright and open. Resonance is fixed and moderately singing.
- **Switch DOWN (held):** selects the scale instead (see below). The cutoff is held at its last value while you do this.

### Switch

| Position | Function |
|----------|----------|
| **MIDDLE** | **Run** — normal generative movement. |
| **UP** | **Freeze** — hold the current pitches. The walk stops; CV outs stay put. Flip back to MIDDLE to resume. |
| **DOWN** *(momentary)* | **Scale select** — while held, the Main knob picks the scale. LEDs show the scale index in 6-bit binary. Release to return to Run/Freeze. |

---

## LEDs

| State | Display |
|-------|---------|
| **Run** | A single bright LED shows the walk's current position within its range (a bouncing bar). Dim flashes mark note changes. |
| **Freeze** | All six LEDs dim and static. |
| **Scale select** (switch DOWN) | Scale index (0–11) in 6-bit binary at half brightness. |

---

## Scales

Twelve scales, arranged CCW (dark/minor) → CW (bright/ambiguous), reused from the Markov
card: Phrygian, Hirajōshi, Harmonic Minor, Natural Minor *(default)*, Minor Pentatonic,
m7 Arpeggio, Dorian, Major Pentatonic, Ionian (Major), Maj7 Arpeggio, Whole Tone,
Chromatic. All scales are rooted at C — you set the actual key by tuning the oscillators.

Hold **switch DOWN** and turn the Main knob to pick the base scale. **CV In 2** then
offsets that choice up or down the table (see I/O), so you can modulate the scale under
CV while the knob sets home. Scale changes re-quantize the current notes immediately — a
slowly swept LFO on CV In 2 makes the held voices drift through modes and reharmonize.

---

## How to patch it

1. **CV Out 1 → Oscillator A Pitch (1V/Oct).** **CV Out 2 → Oscillator B Pitch.**
2. **Tune the two oscillators by ear** to the interval you want (a fifth, an octave, a
   third — whatever). Both now move in parallel, preserving that interval.
3. Optional articulation: **Pulse Out 1 → envelope A**, **Pulse Out 2 → envelope B**, so
   the notes re-trigger on each change instead of gliding continuously.
4. Optional processing: **Osc A → Audio In 1**, **Osc B → Audio In 2**, then
   **Audio Out 1/2 → your mixer**. The Main knob now filters both voices.
5. **Clock:** patch your system clock to **Pulse In 1**, or leave it unpatched to
   free-run at the rate set by X.
6. Set **Y** for how much the line should move, **X** for how fast, hold **switch DOWN**
   and sweep **Main** to pick a scale.

**Freeze a phrase:** let it run until a nice shape appears, flip the switch **UP** to lock
the pitches, then keep playing over it. Flip back to **MIDDLE** to set it moving again.

---

## Technical notes

- Sample rate 48 kHz, system clock 192 MHz. Integer-only DSP in the audio callback.
- **Movement:** a reflected random walk in semitones, bounded by Y, quantized to the
  selected scale. The output is octave-folded to stay ≥ MIDI 60 (~0 V) so it drives
  positive-only pitch inputs.
- **Parallel motion:** CV Out 1 and CV Out 2 carry the same walk, so the interval between
  your two voices is whatever you tuned into the oscillators. The exception is doubling —
  voice B occasionally departs for a mid-beat passing tone, then re-locks (see above).
- **Filter:** a Chamberlin state-variable low-pass per channel, fixed-point Q12, with a
  fixed moderate resonance.
- The pure movement engine (`src/lockstep_engine.h`) is unit-tested on the host —
  see `sim/test_engine.cpp` (`./sim/build.sh`), which checks that every output note is a
  scale tone, stays positive-CV, and respects the range.

---

## Build

Requires the Pico SDK at `PICO_SDK_PATH`.

```bash
cd src
cmake -S . -B build
cmake --build build -j$(sysctl -n hw.logicalcpu)
# Output: build/lockstep.uf2
```

Flash by holding BOOTSEL while connecting USB, then copy the `.uf2` to the mounted drive.

---

## Status

**Built and validated in offline simulation; not yet tested on hardware.**
