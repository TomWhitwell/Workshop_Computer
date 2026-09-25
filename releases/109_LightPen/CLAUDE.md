# LIGHTPEN — working notes for Claude Code

A program card for the **Music Thing Modular Workshop System Computer**
(RP2040), built on the header-only **ComputerCard** library. Sibling project
to `../WorkshopNibbleDrum`, `../Workshop2D2`, `../WorkshopBio` and the other
`Workshop*` card folders in `../` — reuse their conventions and structure
where they fit.

**LIGHTPEN drives an "RGB sensor wand"**: a whiteboard-marker body with three
LDRs in the tip under red, green and blue gels. It reads room light or scans
printed colour (gradient maps, barcode stripes, rainbow strips) and turns it
into CV and audio across fourteen modes.

## Current status: v1.2.1, released, flashed and played

Builds clean under `-Wall -Wextra -Wdouble-promotion -Wfloat-conversion` to
`build/lightpen.uf2`: **6.16% flash, 89.28% RAM** (mostly `gTape`'s 144KB and
the effects' 40KB `gFx`). The released binary is committed at
`UF2/lightpen.uf2`.
`python tools/lpsim.py` passes all 233 checks.

`info.yaml` is `draft: false`, `Status: Released`, version 1.2.1. Own repo:
`uglifruit/WorkshopLightPen`, tagged `v1.2.1`. Submitted to the community
catalogue as `releases/109_LightPen` in `TomWhitwell/Workshop_Computer` —
merged there at 1.0.0 (PR #421), then 1.1.0 (PR #423), with 1.2.0 open as
PR #424. Note 1.1.1 never went up on its own — its Mode 9 fix rode along in
1.2.0, so the catalogue skipped straight from 1.1.0. Copy only the files a release
actually changed into that folder: the fork is checked out with CRLF, so
re-copying every file makes git see all 41 as modified.
Design plan of record: `~/.claude/plans/glistening-popping-avalanche.md`.

The bench list further down is no longer a list of blockers — the card has
been on hardware — but those are still the values that were set from the
model rather than measured, so they are where to look first if a mode feels
wrong on a different wand or a different set of gels.

## Fixed wiring (every mode)

| Jack | Job |
|---|---|
| CV Out 1 | LDR supply, `CVOut1Precise(262143)` once. Latched by the library. Only `main.cpp` writes it; engines cannot. |
| CV In 1 / CV In 2 / Audio In 2 | Red / Green / Blue LDR returns |
| X knob | global gain, 0.25x..4x (exp, unity at noon) |
| Y knob | global slew, shift 5..14 at 48kHz (~240Hz .. ~340ms) |
| Switch Up | next mode, on arrival, after a 20ms debounce |
| Switch Down | the mode's action (press / release / 1s hold events) |
| Down held at power-on | calibration, two-point or five-point, saved to flash |

LEDs are three rows of two (`0 1 / 2 3 / 4 5`). Left column (0, 2, 4) = the
mode as a 3-bit number counting from ONE (mode 1 = one LED, mode 7 = all
three), so no mode is ever a dark column. Three bits stop at 7, so modes past
that light the same LEDs at HALF BRIGHTNESS and count again from one: mode 8 is
a dim 1, mode 9 a dim 2. **Brightness is the fourth bit**, which leaves room to
14 without touching the right column. `kHalfMode` is 1100, not 2048 — an LED's
perceived brightness is nowhere near linear in duty cycle, and half the number
reads as nearly as bright. It blinks for 1s on a mode change. Right column
(1, 3, 5) = live R, G, B. A mode's own option change (chord, rotation, scale,
kit) blinks its option number as the first N LEDs for 1s. All six blinking at
boot = no calibration saved yet.

## Calibration

Two-point, per channel: black reads 0 and white full scale, so the three
gels read on the same scale however much light each passes. The sign of
white minus black also sets the polarity, so a wand with the LDRs on either
side of the divider just works.

**The curve between those ends is per-channel too** (`sensors.cpp`). A plain
linear-in-voltage map is badly compressed at the bright end — worst on
whichever gel passes most light, usually red, which then runs to full scale
on modest light and sits there. Since the front end is known exactly, a
reading inverts to the LDR's resistance, and each channel is spread evenly
across LOG resistance between its own two references, which is the right
scale for a power-law device. In the model: with a red LDR spanning 10k to
200k, mid-grey read 65% of full scale before and 50% after.

The curve is a 513-entry table per channel, built at boot by
`SetCalibration()` (divides and logarithms; never from the audio path) and
sampled with linear interpolation, within 0.01% of the exact curve. It is
indexed over the whole signed input range, and is rescaled after building so
the two references land exactly on 0 and full — which is why the table is
allowed to hold negative values below black rather than clamping there;
clamping pins the nodes under the reference and breaks the rescale.
`CalibFailMask` and the model's own sanity checks (standard wiring, readings
inside the divider's range, at least a sixteenth of an octave of resistance
between the references) fall back to a straight line, which still
calibrates — just without the curve.

