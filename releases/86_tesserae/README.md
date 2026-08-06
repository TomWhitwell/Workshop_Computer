# Tesserae — Arpeggiated Harmonic Generator

A variable-voice arpeggiated chord generator for the Workshop Computer. Inspired by Laurie Spiegel's Music Mouse (1986) and Patchwork (1977).

In mosaic art, a *tessera* (plural: *tesserae*) is an individual piece of stone or glass that, combined with others, forms a larger pattern. This card weaves individual harmonic voices — tesserae — into arpeggiated patterns across a scale-aware harmonic space.

## Concept

Tesserae maps the three Workshop Computer knobs to the X/Y chord grid concept from Laurie Spiegel's Music Mouse. Knob X selects a melody position in the scale, Knob Y spreads voices apart, and the Z switch controls root note, pattern, scale, voice count, and tempo. Where Music Mouse used mouse position on a 2D grid, this card uses knobs and CV to navigate the same harmonic territory — but with variable voice count (2–8), multiple arpeggio patterns, tap tempo, andCV/audio transpose inputs.

## Controls

| Control | Function |
|---------|----------|
| **Knob X** | Melody position — selects base note in the current scale across 3 octaves |
| **Knob Y** | Chord spacing — how many scale degrees apart each voice is (1–5). Also sets voice count when Z Down held (2–8). |
| **Knob Main** | Function depends on Z switch position (see below) |

### Z Switch Modes

| Position | Mode | Knob Main | Knob Y |
|----------|------|-----------|--------|
| **Up** | Pattern select | Choose arpeggio pattern (5 patterns) | — |
| **Centre** | Root octave | Select root note (C1–C7, logarithmic) | — |
| **Down** (tap < 300ms) | Tap tempo | — | — |
| **Down** (hold >= 300ms) | Scale & voice select | Choose scale (10 scales) | Choose voice count (2–8) |

- **Pattern select** (Z Up): Knob Main sweeps through 5 arp patterns. The selected pattern persists when the switch returns to Centre.
- **Root octave** (Z Centre): Knob Main sets the root note with a logarithmic curve — even spacing across C1 to C7. Default is C3 (MIDI 48) when in Pattern select mode.
- **Tap tempo** (Z Down quick tap): Each tap resets the beat phase so the next step fires one beat after the tap. The interval between two consecutive taps (within 2 seconds) sets the internal clock BPM. First tap after a gap just resets phase without changing tempo. Tap tempo is ignored when an external clock is connected.
- **Scale & voice select** (Z Down hold): Hold Z Down for 300ms or longer. Knob Main selects a scale (0–9), Knob Y selects voice count (2–8, default 4). Both update in real-time while held. LED 4 lights up to indicate this mode.

### Voice Count

The arpeggio can use 2–8 voices (default 4). More voices = a wider chord spread across the same scale. Set via Knob Y while Z Down is held. The chord is built as:

```
Voice 0: degree + 0
Voice 1: degree + spacing
Voice 2: degree + 2×spacing
...
Voice N-1: degree + (N-1)×spacing
```

### Arpeggio Patterns

Patterns work with any voice count N. Cycle lengths adapt automatically.

| # | Pattern | Behaviour |
|---|---------|-----------|
| 0 | Up | 0 → 1 → … → N-1 → 0 … |
| 1 | Down | N-1 → … → 1 → 0 → N-1 … |
| 2 | Up-Down | 0 → 1 → … → N-1 → N-2 → … → 1 (2N-2 step cycle) |
| 3 | Down-Up | N-1 → N-2 → … → 0 → 1 → … → N-2 (2N-2 step cycle) |
| 4 | Random | XOR-shift PRNG, equal probability for all N voices |

## Inputs

| Input | Function |
|-------|----------|
| **Audio In 1** | Pitch transpose — shifts the arpeggiated voice (CV Out 1, Audio Out 1 & 2) by semitones when connected. 1V/oct: +1V = +12 semitones. |
| **Audio In 2** | Root transpose — shifts the entire chord (root + all voices) by semitones when connected. 1V/oct: +1V = +12 semitones. |
| **CV In 1** | Offsets melody position (Knob X) when connected |
| **CV In 2** | Offsets chord spacing (Knob Y) when connected |
| **Pulse In 1** | External clock — each rising edge advances one arpeggio step (overrides internal clock when connected) |
| **Pulse In 2** | Reset — restarts arpeggio from step 0 |

## Outputs

| Output | Function |
|--------|----------|
| **CV Out 1** | Current arpeggiated chord note, calibrated 1V/oct |
| **CV Out 2** | Root note (base melody pitch), always present, calibrated 1V/oct |
| **Audio Out 1** | Same as CV Out 1 — current arpeggiated note, approximate 1V/oct (uncalibrated audio DAC) |
| **Audio Out 2** | Previous arpeggiated note (the note before the current step), approximate 1V/oct |
| **Pulse Out 1** | Gate — high while the current arpeggio note is active (~50ms) |
| **Pulse Out 2** | Trigger — short pulse (~10ms) at the start of each arpeggio step |

