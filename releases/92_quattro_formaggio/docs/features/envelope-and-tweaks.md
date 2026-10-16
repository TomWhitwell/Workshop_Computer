# Envelope and tweaks

Beyond choosing a patch from the voice matrix, you shape sound with a shared **amplitude envelope** and two **tone controls**: cutoff and PWM. All three are stored as learn slots and can be driven from MIDI CC or from the panel knobs X and Y (factory mapping).

## ADSR (slots 7–10)

| Slot | Parameter | Factory source |
|------|-----------|----------------|
| 7 | Attack | Knob **X** |
| 8 | Decay | *(none — learn a CC or leave at default)* |
| 9 | Sustain | *(none)* |
| 10 | Release | Knob **Y** |

Factory values form a **clickless gate**: Attack 0, Decay 0, Sustain 127, Release 0. Notes start and stop quickly without a slow fade, which suits modular gates and avoids clicks when CV and audio run together.

To hear a traditional envelope, raise **Decay** and **Release** first. Add **Attack** if you want a soft onset. The web editor Live tab shows all four sliders; moving them sends changes immediately in device mode.

Each poly voice runs its own envelope instance. Releasing one note in a chord only releases that voice’s envelope.

### Why X/Y behave differently from the Live tab

The card only applies hardware X and Y when those knobs **physically move**. If you set Attack to 80 in the browser and leave X untouched, the value stays 80. Turn X slightly and Attack follows the knob until you change it again from software.

That prevents idle analogue drift from undoing editor settings. If you want X to control something else, re-learn it in SETUP.

## Cutoff (slot 11)

Range 0 (dark) to 127 (open). Factory default is **127** (wide open).

On **lowpass variations** (column 7), cutoff closes the filter. Envelope from the amp also pushes the filter, so Release affects brightness as well as level.

On **FM and sync** patches, cutoff often maps to **modulation index** or filter on FM paths instead of a literal Moog-style ladder. Treat it as “brightness / intensity” when the patch is not a simple subtractive filter.

Learn any CC or knob in SETUP slot 11, or use the Live tab / a controller.

## PWM (slot 12)

Range 0–127, default **0**. Meaning depends on the base row:

- **Pulse row:** pulse width (64 ≈ square).
- **Sync column:** sync ratio or hardness.
- **Acid-style glide patches:** combined with cutoff for squelch.

If a patch ignores PWM, moving the control simply has little audible effect.

## Quick recipes

**Plucky bass (saw + filter)**  
Patch CC 40 (saw + lowpass). Short Decay, moderate Release, cutoff around halfway, play staccato.

**Pad (chorus pulse)**  
Patch CC 8. Slow Attack, high Sustain, long Release, open cutoff.

**Acid line**  
Patch CC 42 (saw + glide). Tune cutoff and PWM together; short gate-style ADSR or factory gate for tight notes.

**FM bell**  
Patch CC 88. Use cutoff as FM index; short Release for mallet decay.

## Saving tweak defaults

Live tab changes are RAM-only until you **Write maps to flash** (Maps tab) or save from Settings for channel data. Hardware-learned CC assignments save when you exit SETUP with Z Up → Middle.

Writing maps also stores the current Live engine values (voice, ADSR, cutoff, PWM) into flash together with the learn table.

## Factory reset

Power on while holding **Z Down** briefly (~0.1 s) restores factory ADSR, cutoff 127, PWM 0, and CC maps including X→Attack and Y→Release.