### Five-point: colour, and the cross-talk un-mix

Gelled LDRs are hopelessly broad. On the bench a pure blue screen read
almost equally on all three cells. Two separate things cause that, and the
five-point path fixes both:

1. **Half of it was the display curve.** In linear light that blue patch is
   already (0.13, 0.19, 0.27) in the model — poor, but not grey; the
   per-channel log curve then stretched it to (0.27, 0.40, 0.52). So the
   gain applied after the un-mix is now SHARED: one lookup on `max(c)`,
   applied to all three, which preserves every ratio exactly. A per-channel
   curve would lift the residual bleed straight back into view.
2. **The rest is the gels**, and that mixing is linear in LIGHT. So the
   tables carry linear light instead of the log curve, the three primary
   captures ARE the mixing matrix, and boot inverts it.

The exponent of the power law is unknown per cell, so it is solved from the
captures: on a screen, white is red plus green blue, so a correctly
linearised cell's response to white must equal the sum of its responses to
the primaries. `Additivity()` is that residual and is sign-definite (a
majorization argument — positive below the true exponent, negative above),
so 12 bisection steps land it with no tolerance test.

**Do not trust that exponent in absolute terms.** A black CARD rather than a
black screen biases all three by about +20%, and a panel that dims full white
biases them the other way. It is kept because the RATIOS between the three
cells survive both, and the ratios are what stop a grey card reading tinted
(a single shared exponent gives a visible blue cast, measured in lpsim).

**Damping, and why not Tikhonov.** `M` is shrunk toward `diag(rowsums)`
rather than damping `MᵀM`: both have the same row sums, so neutral stays
neutral at every setting, and beta = 1 gives plain per-channel
normalisation — exactly the two-point card. The regulariser is a continuous
dial from "full un-mix" to "no un-mix", and 14 bisection steps find the
least damping whose row budget (`Σ|A| ≤ 6.0`) holds. That budget is doing
three jobs at once: bounding noise gain, bounding the systematic error from
CdS drift and screen-brightness change, and keeping the per-sample
accumulator inside int32, which is what makes Q10 the right format for the
matrix.

`Separation()` (|det| of M with columns normalised) is the diagnostic: 5 LEDs
at 0.30, down to a fallback below 0.03, reported on the LEDs after the reboot.
It is also how to A/B gels and baffling on the bench, with one number.

Measured in `tools/lpsim.py` on a leaky-gel model: blue went from a 0.11
spread to 0.67, white still reads full, black exactly zero, grey neutral to
within 0.02, and a hopeless gel set declines the job rather than amplifying
noise.

**Behaviour change worth knowing**: after un-mixing, warm room light reads as
mostly red with green and blue near zero, where before it read as a lifted
grey. That is more correct, but Modes 1/3/6 will feel different in ambient
light. Two-point remains the right calibration for non-screen use.

Flow (`calibration.h`, driven from `main.cpp`): hold Down at power-on,
release, then five taps of Down — red, green, blue, white, black. Up cancels.

**There is no mode selection**, and deliberately so: the captures say which
calibration was meant. Show the card white for the first four taps and it
reads as two-point. `CapturesLookTwoPoint()` is the test, and the margin is
wide because white is the SUM of the primaries on any additive display — at
least one cell always reads white well clear of the dimmest primary. Measured
across the gel sets in lpsim, real primaries exceed the tolerance (span/6, or
32 counts) by 2.6x even when the gels can barely tell colours apart, while
four presentations of the same white differ by about a dozen counts, and
still pass with 40 counts of deliberate hand wobble. A mis-read is harmless
anyway: four near-identical "primaries" make a singular matrix, the
separability test declines it, and the white/black captured alongside are
used instead.

