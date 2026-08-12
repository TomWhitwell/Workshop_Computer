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

This card intentionally builds against the local
[`ComputerCard.h`](/Users/adrianvos/coding/GitHub/Workshop_Computer/releases/77_PunkConfusion/ComputerCard.h)
copy in this release folder. The upstream
[`Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h`](/Users/adrianvos/coding/GitHub/Workshop_Computer/Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h)
copy is left untouched.

Local copy version checked for this card: `ComputerCard` **0.3.0** dated
**May 12, 2026**, with the newer per-card startup-silence and ADC DMA re-arm
fixes found in recent release copies.

`ComputerCard` is by **Chris Johnson** and is MIT licensed. That credit should
stay visible in card documentation, and any future local adaptations should
clearly distinguish between upstream framework code and Punk Confusion-specific
code.

## First-pass mode behavior

### Switch Up: Atari Punk Console

This mode is the joke in the title taken literally. It is not an amp or an
effect; it is a self-running synth voice inspired by the classic dual-555 Atari
Punk Console / stepped-tone-generator idea.

- `Knob X` acts as APC pot 1: trigger oscillator rate / pitch.
- `Knob Y` acts as APC pot 2: monostable one-shot time for the stepped-tone
  effect.
- `Main` is overall output volume.
- `CV In 1` modulates APC timing section 1 / trigger rate.
- `CV In 2` modulates APC timing section 2 / one-shot length.
- `Pulse In 1` is a hard gate when patched. With no gate patched, the APC runs
  freely.
- `Audio Out 1` and `Audio Out 2` carry the APC signal.

The target feel is free-running and stubborn by default, with control voltages
able to drag it into squelch, chirp, stepping, and ugly pseudo-melodic behavior.
The model follows the classic APC idea more closely than a simple PWM oscillator:
`X` clocks the astable trigger oscillator, while `Y` sets a separate one-shot
length that can overrun the trigger period and produce skipped/stuck steps.
`Y` is inverted in firmware so the useful monostable range follows the hardware
feel rather than the raw ADC direction.

### Switch Middle: Broken Venue

This mode turns the card into a dirty treatment box for external audio.

- `Audio In 1` is the source input.
- `Main` sets input/room gain: below noon attenuates hot modular signals, noon
  is about unity, and clockwise boosts quieter line-level sources.
- `Knob X` selects and morphs between venue personalities.
- `Knob Y` controls audience absorption: clockwise simulates more bodies in the
  room, damping the reflected room path and shortening the lively tail.
- `Audio Out 1` carries the main processed output. `Audio Out 2` carries a
  decorrelated room output for stereo patches.

Current hardware-test build note: Broken Venue uses a fixed 50% dry / 50% wet
mix so the venue personalities can be auditioned without losing the source.
`Main` no longer changes dry/wet mix in this test pass. The room engine has been
simplified for hardware testing: no random dropout/choke layer, just
deterministic venue colour, delay feedback, and drive.

Audience attenuation note: the current `Y` curve is inspired by
Rummler/Green/Jurkiewicz/Kahle, "Forget About The Seat Dip Effect" (Forum
Acusticum / Euronoise 2025). The implementation approximates grazing-over-
seating loss with a cheap three-band split: small low-frequency loss, strong
`400 Hz-3 kHz` attenuation, and moderate high-frequency damping on the wet room
path.

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

- `Main`: input/room gain, roughly 0.25x to 2x with unity near noon
- `Knob X`: venue selection / morph
- `Knob Y`: audience absorption, with clockwise values damping reflections and
  reducing high-frequency room energy

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
the venues in approximate room-length order.

- `0-1023`: `Marquee`
- `1024-2047`: `CBGB`
- `2048-3071`: `100 Club`
- `3072-4095`: `Whisky a Go Go`

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

- Trigger the vocal one-shot linked to the currently selected venue.
- Route that sample through the same `Broken Venue` processing chain.
- `Pulse In 2` mirrors this trigger behavior.
- Vocal playback is gated: releasing Switch Down, or letting `Pulse In 2` go
  low, stops the call for stutter-style performance gestures.
- No separate knob layer is introduced while the switch is held.
- Playback uses a tiny fade-in/fade-out envelope to avoid trigger clicks.
- The live input is ducked while a call is active so the vocal can cut through a
  running patch.

The sample direction should stay original and genre-inspired rather than trying
to imitate any specific band or performer.

Current embedded vocal calls are original recordings by Adrian Vos, processed
for the card as 24 kHz mono signed 16-bit PCM and peak-normalised before
embedding:

- `Marquee`: `Oi`
- `CBGB`: `Hey Ho`
- `100 Club`: `No Future`
- `Whisky a Go Go`: `Let's Go`

## Suggested jack map

| Jack | First version role |
|------|---------------------|
| `Audio In 1` | Broken Venue input |
| `Audio In 2` | Unused |
| `CV In 1` | APC CV 1 |
| `CV In 2` | APC CV 2 |
| `Pulse In 1` | APC hard gate when patched; unpatched = free-running |
| `Pulse In 2` | Vocal trigger |
| `Audio Out 1` | Main output |
| `Audio Out 2` | Stereo room output in Broken Venue; mirrored APC output in Switch Up |
| `CV Out 1` | Unused |
| `CV Out 2` | Unused |
| `Pulse Out 1` | Unused |
| `Pulse Out 2` | Unused |

## LED map

- `LED0`: APC mode indicator in Switch Up.
- `LED2`: divided APC trigger-clock blink in Switch Up; participates in
  room-zone display otherwise.
- `LED4`: APC gate-open indicator in Switch Up; participates in room-zone
  display otherwise.
- `LED1`: Broken Venue / vocal mode indicator.
- `LED3`: vocal trigger / Switch Down flash.
- `LED5`: clip / chaos indicator.

Middle-mode room-zone display on `LED0/LED2/LED4`:

- `Marquee`: `LED0`
- `CBGB`: `LED2`
- `100 Club`: `LED4`
- `Whisky a Go Go`: `LED0 + LED2 + LED4`

## Initial implementation notes

- Prefer `ComputerCard` + Pico SDK for the first firmware pass.
- Use the local `ComputerCard.h` copy in this release folder; do not modify the
  upstream HelloWorlds version for this card.
- Prefer `set_sys_clock_khz(192000, true)`.
- Include `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`.
- Use the default flash binary type for the sample build; the embedded vocal PCM
  is too large for `copy_to_ram`.
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
