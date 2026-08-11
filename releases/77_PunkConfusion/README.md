# 77_PunkConfusion

`77_PunkConfusion` is a deliberately split-personality card built around two
different meanings of "punk".

- `Switch Up` is a voltage-controlled Atari Punk Console-inspired synth voice:
  buzzy, stepped, rude, and patchable.
- `Switch Middle` is `Broken Venue`, a broken-basement PA effect for external
  audio with slapback, grit, feedback, and collapsing room tone.
- `Switch Down` or `Pulse In 2` throws one of four short built-in vocal stabs
  through the same `Broken Venue` chain.

This folder is the initial design scaffold for the card. It captures the first
pass of the controls, jack map, and implementation intent before firmware work
begins.

## Framework and credit

This card is intended to build against the shared
[`Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h`](/Users/adrianvos/coding/GitHub/Workshop_Computer/Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h)
framework in this repository, rather than carrying a separate local copy.

Current repo version checked for this card: `ComputerCard` **0.3.0** dated
**May 12, 2026**.

`ComputerCard` is by **Chris Johnson**. That credit should stay visible in card
documentation, and any future local adaptations should clearly distinguish
between upstream framework code and Punk Confusion-specific code.

## First-pass mode behavior

### Switch Up: Atari Punk Console

This mode is the joke in the title taken literally. It is not an amp or an
effect; it is a self-running synth voice inspired by the classic dual-555 Atari
Punk Console / stepped-tone-generator idea.

- `Knob X` acts as APC pot 1.
- `Knob Y` acts as APC pot 2.
- `Main` is overall output volume.
- `CV In 1` modulates APC timing section 1.
- `CV In 2` modulates APC timing section 2.
- `Pulse In 1` is a hard gate.
- `Audio Out 1` and `Audio Out 2` carry the APC signal.

The target feel is free-running and stubborn by default, with control voltages
able to drag it into squelch, chirp, stepping, and ugly pseudo-melodic behavior.

### Switch Middle: Broken Venue

This mode turns the card into a dirty treatment box for external audio.

- `Audio In 1` is the source input.
- `Main` sets input/room gain: below noon attenuates hot modular signals, noon
  is about unity, and clockwise boosts quieter line-level sources.
- `Knob X` selects and morphs between venue personalities.
- `Knob Y` controls collapse, instability, and failure intensity.
- `Audio Out 1` and `Audio Out 2` carry the processed output.

The target feel is not "nice reverb". It should suggest a damaged rehearsal PA,
basement slapback, speaker bark, and unstable room energy, with venue mood
leaning toward the sweat, wall reflections, and abrasive intimacy of rooms like
CBGB, the 100 Club, the Marquee, and the Whisky a Go Go.

### Broken Venue parameter model

The easiest way to make the venue references feel distinct is not to write four
different reverbs. Instead, use one shared dirty-room engine and swap the
parameter set underneath it.

Shared engine blocks:

1. Input gain / pre-saturation
2. Early reflections (2-4 taps)
3. Main dirty delay / feedback room
4. Venue color stage (EQ + clipping)
5. Collapse layer (flutter, choke, dropout, overload, noise)

Control intent:

- `Main`: input/room gain, roughly 0.25x to 8x with unity near noon
- `Knob X`: venue selection / morph
- `Knob Y`: collapse amount

### Venue personalities

| Venue | Character | Early reflections | Delay / tail | EQ / color | Instability | Noise floor |
|------|-----------|-------------------|--------------|------------|-------------|-------------|
| `CBGB` | cramped, abrasive, overloaded | very short, splashy wall slap | short slapback, fast smear | upper-mid bark, trimmed lows, early clipping | moderate flutter, occasional crackle | hiss + hum possible |
| `100 Club` | dense, warm, sweaty | slightly denser cluster | thicker low-mid tail, less metallic | softer highs, more low-mid bloom | moderate, more pumping than flutter | low-mid room wash |
| `Marquee` | tight, sharp, punchy | shortest and most defined | shortest tail, strongest comb feel | brighter attack, less mud | low to moderate | low |
| `Whisky a Go Go` | bigger, splashier, more stage PA | wider first reflections | longest tail / clearest echo | more top before dirt, broader bandwidth | lower flutter, more dramatic splash | light air / room hash |

### First-pass implementation table

These are not sacred final values. They are practical starting points for code.
All times below are at 48kHz and assume a single shared dirty-room algorithm.