This replaced an earlier chooser (Up toggling the sequence before the first
tap). Inferring it is strictly better — one gesture, no hidden state, and
nothing to mis-flick — so don't reintroduce the chooser.

A `Report` step blinks two LEDs or five for a second before saving, so the
user sees which calibration was recorded before the reboot.

While waiting for a tap the LEFT column is a live raw R/G/B meter (so the
wand can be aimed) and the RIGHT column is the target: LED 1 red, 3 green,
5 blue, all three for white, all three DIM for black. Dim rather than dark —
a dark card is indistinguishable from a crashed one. Capturing fills all six
progressively; all six steady means saved, then it reboots.

A capture pinned outside [16, 2040] retries just that step. A channel whose
white and black differ by less than 64 LSB (`kMinCalibSpan`, ~0.2V) fails
the whole sequence, flashing that channel's LED.

`tools/calibration-target.html` is the screen target for the five-point
sequence: the five colours full-screen in capture order, with the LED cues
and the display settings (night mode, auto-brightness) that would otherwise
be baked into the result. Published at
https://claude.ai/artifact/6oRVzkty6uS9Jd3RGUD4Sj

Saving (`calibstore.h`): the calibration finishes inside `ProcessSample()`,
which calls `Abort()`. That makes `Run()` return in `main()` with both of
the library's interrupt handlers removed, so `main()` writes the one flash
sector (512KB in; magic `LPC1`, checksum) with nothing that could fetch from
flash mid-erase, then `watchdog_reboot`s. The next boot loads it. A record is
ignored if its magic, version or checksum is wrong, if it fails the span
check, or if the firmware image ever grows past 512KB.

**Version 2** carries the three primary captures and the mode; a version-1
record still loads as a two-point calibration and is rewritten as v2 next
time anyone calibrates. Only the CAPTURES are stored — exponents, matrix,
tables are all recomputed every boot, so improving that maths takes effect
without anyone recalibrating. Bump `kCalibVersion` if `CalibData` changes
again, and keep the v1 read path.

## Modes

| # | File | Main | Down | Outputs |
|---|---|---|---|---|
| 1 | `modes/mirror` | gate threshold | hold = freeze CV | CV2/A1/A2 = R/G/B 0-5V; P1/P2 = R/B over threshold |
| 2 | `modes/triad` | sine→tri→saw→square | tap = Maj/Min/Sus2/Sus4 | A1+A2 = drone; R/G/B = root/3rd/5th VCA; A In 1 = 1V/oct (0V = A2) |
| 3 | `modes/barcode` | speed 1/8x..8x (1x at noon, dead zone) | hold = record; TAP = next kit | luminance, cut into elements; P1 = every element, P2 = wide ones; A2 = black/white gate; A1 = the voice (CV2 = width), or width (CV2 = brightness) with the voice off |
| 4 | `modes/tapescrub` | LP→BP→HP walk | hold = record; TAP = Scrub/Slice/Sweep | A1+A2; A In 1 = source; G = head or slice or loop length, R = cutoff, B = resonance |
| 5 | `modes/synesthesia` | sine→saw→square | tap = rotate R/G/B roles | A1+A2 voice; CV2 = envelope; A In 1 = 1V/oct (0V = C3); P In 1 = gate |
| 6 | `modes/hueorgan` | scale (4 zones) | tap = transpose +1 (wraps), hold 1s = C | CV2 = note (calibrated); P1 = new note; P2 = colour seen; A1+A2 organ |
| 7 | `modes/jog` | jog depth (nudge→shuttle) | hold = record; TAP = Nudge/Platter/Brake | A1+A2; G = speed, B = platter weight, R = cutoff; CV2 = position; P1 = loop start; P2 = reversing |
| 8 | `modes/prism` | sine→tri→square | tap = rotate R/G/B roles | A1+A2 voice; CV2 = envelope; R = FM, G = crush, B = cutoff; sustains on the gate |
| 9 | `modes/colourfilter` | filter drive 1x..8x | tap = rotate R/G/B roles | A In 1 → SVF → A1+A2; R = cutoff, G = resonance, B = LP→BP→HP blend; CV2 = envelope follower, P1 = signal present |
| 10 | `modes/delay` | feedback tone | tap = rotate R/G/B roles | R = time 10-416ms, G = feedback, B = mix; P1 = one pulse per delay period; CV2 = envelope |
| 11 | `modes/reverb` | pre-delay 0-85ms | tap = rotate R/G/B roles | 8 combs + 4 allpass; R = size, G = tail brightness, B = mix; CV2 = envelope |
| 12 | `modes/freeze` | grain scatter | hold = TOGGLE freeze; TAP = rotate roles | 3 granular voices over the frozen buffer, alternating between the two outs; R = grain size, G = pitch, B = density; P1 = each grain |
| 13 | `modes/modulation` | phaser→flanger→chorus | tap = rotate R/G/B roles | 6 allpass stages; R = LFO rate, G = depth (floored), B = feedback; CV2 = the LFO, P1 = its rising half |
| 14 | `modes/mangle` | ring carrier pitch | tap = rotate R/G/B roles | no buffer; R = bit+rate crush, G = fold, B = ring depth; CV2 = envelope |

