# Voice matrix

The onboard synth is not a single fixed sound. It is a grid of **121 patches** built from eleven base timbres and eleven variations. You pick a patch by sending a MIDI **control change** value from 0 to 120 on the learned “audio engine” slot.

Factory default: **CC 24**, any channel (Omni), value = patch number.

## How the grid works

Think of two axes:

- **Row (base timbre):** the raw oscillator family — pulse, square, sine, saw, triangle, and a few specials (FM bell, organ, noise blend).
- **Column (variation):** what happens on top — extra oscillators, detune, sub octave, filter, chorus, glide, sync, and so on.

Patch number = row × 11 + column. Example: row 3 (saw), column 0 (pure) → CC value **33**. Row 3, column 7 (lowpass) → CC value **40**.

You do not need to memorise the arithmetic. The web editor Live tab shows row, column, CC, and a clickable grid. Hardware users often assign one knob to CC 24 and scroll values.

## Base timbres (rows)

| Row | Name | Character |
|-----|------|-----------|
| 0 | Pulse | Variable pulse width (PWM knob matters) |
| 1 | Square | Fixed 50% square |
| 2 | Sine | Smooth sine |
| 3 | Saw | Bright saw |
| 4 | Triangle | Soft triangle |
| 5 | Narrow pulse | Thin pulse (~15% duty) |
| 6 | Bright saw | Saw with extra edge |
| 7 | Hollow | Triangle blended with a quiet square |
| 8 | FM bell | Two-operator FM bell tones |
| 9 | Organ | Drawbar-style stacks |
| 10 | Noise hybrid | Oscillator plus filtered noise |

## Variations (columns)

| Col | Name | What it adds |
|-----|------|--------------|
| 0 | Pure | Single oscillator |
| 1 | Dual | Two oscillators, same pitch |
| 2 | Triple | Three oscillators |
| 3 | Detune | Two oscillators, slight detune |
| 4 | Sub | Sub oscillator one octave down |
| 5 | Octave | Layer one octave up |
| 6 | Unison | Four detuned copies |
| 7 | Lowpass | Filter with envelope from amp |
| 8 | Chorus | Stereo-ish ensemble width |
| 9 | Glide | Portamento between notes |
| 10 | Sync/FM | Row-dependent: hard sync, FM, or metallic stacks |

Column 10 behaves differently depending on the row. On saw and pulse rows it is often **hard sync** (PWM sets sync ratio). On sine/triangle rows it may be **FM** (cutoff sets FM index). The full matrix is in [developer/VOICE_MATRIX.md](../developer/VOICE_MATRIX.md) if you want every cell named.

## Patches worth trying first

These map to familiar sounds from earlier firmware versions:

| CC | Rough label | Use |
|----|-------------|-----|
| 0 | Pulse pure | Default boot sound |
| 4 | Pulse + sub | Thick raw pulse |
| 8 | Pulse + chorus | Juno-like width |
| 33 | Saw pure | Straight saw |
| 36 | Saw detune | Dual detuned saws |
| 40 | Saw + lowpass | Moog-ish filtered lead |
| 42 | Saw + glide | Acid-style glide (pair with cutoff/PWM) |
| 43 | Saw + sync | Hard sync lead |
| 88 | FM bell | Bell / mallet |

CC **127** on the engine map snaps back to patch **0** (same as CC 0).

## Global knobs that follow every patch

These are not per-patch presets; they apply to whichever voice you selected:

- **ADSR** (slots 7–10): amplitude envelope on each poly voice.
- **Cutoff** (slot 11): filter cutoff on filtered patches; FM index on FM patches.
- **PWM** (slot 12): pulse width on pulse rows; sync ratio or acid flavour on others.

See [Envelope and tweaks](envelope-and-tweaks.md).

## Polyphony

All 121 patches run as **four-voice poly**. Variations like chorus and glide apply per voice, not as a send effect across the mix.

## Changing the CC number

If CC 24 is awkward on your controller, enter SETUP, select **slot 4** (audio engine), and move the CC you prefer. Save with Z Up → Middle. The web Maps tab edits the same table.

Slots 5 and 6 are reserved and unused in firmware 0.10.0.
