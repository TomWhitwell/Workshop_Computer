# Dub Warning

Beta status: passed single-device hardware testing.

Dub Warning is a playable siren oscillator for the Music Thing Modular Workshop
Computer. It makes a bright triangle-to-square tone, bends it with an internal
LFO, adds a short noisy attack on triggers, and feeds it into a PT2399-style
delay that can smear into a reverb-like dub wash at high repeat settings.
Z Down fires a compact Wheel Up one-shot gesture for punctuation before drops,
while Pulse In 1 fires the normal gated siren.
Pulse In 2 is a reset pluck: at fast or audio-rate triggers it becomes a useful
rhythmic/noisy exciter, especially while sweeping Main pitch.

The card is meant to be immediate: patch either audio output, grab the knobs, and
hit the switch or a pulse input when you want the siren to shout.

## Controls

| Control | Function |
| --- | --- |
| Main | Base pitch, unless Audio In 2 is patched |
| X | Siren sweep rate and pitch bend depth |
| Y | PT2399-style delay character: time, modulation, repeats, dirt, and wet level |
| Z Up | Continuous siren |
| Z Middle | Gated mode. Pulse In 1 fires the siren, then it fades away |
| Z Down | Fire Wheel Up one-shot and reset phase |

## Inputs

| Input | Function |
| --- | --- |
| Audio In 1 | Extra pitch modulation |
| Audio In 2 | Main pitch CV override. When patched, it defeats the Main knob |
| CV In 1 | Extra pitch modulation |
| CV In 2 | Siren color and drive modulation |
| Pulse In 1 | Fire gated siren |
| Pulse In 2 | Reset oscillator/LFO phase and create a short pluck |

## Outputs

| Output | Function |
| --- | --- |
| Audio Out 1 | Main siren through the wet/dry effect mix |
| Audio Out 2 | Alternate direct-plus-echo output |
| CV Out 1 | Internal LFO |
| CV Out 2 | Gate / hit envelope |
| Pulse Out 1 | Gate while the siren or Wheel Up gesture is active |
| Pulse Out 2 | Short hit pulse on trigger/reset/Wheel Up |

## LEDs

| LED | Meaning |
| --- | --- |
| LED0 | Siren level / active gate |
| LED1 | Active Main pitch source: Main knob or Audio In 2 override |
| LED2 | X sweep knob position |
| LED3 | Y delay/effect knob position |
| LED4 | Internal siren LFO |
| LED5 | Wheel Up / hit envelope while active, otherwise echo return level |

## Test

Use `UF2/DubWarning.uf2` for hardware testing. The full input
and output test checklist is in `TEST_PROCESS.md`.

## Development notes

The firmware uses the `ComputerCard` framework and keeps the audio callback
integer-only. `CV In 2` handles oscillator color so the Y knob can stay focused
on the effect. `DubWarning.uf2` is the shippable PT2399-style delay version:
darker repeats, slight clock dirt, smeared feedback, and reverb-like wash at
longer settings. The Wheel Up gesture is fired from the switch and temporarily
overrides the free siren sweep with a short trilled pitch phrase and stronger
delay send. Audio In 2 uses jack detection, so the Main knob is only defeated
when a cable is patched.

Build in the repository dev container from this directory:

```sh
make
```

The dev container will configure CMake, build the firmware, and stage the UF2 in
`UF2/`.

## Credits

- Dub Warning for the Workshop Computer by Adrian Vos, 2026.
- ComputerCard is the Music Thing Modular card library by Chris Johnson, MIT,
  included here as `ComputerCard.h`.
- Music Thing Modular Workshop System Computer by Tom Whitwell / Music Thing
  Modular.

## License

Dub Warning is released under the [MIT License](LICENSE). `ComputerCard.h`
keeps its own MIT license and attribution to Chris Johnson.