Design decisions worth knowing before changing things:

- **X is gain only**, applied after calibration: 0.25x..4x of the calibrated
  white-to-black range.
- **Mode 2 has no inversions**: four flavours, root position. The spec said
  "flavours / inversions", and the plan traded inversions away. Easy to add
  to `kChord`.
- **Mode 2 takes 1V/oct on Audio In 1**, which the plan said it would not have.
  The jack is free in that mode, and a drone you cannot tune is limiting.
- **Mode 3 reads luminance, not colour** (`(ur+ug+ub)/3`): a printed barcode
  varies lightness, and summing gives three channels of signal with their
  noise averaged down. Coloured stripes still read, by their lightness.
- **Mode 3 stores elements, not a waveform.** After release, an incremental
  pass (64 samples per control tick, ~43ms for a full take) runs a Schmitt
  trigger over the recorded bytes and records each run of black or white as
  {start, width, dark}. Playback walks those, so triggers land on bar edges
  at any speed and the wide/narrow distinction (against the take's mean
  width) is what drives Pulse Out 2 and the width CV. One pass inside a
  single ProcessSample would overrun the 20.8us budget many times over.
  Recording is 1 sample per control tick (1.5kHz, 4096 max = 2.73s): width
  resolution is the whole point of this mode, so it is not decimated further.
  Takes go into a spare buffer and swap in only once analysed and found to
  hold at least two elements, so a stray tap keeps the playing loop.
- **Mode 4's filter is a continuous crossfade** (0-20% LP, 40-60% BP, 80-100%
  HP), not hard thirds. The user confirmed this.
- **A TAP of Down cycles an option in Modes 3, 4 and 7; a longer press is
  still the record gesture.** The threshold is `kTapTicks` (256 ticks, 171ms,
  in fastmath.h): above the slowest deliberate tap, below the shortest swipe
  the LDRs can physically resolve. `OnDownRelease` now carries the press
  duration rather than a bool, and `Ctrl` carries the live count.
  **Modes 4 and 7 must NOT call `StartRecord()` on the press** — it zeroes the
  write head, so it would overwrite the front of the existing take every time
  anyone tapped. They arm on the press and start the tape from `ControlTick`
  when the count reaches `kTapTicks`, which costs the first 171ms of a take
  and makes a tap provably harmless. `Tape::kMinLen` dropped to 256 because
  the "discard accidental taps" guard it existed for is now unreachable.
  Mode 3 needs none of that: it records into the spare buffer and swaps.
- **Modes 4 and 7 share one take** (`tape.h`, `gTape`, 168KB at file scope).
  Recording is a gesture in both: hold Down and the take is as long as the
  hold, capped at `Tape::kMaxLen` (1.5s, down from 1.75s when the effects
  arrived wanting 40KB — see the RAM table). A take under 50ms is discarded and the previous one kept, though
  its first few ms have been overwritten by then. Mode 4 places the head
  absolutely from Green; Mode 7 lets the loop run and Green sets its speed.
