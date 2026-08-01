# F1shnet

F1shnet is an FSH-1-inspired filter/sample-hold effect card for the Music Thing
Modular Workshop Computer.

It is not a schematic clone. The card takes the useful performance shape of the
Maestro FSH-1 / F1shy idea and turns it into a simple Workshop Computer effect:
a resonant low-pass filter animated either by an envelope follower or by stepped
sample-and-hold movement.

The current stable behaviour is tuned for Workshop/euro-level synth signals.
Normal 5-6Vpp oscillators and drum machines should move the envelope without
immediately pinning it open, and the output trim starts safely low on boot.

## Modes

- `Middle`: main envelope-filter page.
- `Up`: setup page for output trim and envelope response.
- `Down`: momentary sample-and-hold performance gesture.

`Up` and `Middle` use soft pickup. A knob will keep the stored value for that
page until the physical knob crosses near it, preventing jumps when switching
pages.

`Down` also uses soft pickup for its S&H speed control. Releasing `Down` re-arms
soft pickup for the page you return to.

## Controls

`Middle` main page:

- `Main`: filter range / base cutoff.
- `X`: envelope and S&H depth.
- `Y`: resonance.

`Up` setup page:

- `Main`: output trim. This is scaled for hot modular signals and boots at
  minimum trim. It is a post-filter output level control, not an input
  attenuator, so it does not change envelope sensitivity or filter drive.
- `X`: envelope sensitivity. The range is tuned for euro-level oscillators,
  drum machines, and other modular signals.
- `Y`: envelope decay. Minimum is longest, maximum is shortest.

`Down` momentary gesture:

- `Main`: internal S&H clock speed. Minimum is slowest, maximum is fastest.
- `X` and `Y`: not changed by the Down gesture; the S&H voice uses the stored
  Middle-page depth and resonance settings.

## Audio Behaviour

In normal envelope mode, the card is a wet low-pass auto-wah style effect.
`Audio Out 1` and `Audio Out 2` use the same input and filter core settings, but
with opposite envelope directions:

- `Audio Out 1`: envelope-up low-pass. Transients open the filter upward.
- `Audio Out 2`: envelope-down low-pass. Transients close the filter from the
  stored range.

Holding `Down` or holding `Pulse In 2` replaces the envelope movement with the
sample-and-hold gesture. In S&H mode, the stepped low-pass voice ping-pongs
between the two audio outputs on each new S&H sample. The active side is full
level and the opposite side stays quietly present rather than hard muted. The
pan movement uses a short crossfade to avoid clicks at the start of each step.

The S&H path has:

- a wider pitch-style cutoff response around the stored Middle `Main` range.
- a moderate lower cutoff floor so low `Main` settings stay articulate.
- moderately damped resonance to avoid harsh ringing spikes without making the
  steps too dull.
- ping-pong output movement between `Audio Out 1` and `Audio Out 2`.

## Patch Points

`Audio In 1`

Main mono audio input to the filter engine.

`Audio In 2`

Audio-rate modulation input. It nudges filter range and resonance response.

`CV In 1`

Consistent modulation for filter range and envelope sensitivity in all switch
positions.

`CV In 2`

Consistent modulation for depth in all switch positions. Positive CV widens the
envelope/S&H sweep; negative CV makes it shallower.

`Pulse In 1`

External clock / retrigger. In S&H mode it forces a new random step. In envelope
mode it can kick the envelope open.

`Pulse In 2`

S&H gate input. After seeing a real low-to-high gate, it forces the S&H gesture
high while held. This is qualified to avoid a floating input stealing the normal
envelope-filter path.

`Audio Out 1`

Fully wet envelope-up low-pass output. During S&H gesture this is one side of
the ping-pong S&H low-pass voice.

`Audio Out 2`

Fully wet envelope-down low-pass output. During S&H gesture this is the other
side of the ping-pong S&H low-pass voice.

`CV Out 1`

Stepped sample-and-hold CV, always available.

`CV Out 2`

Envelope follower CV, always available.

`Pulse Out 1`

Mirrors `Pulse In 1`.

`Pulse Out 2`

Divided sample trigger pulse. It fires once for every four new S&H values.

## LEDs

- LED `0`: lit when the `Up` setup page is active.
- LED `1`: follows `Main`; while `Down` is held it follows Down-Main S&H speed.
- LED `2`: lit while the S&H gesture is active from `Down` or `Pulse In 2`.
- LED `3`: follows `X`.
- LED `4`: lit when the `Middle` main page is active.
- LED `5`: follows `Y`.

## Stable Fallback

This folder includes a tested fallback UF2:

- `UF2/stable-fallback/F1shnet.0.1.0.stable-fallback.uf2`
- SHA256:
  `3555612944e22b2fd5f7e43f47b37cf9cb009db79590191a3bb5dd2187bd59d7`

The local Git tag `stable-fallback` points at the fallback marker commit. See
`STABLE_FALLBACK.md` for the hardware-tested behaviour list.

## Build

This is a self-contained Workshop Computer release-style folder:

- `main.cpp`
- `ComputerCard.h`
- `pico_sdk_import.cmake`
- `CMakeLists.txt`
- `info.yaml`
- `UF2/F1shnet.0.1.1.uf2`

Typical Pico SDK build flow:

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

The current build uses:

- `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`
- `pico_set_binary_type(... copy_to_ram)`
- USB stdio disabled
- 192 MHz system clock in `main()`

## Notes

- F1shnet is a simple performance filter effect, not a generic S&H utility.
- The main identity is low-pass auto-wah movement plus a momentary random
  stepped-filter gesture.
- The firmware favours immediate playability over menu depth or many modes.
