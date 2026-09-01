# DSP architecture

Fixed-point audio path for the USB MIDI Host Synth. All real-time DSP runs on **core 0** inside `ProcessSample()` at **48 kHz**. Core 1 handles USB only.

See also [CONTROL_FLOW.md](CONTROL_FLOW.md) for the full ISR diagram.

## Fixed-point ranges

| Stage | Type / range | Notes |
|-------|----------------|-------|
| Oscillator output | `int16_t` ±2047 (`clamp12`) | Sine LUT, saw/square/pulse/triangle |
| Voice mix (pre-env) | `int32_t` | Matrix columns may stack 2–3 oscs |
| ADSR level | `uint32_t` 0..65535 | Linear segments; increments from boot LUT |
| Post-env voice | `int32_t` | `(sample * env >> 16) * amp >> 12` |
| Poly bus | `int32_t` | Sum of 4 voices, each `>> 2` (÷4) |
| Drums | `int32_t` | Added to poly bus before final clamp |
| DAC output | `int16_t` ±2048 | Final clamp before `AudioOut*` |

## Phase and pitch

- **Phase:** `uint32_t` accumulator, full 32-bit wrap.
- **Pitch LUT:** `g_midiPhaseInc[128]` built at boot from A4 = 440 Hz (note 69) and Q32 semitone ratio — no float in init.
- **Bend:** `noteBendIncrement()` interpolates between adjacent LUT entries (8-bit fractional note).

## Oscillators

| Wave | Method | Aliasing |
|------|--------|----------|
| Sine | 256-point LUT, index `phase >> 24` | Low (no interp — character) |
| Saw | Phase ramp + **PolyBLEP** | Reduced on saw rows only |
| Square / pulse | Comparator on phase | **Intentional** — digital edge character |
| Triangle | Parabolic segment from upper bits | Moderate |

**PolyBLEP** uses a precomputed `phaseIncRecip = (1<<32)/inc` once per oscillator call; the inner loop uses multiply+shift instead of 64-bit division.

Pulse duty cycle: `(pwm * 0xF0000000 * inv127) >> 32 + 0x08000000` — no `/127` in the hot path.

## Filters

**SVF LPF** (`voiceSvfLp`): Chamberlin-style integer SVF. Input is scaled `<< 7`, states clamped in the amplified domain, output `clamp12(lp >> 7)` per Workshop Computer directive headroom pattern.

**One-pole** (`applyCutoff`, drum `drumHp`): Same `<< 7` / `>> 7` pattern on state and output.

Resonant/acid paths modulate cutoff via `filtEnv` (fast decay envelope on the voice).

## Envelope

- **Poly:** per-voice `envTick()` with stage machine (A/D/S/R).
- **Mono:** `MonoEnv` for panel CV-style gates.
- **Increments:** `g_adsrIncLut[128]` precomputed at boot (`initAdsrLuts()` from `initLuts()`).
- **Sustain level:** `sustain * 516` (≈ `* 65535/127`).

## Worst-case hot path

One sample, 4 held notes, patch **Saw + Unison (col 6) + LPF (col 7 or 8) + chorus**, drums active:

```
ProcessSample (core 0)
  └─ per voice ×4
       ├─ noteBendIncrement (64-bit mul + LUT lerp)
       ├─ renderWaveMatrix: 3× sampleRowWave (PolyBLEP saw if row 3/6)
       ├─ voiceSvfLp (mul >> 15, <<7 domain)
       ├─ voiceChorusStereo (LUT LFO + delay line)
       └─ env multiply; bus += voice >> 2
  └─ drumsRenderMix (up to 8 voices, noise + drumHp)
  └─ clamp ±2048 → AudioOutL/R
```

Target budget: ~20 µs per sample on RP2040 @ 200 MHz (directive §4). No float in this path; boot-only LUT init uses integer recurrence for sine and semitone pitch.

## Intentional non-reference choices

- **Square / pulse without BLEP** — Juno/303-style edge aliasing as tone.
- **`softClip12`** on bright saw matrix row (row 6) — saturation as character.
- **Sine LUT nearest-neighbor** — no linear interp (optional quality tradeoff).
- **Unison col 6** uses `>> 1` on three detuned oscs (~1.5× level, not ÷3) — loudness by design.

Do not “fix” these without an explicit musical/product request.

## Boot initialization

`initLuts()` (called from card constructor):

1. Build `g_midiPhaseInc[]` from note 69 outward (Q32 semitone multiply/divide).
2. Fill `g_sinLut[256]` via Q15 sin/cos recurrence (one step = 2π/256).
3. Call `initAdsrLuts()` for attack/decay/release increment table.

No `<cmath>` in the DSP module.