- **Mode 3's voice** (`percvoice.h`, header-only so it inlines into the
  RAM-resident `AudioTick`) is two drum voices permanently assigned to the two
  edge directions — bright→dark owns one, dark→bright the other — so a kick
  and a snare always ring together with no stealing. Each voice has separate
  tone and noise envelopes, which is what makes one kit a kick (long body,
  2ms click) and another a snare (short body, long hiss) from the same
  arithmetic. Envelopes decay by `(env >> shift) + 1`; the **+1 is
  load-bearing**, as a plain shift stalls short and leaves DC on the output.
  The pitch sweep falls exponentially toward its floor, which is what makes a
  kick a kick rather than a click followed by a tone. One shared `Svf` serves
  every kit through its three taps. Five positions: Off, Kick+Snare, Clicks,
  Crackle, Shaped Noise — Off restores the mode exactly as it was.
- **Mode 8 is Mode 5's architecture on different destinations** (FM, crush,
  cutoff) with a SUSTAINING envelope rather than a percussive one, so the
  two voices do not just sound like knob swaps of each other.
- **Mode 5's fold is closed-form** (`Fold()` in `synesthesia.cpp`), not
  NibbleDrum's 3-pass reflect loop: at 8x drive a full-scale input needs more
  than three reflections.
- **Mode 6 rotates the hue wheel 30°** so the wrap is at magenta. At the
  default origin, red would dither between the lowest and highest note.
  Degrees are evenly spaced in hue (two octaves + top root), with a
  quarter-band hysteresis and a 30ms dwell per note. The organ voice on the
  audio outs was not in the plan; it was added because those jacks were free.
- **Mode 9 is the only PROCESSOR on the card.** Every other mode generates;
  this one filters Audio In 1. With nothing patched, `EnableNormalisationProbe`
  holds that input at exactly 0 and the mode is silent — correct, and worth
  knowing before chasing it as a fault. It is also the only mode where a
  colour drives the filter TYPE rather than a level: `SvfBlend` takes a 0..4095
  knob, so blue goes in as `ub >> 4` and the continuous LP→BP→HP walk needs no
  hysteresis, which is exactly why that blend was written as a crossfade.
- **Mode 9's soft clip is trimmed to +/-2047, and the trim is load-bearing.**
  The cubic's ceiling lands on 2048 and its floor on -2049: the final shift
  floors while `c / 3` truncates toward zero, so the two ends miss by a LSB in
  OPPOSITE directions. That is 1 LSB outside the range `Svf`'s overflow
  headroom is argued for. lpsim asserts both the trimmed range and the
  untrimmed escape, so removing the clamp fails a check rather than going
  quiet. L is 4096 rather than full scale to keep `x*x` exact inside int32,
  and the 0.75 that costs is folded into the drive constant (5461 = 4096*4/3),
  which is what makes the bottom of the drive travel unity for small signals.
- **Modes 10-14 all share one 40KB buffer** (`fx.h`, `gFx`), the way 4 and 7
  share the tape. They are mutually exclusive, so only the current mode's
  regions are live. Each lays its regions out from offset 0 and must fit
  `kFxLen`; the reverb's tank has a `static_assert` for exactly this.
  **Entering one of these modes wipes the buffer over control ticks**
  (`FxClearChunk`, 1024 samples a tick, ~13ms) and keeps the wet path muted
  until it finishes — 40KB of stores is thousands of times the 20.8us budget,
  and without the wipe you hear the previous effect's buffer as a burst.
- **The reverb's input shift is a measured number, not a guess.** Eight combs
  at 0.92 feedback each reach ~12x their input before summing. lpsim measured
  the worst case (biggest room, no damping, sustained tone): `>>4` leaves the
  tank 13.8dB down and inaudible under the dry signal, `>>2` peaks at 2094 and
  clips, `>>3` is -7.8dB with a peak of 1042. Feedback tops out at 0.92 rather
  than Freeverb's 0.98 for the same reason.
- **Mode 10 clamps its delay time, and the clamp is load-bearing.**
  `pow2_scale` interpolates LINEARLY inside an octave and 2^x is convex, so it
  reads up to 6% HIGH — at full brightness it asks for 441ms from a 416ms line,
  which `Tap()` wraps modulo the buffer into a completely different delay. Every
  other caller of `pow2_scale` tolerates that error; this one cannot.