Audio outputs use the 12-bit audio DAC without per-channel calibration, so pitch is approximate (~10–20 cents deviation possible). This is good enough for patching into V/Oct inputs on other modules.

## LEDs

| LED | Function |
|-----|----------|
| **LEDs 0–3** | Shows current voice index modulo 4 (one LED on at a time) |
| **LED 4** | On when scale/voice select mode is active (Z Switch Down held) |
| **LED 5** | On when external clock is connected (Pulse In 1) |

## Scales

10 scales, selected by holding Z Switch Down and turning Knob Main:

| # | Scale | Intervals |
|---|-------|-----------|
| 0 | Major | 0, 2, 4, 5, 7, 9, 11 |
| 1 | Minor | 0, 2, 3, 5, 7, 8, 10 |
| 2 | Pentatonic Major | 0, 2, 4, 7, 9 |
| 3 | Pentatonic Minor | 0, 3, 5, 7, 10 |
| 4 | Dorian | 0, 2, 3, 5, 7, 9, 10 |
| 5 | Phrygian | 0, 1, 3, 5, 7, 8, 10 |
| 6 | Lydian | 0, 2, 4, 6, 7, 9, 11 |
| 7 | Mixolydian | 0, 2, 4, 5, 7, 9, 10 |
| 8 | Whole Tone | 0, 2, 4, 6, 8, 10 |
| 9 | Chromatic | 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 |

## How It Works

### Chord Generation

Knob X maps to a scale degree across 3 octaves. With Knob Y setting the spacing, the chord is built by adding `spacing` scale degrees for each successive voice (2–8 voices).

For example, with Major scale (7 notes), 4 voices, and spacing=2:
- Knob X at degree 2 (D in C major)
- Chord: D, F#, A, C (D minor 7)

With 6 voices and spacing=1:
- Knob X at degree 0 (C in C major)
- Chord: C, D, E, F, G, A (6 notes of the scale)

### Arpeggiation

The arpeggio cycles through N voices according to the selected pattern. On each step, CV Out 1 jumps to the next voice's pitch, CV Out 2 holds the root. Audio Out 1 mirrors CV Out 1, Audio Out 2 holds the previous note. The cycle resets to step 0 when:
- A reset trigger is received (Pulse In 2 rising edge)

### Audio Inputs (Transpose)

Audio In 1 and Audio In 2 work as additional CV inputs for pitch transposition. When nothing is connected, they have no effect (readings are zeroed by hardware).

- **Audio In 1 (pitch transpose)**: Adds a semitone offset to the arpeggiated voice outputs (CV Out 1, Audio Out 1, Audio Out 2). Uses 1V/oct scaling (≈341 counts per octave, ≈28 counts per semitone). Does not affect the root note (CV Out 2). Ideal for vibrato, envelope-driven transposition, or sequenced pitch shifts from an external LFO or EG.
- **Audio In 2 (root transpose)**: Adds a semitone offset to the root note, which shifts the entire harmonic framework — both the root (CV Out 2) and all derived chord notes. Same 1V/oct scaling. Use for key changes or root modulation from external CV.

### Timing

- Internal clock: default 120 BPM, adjustable via tap tempo (Z Down). Phase accumulator at 48 kHz.
- External clock: when Pulse In 1 is connected, each rising edge advances one step.
- Gate length: ~50ms (2400 samples at 48 kHz)
- Trigger length: ~10ms (480 samples at 48 kHz)

## Example Patch

![Example Patch](patch%20example.png)

## Building

```sh
# Configure (first time)
cmake -S . -B build

# Compile
cmake --build build -j$(sysctl -n hw.logicalcpu)

# Output: build/tesserae.uf2
```

Flash by holding BOOTSEL and copying `tesserae.uf2` to the mounted drive.

## Files

```
Tesserae/
├── main.cpp               — card implementation
├── CMakeLists.txt          — build configuration
├── ComputerCard.h          — hardware abstraction (from Workshop Computer repo)
├── pico_sdk_import.cmake   — Pico SDK import
├── info.yaml               — card metadata
├── README.md               — this file
└── .gitignore
```

## Credits

Inspired by Laurie Spiegel's Music Mouse (1986) and Patchwork (1977). The concept of navigating harmonic space through a 2D grid comes from Music Mouse; the name Tesserae honours both — each voice is a tessera in the sounding mosaic, just as Spiegel's Patchwork wove musical threads into a larger pattern.

Algorithm adapted from Music Mouse emulations by Tero Parviainen and ulmert/laurie (Korg NTS-1 port).

## License

This project is released under the [MIT License](LICENSE). Use it, modify it, fork it, break it, improve it. No warranties, no liabilities. If you build something from this, you don't owe me anything — but I'd love to hear about it.
Build using opencode and glm5.1. Many thanks to Tom Whitwell for making this highly addictive and inspiring musical platform.