| Venue | Early tap samples | Main delay samples | Feedback start | Tone tendency | Distortion | Collapse behavior |
|------|-------------------|--------------------|----------------|---------------|------------|-------------------|
| `CBGB` | `220, 470, 820` | `2800-4200` | `0.52` | mid-forward, dark top | asymmetric grit early | flutter + crackle + brief overload |
| `100 Club` | `260, 540, 930, 1400` | `3600-5600` | `0.58` | warm, low-mid heavy | soft saturation after tail | pumping choke, less pitch wobble |
| `Marquee` | `140, 310, 560` | `1800-3000` | `0.38` | brightest of the four | hard edge, less fuzz | mostly comb bite, mild dropout |
| `Whisky a Go Go` | `340, 760, 1500` | `5200-7600` | `0.46` | broader, slightly shinier | speaker bark on peaks | splashy tail, occasional wash surge |

### Suggested morph behavior for `Knob X`

`Knob X` should not just darken or brighten the effect. It should move through
the venues.

- `0-1023`: `CBGB`
- `1024-2047`: morph `CBGB -> 100 Club`
- `2048-3071`: morph `100 Club -> Marquee`
- `3072-4095`: morph `Marquee -> Whisky a Go Go`

If the morphing feels too subtle in code, use snapped zones first, then add
crossfades later once the individual personalities are working.

### Suggested collapse behavior for `Knob Y`

`Knob Y` should add "room failure", not just more reverb.

- `0-1023`: mostly stable room, venue fingerprint clear
- `1024-2047`: more grit, more tail density, slight duck/choke
- `2048-3071`: flutter, overload, occasional dropout, stronger feedback
- `3072-4095`: collapsing PA territory, gated tail, unstable reflections, noise bursts

Venue-specific collapse emphasis:

- `CBGB`: crackle, rough clipping, wall slap tearing
- `100 Club`: bloom, pumping, low-mid congestion
- `Marquee`: tight harshness, comb bite, less chaos
- `Whisky a Go Go`: splash, stage wash, echo swell

### Switch Down: Vocal stab

`Switch Down` is a performance interruption, not a third full mode.

- Trigger one of four short built-in vocal one-shots at random.
- Route that sample through the same `Broken Venue` processing chain.
- `Pulse In 2` mirrors this trigger behavior.
- No separate knob layer is introduced while the switch is held.

The sample direction should stay original and genre-inspired rather than trying
to imitate any specific band or performer.

First real sample candidate:

- `Men Shouting Hey.wav` by Jace on Freesound
- Source: https://freesound.org/people/Jace/sounds/57204/
- License: Creative Commons 0 / public domain
- Notes: Group "hey" shout that auditioned well for the card. Trim, downsample,
  pitch-shift if needed, and embed a transformed short version rather than
  shipping the unedited source WAV.

## Suggested jack map

| Jack | First version role |
|------|---------------------|
| `Audio In 1` | Broken Venue input |
| `Audio In 2` | Unused |
| `CV In 1` | APC CV 1 |
| `CV In 2` | APC CV 2 |
| `Pulse In 1` | APC hard gate |
| `Pulse In 2` | Vocal trigger |
| `Audio Out 1` | Main output |
| `Audio Out 2` | Mirrored main output |
| `CV Out 1` | Unused |
| `CV Out 2` | Unused |
| `Pulse Out 1` | Unused |
| `Pulse Out 2` | Unused |

## Initial implementation notes

- Prefer `ComputerCard` + Pico SDK for the first firmware pass.
- Use the current shared `ComputerCard.h` from the repo as the source of truth.
- Prefer `set_sys_clock_khz(192000, true)`.
- Include `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`.
- Prefer `copy_to_ram` unless a later feature gives us a good reason not to.
- Keep the APC path simple and cheap enough to run comfortably inside the audio
  callback budget.
- Keep the vocal samples short and stored on-card. The first real candidate is
  the CC0 Freesound `Men Shouting Hey.wav` sample noted above.
- Route vocal playback through the same venue engine so the card still feels
  like one instrument rather than three unrelated features.
- Start with venue snapping and only add continuous morphing once the four venue
  personalities are distinct enough to be worth interpolating.

## Next code targets

- Replace the single generic Broken Venue parameter set with the four venue
  personalities above.
- Move `Knob X` from generic tone to venue selection/morph.
- Move `Knob Y` from generic decay to collapse/failure amount.
- Keep the existing vocal ducking for now unless hardware testing suggests it is
  too heavy-handed.