- **Mode 13 crossfades between two topologies** rather than switching: a phaser
  and a modulated delay both run every sample and Main mixes them, the same
  trick `SvfBlend` uses on the filter taps, so there is no discrete boundary to
  chatter across. Note `depth_ >> 1` before it meets the LFO — `lfo * depth_` at
  full scale is 2147385345, which clears int32 by 98302.
- **Do NOT pre-mix the phaser output with the input.** The final
  `(dry + wet) >> 1` is already the 50/50 a phaser needs for full-depth notches.
  The first version mixed dry in twice, leaving a quarter allpass against three
  quarters dry: measured sweep 0.35dB, which is inaudible. Not pre-mixing,
  widening the coefficient sweep from 0.25..0.75 to 0.10..0.90, and going from
  four stages to six took it to 4.23dB — twelve times the movement. lpsim
  asserts the swing in dB and compares it against the old arrangement.
- **Mode 13's depth and rate both have FLOORS**, and they are the whole reason
  the mode felt "not obvious". At depth 0 the LFO moves nothing, and a dark wand
  asked for exactly that, so the mode was a bypass at rest; the rate floor was
  0.0625Hz, one cycle every sixteen seconds, which reads as switched off rather
  than as slow. Depth now starts at 35% and the rate at 0.25Hz. **Any control
  whose zero is "effect off" needs a floor if a dark wand is going to ask for
  it** — the same fault as Mode 9's cutoff, in a third place.
- **Mode 12 toggles from the control tick, not the press**, at
  `downTicks == kTapTicks`, exactly as Modes 4 and 7 start recording — and for
  the same reason: acting on the press makes every tap a 171ms hole. The freeze
  LATCHES rather than being momentary, because holding the switch occupies the
  hand that should be moving the wand — which is most of the instrument.
  `toggled_` stops one hold flipping it twice and stops the release being read
  as a tap.
- **Mode 12 is the only mode that sends different audio to its two outs.** The
  three grain voices alternate sides, panned 3:1 rather than hard, so the pad is
  wide and still sums to mono. Everywhere else `audio2 = audio1`; a granular
  cloud is the one place width is worth more than the redundancy.
- **Mode 12 takes NO output gain, and that is measured.** The 3:1 pan weights
  and the 50%-overlap envelope already sum to unity: across every density, grain
  size and pitch, a full-scale frozen source peaks at 1996 of 2047. A 1.25x
  boost — which looks harmless, and which the first version had — clips at 2495.
  lpsim asserts both numbers.
- **Mode 12's spawn interval must never exceed the grain length.** Two
  triangular envelopes sum flat at 50% overlap; at 100% they meet at zero and
  the pad pulses to silence at every join. The density control therefore runs
  from half a grain down to a third, never longer. The grain floor is 20ms for
  the same class of reason: at the original 2ms a grain was 96 samples with a
  24-sample ramp, which is a click generator, and it was what a dark wand asked
  for — so it was what the mode sounded like at rest.
- **Per-sample virtual dispatch** of `AudioTick` is deliberate; see
  `engine.h`.

## Analogue front end (from Tom's Rev 1 docs + schematic)

`../Workshop_Computer/documentation/` has `Computer_ Rev 1 Documentation.pdf`
and `computer_Rev_1_0_0_Schematic.pdf` (no text layer; render it with
pymupdf). Sheets 2/5 and 3/5 settle how the wand behaves:

- **Every signal input is 120k into a virtual earth**: CV In 1 (R17), CV In 2
  (R25), Audio In 1 (R27) and Audio In 2 (R42) each feed an MCP6004
  inverting stage (33k feedback, -5V_REF offset) that maps ±6V to 0-3.3V.
  So all four inputs present the **same** 120k to 0V, and an LDR from CV Out 1
  to an input tip is a divider against that 120k. The wand needs no pull-down
  and no ground wire of its own.
- **CV and audio outputs already have a 1k series resistor** (R55, R3, R48,
  R51) from a TL074. With 120k always dominating, each LDR channel draws
  about 50µA, so all three together drop ~0.15V across that 1k: cross-talk
  between channels is ~2%, and no current limiting is needed.
