# Bib for Workshop Computer

**Status: hardware-tested release candidate — Bib 1.0.0-rc1. Prior builds are
archived in `archive/`; the flashable release candidate is in `releases/`.**

This is a Bib-inspired stereo delay and reverb card for the Music Thing
Workshop Computer. It translates Bib's touch-first interface into the
Workshop's three pots and momentary Z switch. It reuses the original project's
musical ideas, but not its artwork, panel graphics, or touch UI.

The card uses the bundled `ComputerCard.h` **v0.3.0** (12 May 2026).

## Controls

- Main: virtual slider; turn it through four regions to select a mode
- X/Y: the two controls for that mode
- Z down: tap, toggle, hold, or freeze according to the mode
- LEDs 0–1: binary mode page (`off/off`, `on/off`, `off/on`, `on/on`)
- LEDs 2–5: live, page-specific parameter/status display
- CV In 1 / CV In 2: bipolar modulation of the active page's X / Y parameter

LEDs 2 and 3 show the effective X and Y values after CV modulation. LEDs 4
and 5 show page-specific state: Drive = wavefold and ping-pong; Delay = clock
lock and multi-tap count; Reverb = shimmer and wet mix; Mix = freeze and output
limiter reduction. On the Reverb Up page, LED 2 shows shimmer and LED 3 shows
decay. On the Delay Up page, LEDs 2 and 3 show tape speed and wobble.

When you change Main page, X and Y use **pot pickup**. A control does not
change its newly selected parameter until the physical pot reaches that
parameter's saved position. This prevents abrupt sound changes when moving
between pages.

CV is summed after pot pickup: a centred pot plus CV gives bipolar movement,
and patching/unpatching CV does not change the physical pot's pickup state.

## Modes

- Mode 0 (Main fully counter-clockwise): X = drive; Y = bipolar delay send:
  centre is off, clockwise is normal stereo delay, and counter-clockwise is
  3:4 ping-pong delay. Z tap toggles between overdrive and wavefold.
- Mode 1: X = delay time; Y = feedback. Z recreates Bib's multi-tap
  tapography: the first tap starts a phrase, then up to eight following taps
  (within one second of each other) become relative repeat heads. The final
  tap sets the overall delay length. Workshop's Z has no pressure sensing, so
  all recorded heads have equal level. Two taps make the familiar single
  tap-tempo repeat. After tapping, make a substantial turn of X to return to
  manual time control and clear the recorded pattern back to one even repeat.
  X reaches approximately two seconds: longer times use Bib's automatic tape
  slowdown while retaining the fixed 32k-sample tape buffer.

Pulse In 1 is an external delay clock. After two valid rising edges 50 ms to
2 s apart, Bib measures their interval and uses the original Bib quantiser:
X still selects the desired delay range, but its time snaps to the nearest
3/4, straight, or dotted division of a suitable octave of that clock. A Z tap
immediately clears quantisation for manual tap tempo; as on original Bib, the
next valid incoming clock interval enables quantisation again. On clock loss,
the last synced repeat time is held until X is deliberately moved, avoiding an
audible jump back to the physical pot position.
- Mode 2: X = reverb send; Y = reverb decay.
- Mode 3 (Main fully clockwise): X = wet/dry mix; Y = output level; hold Z
  to freeze the delay input for dub-style looping.

Audio In 1 is the primary input. With nothing patched to Audio In 2, the card
normalises Audio In 1 to both stereo channels. Audio Outs 1 and 2 carry the
stereo result.

### Delay tape page (Switch Up)

On Mode 1 only, the latching Up position opens a secondary tape page. X sets
the delay's transport speed: counter-clockwise slows continuously to a stop,
12 o'clock is normal speed, and clockwise reaches double speed. Y sets a slow
wow/flutter depth around that speed (up to approximately ±25%). Return the switch to Middle for normal
delay time, feedback, and Z tap-tempo operation. Both tape controls use pot
pickup independently of the normal Delay page.

### Reverb shimmer page (Switch Up)

On Mode 2 only, the latching Up position makes X a persistent shimmer amount:
counter-clockwise is off and clockwise increases the original Bib reverb
tank's shimmer feedback. Return to Middle for reverb send and decay. Like
Bib's pressure-set spider control, the chosen shimmer amount remains active
after returning to Middle and across later mode changes; Y is reserved for a
future reverb detail control.

## Build

With a configured Pico SDK:

```sh
cmake -S . -B build -G "Unix Makefiles"
cmake --build build
```

The resulting UF2 is `build/bib_workshop.uf2`.

## Implementation notes

The original Bib's 32k-sample stereo delay and reverb engine are designed for a
different block-audio hardware environment. This version uses a 32k-sample
stereo delay and now directly adapts Bib's Q8 interpolated read heads and
fractional tape writer, so slowed, sped-up, and wobbled transport moves
smoothly between delay samples. The current second pass adds Bib's smoothed
delay-time and tape-speed controls, 144-degree stereo feedback rotation, and
delay-write DC blocking. It also directly adapts Bib's original
fixed-point modulated reverb tank, including damping, limiter, modulation, and
shimmer. The reverb retains
its original every-two-samples cadence, with interpolation back to the
Workshop Computer's 48 kHz output. The design remains a Workshop control/UI
adaptation, not a one-to-one port of the original hardware.

The reverb send retains Bib's quadratic response, with a soft limiting knee in
only its final control range to keep dense transient material clean at maximum
send.

## Fallback

`archive/` contains complete source and UF2 snapshots for each superseded
hardware test stage, with checksums. The current clock revision can therefore
be evaluated and reverted independently.

The card runs the Workshop Computer at **192 MHz**. This provides headroom for
the Bib DSP and is an alias-safe clock for `ComputerCard.h` v0.3.0's CV PWM.

## Future performance option: 240 MHz

The RP2040 can be overclocked to **240 MHz**, which is also an alias-safe
multiple for ComputerCard v0.3.0's CV PWM timing. This is deliberately **not
the default**: 192 MHz is the current tested setting.

Consider 240 MHz only if the direct port of the original Bib DSP needs more
audio headroom after it has been measured on hardware. Make the clock change
in `main()` and then test cold boot, repeated reset, a sustained high-feedback
patch, Pulse In clock sync, tape transport, and at least 30 minutes of normal
use. Revert to 192 MHz if any board shows instability, audio dropouts, or
unreliable reset behaviour.