- **All ins and outs are bipolar DC-coupled, ±6V** — including the CV/Audio
  pair, stated explicitly in the documentation. Blue on Audio In 2 is sound,
  and the audio outs work as CV.
- Reading in code units: `2048 * 120 / (121 + R_LDR_in_k)`. 10k reads ~1876,
  120k ~1019, 1M ~219, and the 1k in series caps the reading at 2031 however
  bright it gets. Sensitivity peaks where the gelled LDR's resistance is near
  120k. `sensors.cpp` inverts this to recover resistance from a reading, so
  those constants are load-bearing, not documentation.
- **The audio inputs are normalled to each other**, and Audio In 2 always
  holds the Blue LDR, so `main()` calls `EnableNormalisationProbe()`: the
  library then forces an unpatched Audio In 1 to exactly 0 instead of
  whatever the normalling gives it.

## Bench values set from the model, not measured (look here first)

1. **LDR span under the gels.** White and black must differ by at least 64 LSB
   (~0.2V) per channel, and a small span reads noisily. Measure the gelled
   LDRs with a meter: if the geometric mean of their white/black resistance is
   far below ~120k, add a resistor of about that mean from tip to sleeve at
   each input plug to restore contrast.
2. **1V/oct scale on Audio In 1**: `kSemisQ8PerUnit = 9` is right for the
   documented ±6V, but the divider is 1% parts — expect to trim it.
3. **Mode 6 thresholds** `kMinBright` / `kMinChroma` are guesses; gelled
   LDRs never separate cleanly. After a five-point calibration they should be
   revisited — the un-mix widens chroma considerably.
4. **How well the real gels separate.** The separability bar after a
   five-point calibration is the number to watch. Below 3 bars, consider
   optical isolation between the cells (opaque sleeving so each sees only its
   own gel) — that attacks the scatter term directly, and buys more than any
   amount of maths inverting it.
5. **CPU headroom.** Not profiled. Mode 2 (three morphing voices) is likely
   the heaviest; the sensor front end costs about 176 cycles a sample of the
   ~4000 available. The card overclocks to 192MHz as the siblings do.
6. **The calibration save.** The Abort -> write -> reboot path is untested
   on hardware. Confirm a calibration survives a power cycle (no six-LED
   blink at boot) and that the card boots normally afterwards.

## RAM

| Consumer | Size |
|---|---|
| `gTape`, shared by Modes 4 and 7 (1.5s at 48kHz) | 144 KB |
| `gFx`, shared by Modes 10-14 (426ms at 48kHz) | 40 KB |
| Mode 3's two takes, with their element lists | 11 KB |
| The three response tables (513 x int32) + shared gain table | 8 KB |
| Sine LUT, engine/oscillator/filter state, library buffers | ~25 KB |
| **Total, measured at link** | **228 KB, 89% of 256KB** |

**The two shared buffers are the whole budget**, and they were traded against
each other: the effects wanted 40KB, so `Tape::kMaxLen` came down from 1.75s to
1.5s to pay for it. At 1.75s the card links at 98.4%, which works — the stack
lives in SCRATCH_X at 0x20040000, outside this 256KB figure, and nothing here
allocates — but it leaves 6KB, so the next feature has nowhere to go. Watch
`--print-memory-usage` whenever either constant moves.

## Build

Toolchain comes from the Pico VS Code extension install at `~/.pico-sdk/`.

From PowerShell:

```powershell
$env:PICO_SDK_PATH = "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
$env:PATH = "$env:USERPROFILE\.pico-sdk\cmake\v3.31.5\bin;$env:USERPROFILE\.pico-sdk\ninja\v1.12.1;$env:USERPROFILE\.pico-sdk\toolchain\14_2_Rel1\bin;$env:PATH"
cmake -B build -G Ninja
cmake --build build
```

Output: `build/lightpen.uf2`. Copy to `FLASHME/` for flashing (git-ignored).
`cmake`/`ninja` are **not** on the default PATH — always set it as above.

**Every new `.cpp` must be added to `add_executable` in `CMakeLists.txt`.**
Header-only modules (`sensors.h`, `controls.h`, `calibration.h`,
`calibstore.h`, `leds.h`, `osc.h`, `svf.h`, `engine.h`) need no entry.

**Only `main.cpp` may include `ComputerCard.h`.** It defines its member
functions in the header unless `COMPUTERCARD_NOIMPL` is set, so a second
includer is a multiple-definition link error. That is why `controls.h` has
its own `Sw` enum.

## DSP checks

`python tools/lpsim.py` gives integer-exact mirrors of fastmath, the sensor
laws and calibration mapping (including an inverted wand and the minimum
span), the oscillators, the SVF and blend, the fold, Mode 3's speed law and
edge finder, Mode 6's hue and note picker, Mode 9's drive and soft clip and the
mode-LED scheme, the fx.h primitives and all five effect modes end to end, and
the switch debounce. It exits non-zero on failure. Keep it in step with the C++: when you change a law in
the firmware, change its mirror too.

It has already caught two real bugs. **The SVF's integrators froze at low
cutoff** (`f*hp >> 14` rounds to zero for |hp| < ~190), leaving up to 117 LSB
of DC stuck on the output; the fix is that the states carry 8 extra fractional
bits. And **Mode 9's soft clip overshot its range by a LSB at each extreme**
(2048 and -2049, the shift flooring one way and the truncating divide the
other) — the check was written expecting it to be in range, and it was not.

**The lesson from Mode 9 shipping silent: test the SOUND, not the pieces.**
Every one of the 198 checks passed while that mode made no noise at all,
because they each tested one stage in isolation and none of them put a signal
in one end and listened at the other. Every effect mode now has an "it makes
sound" check that drives a sine through the whole chain and asserts a level in
dB. Two of those found real faults the moment they were written — the delay's
time overshooting its line, and the reverb tank sitting 14dB down. Add one for
any mode you add.

**And the follow-up lesson: "it makes sound" is not enough either.** Freeze and
Modulation both passed their level checks and both came back from the bench as
"lame" and "could be more obvious". Each was technically working and musically
inert, for the same underlying reason as Mode 9: **the rest state is whatever a
dark wand asks for, and that is usually the useless end of every control.** When
you add a mode, write down what it does with all three colours at zero, and if
the answer is "nothing", put floors on the controls. The checks now assert the
rest state directly — grain size at its floor, LFO depth non-zero, phaser swing
in dB — rather than only that a signal gets through.

## Hard rules

Identical platform constraints to the other cards on this bench:

- `ProcessSample()` runs at **48 kHz** on core 0, inside a DMA interrupt.
  Allocation-free, no `malloc`, no blocking, no `float` in the hot path —
  fixed-point only. Every `AudioTick` is `__not_in_flash_func`; check with
  `arm-none-eabi-nm -C build/lightpen.elf | grep AudioTick` (addresses
  should be `2000xxxx`).
- `pow2_scale` and `note_to_inc` use 64-bit maths / a divide: **control rate
  only**.
- `CVOut2MIDINote` goes through the flash-resident `MIDIToDAC`: `main.cpp`
  only calls it when the note changes.
- Left-shifting a negative value is UB in C++17: write `x * 256`, not `x << 8`,
  for signed values.
- Audio/CV I/O is signed 12-bit (`-2048..2047`). `KnobVal()` is unsigned 12-bit
  (`0..4095`).
- **Never** do hardware setup in the `ComputerCard` constructor — it wedges the
  chip. Setup goes in `main()`. The card object is a function-local `static`
  (Mode 4's tape would overflow the stack).
- `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64` is required for the Workshop
  Computer's crystal — without it the card fails to boot on a cold power-up
  (it works from a warm reset, which is what makes the bug so confusing).

## Release process (for when this card ships)

Own repo (`uglifruit/WorkshopLightPen`) is where development happens. When
ready to release, the card gets PR'd against `TomWhitwell/Workshop_Computer`
via the fork at `../Workshop_Computer` (`origin` =
`uglifruit/Workshop_Computer`, `upstream` = `TomWhitwell/Workshop_Computer`)
— add the card under `releases/<n>_LightPen/` there and open the PR from a
branch on the fork. See the sibling cards' CLAUDE.md files (e.g.
`../WorkshopNibbleDrum/CLAUDE.md`) for the exact PR history and conventions
to follow.
