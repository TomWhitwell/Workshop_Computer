# BARBER'S POLE — hardware checklist

An ordered session with the card. The order matters: each step depends only on
things already confirmed, so when something fails you know the fault is in what
you just added rather than anywhere in the card.

**This is no longer a first bring-up.** The card has been played across several
sessions and twenty-six distinct faults have been found by ear and fixed. What
follows is therefore a *regression* checklist — each section names the symptom
that was actually heard, so a fault that returns is recognised immediately
rather than re-diagnosed from scratch.

**Flash `FLASHME/shepard.uf2`** (hold BOOTSEL, drag, it reboots on its own).

That file is refreshed by every build, so it always matches the source you
have. It did not used to be — it was copied by hand once and went stale,
holding a build from before two knob mappings were fixed. If the card ever
behaves in a way this document flatly contradicts, check that first:

    ninja -C build
    ls -l FLASHME/shepard.uf2 build/shepard.uf2

---

## 0. Before you plug anything in

- Headphones or a monitor on Audio Out 1 and 2.
- A sustained source for the live path — a pad, a drone, noise, a whole mix.
- Nothing patched into the inputs yet. The internal voice needs no input, and
  starting with a bare card removes a whole class of confusion.

There is no longer a DSP-load meter on CV Out 2; that diagnostic was removed
once it had served its purpose, and CV Out 2 now carries the window level. The
load was measured while it existed: the normal engine has comfortable margin,
and HIDDEN BARBER at four simultaneous voices sits close to the ceiling.

---

## 1. Does it boot at all?

**Switch MIDDLE, power on.**

| Expect | Meaning |
|---|---|
| LEDs 0, 2, 4 light together for ~0.75 s | normal boot latched |
| Then LED 4 steady, LEDs 0–2 showing a moving dot | running |

**If the LEDs stay dark:** most likely the crystal startup delay — check
`PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64` survived the build. The signature is
*fails on cold power-up, works from a warm reset*, so try a reset before
concluding anything.

**If LEDs 1, 3, 5 light instead:** the alt-boot latched when it should not
have. That is the WorkshopZX/WorkshopBio bug — the switch read before it
settles.

---

## 2. The illusion — the make-or-break test

**Switch MIDDLE. X to about 2 o'clock (8–10 layers). Y fully anticlockwise
(internal voice only). Main slightly clockwise of centre.**

Expect a pitch that rises and keeps rising, with no point at which it audibly
restarts. Listen for a full minute.

| Symptom | What it was last time |
|---|---|
| A click once per cycle | oscillator phases not rotating at the wrap (finding 4) — the description that solved it was *"the new overtone coming in at full volume"* |
| A level dip once per cycle | window sum not constant — run `shepard_check.py` |
| Pitch audibly "resets" | layer increments wrong; check `inc[i] = inc0 << i` |
| Plateau, then a click, then a jump | the glide advancing once per control block instead of per sample (finding 1) |
| Gentle beating, no distinct event | **normal** — see below |

**Then turn X fully anticlockwise (3 layers).** This is the exposed case: the
fewest components, so the least masking.

**Expect audible gentle beating here, and do not treat it as a fault.** Three
widely spaced oscillators beat against each other. The host analysis reports
N=3 as *"no unique jump"* — the signal has no discontinuity large enough to
measure against — so that case rests on listening, and it has been listened to.
What would be a fault is a *click* or a distinct event at a regular interval.

Compare against `tools/out_wrap_fast_N03.wav` if in doubt — that render glides
an octave per second, so a seam would appear as a 1 Hz rhythm.

---

## 3. Speed, and the perceptual limit

- **Main anticlockwise of centre** — pitch falls forever.
- **Main at centre** — *completely still*. Any slow wandering means the
  deadzone is not zeroing the rate.

Sweep Main slowly from centre outward. About **85% of the travel should sit
inside the illusion**; past that it becomes a siren, and that is deliberate.

**A Shepard tone only works while you cannot track the octave cycle.** Above
roughly one octave per second you will hear "a climbing line, repeated" rather
than an endless rise — because that is what it is. This is the illusion's own
limit, not a defect, and every implementation has it. The knob curve was
reshaped twice to put that boundary near the top of the travel rather than
halfway (finding 5).

---

## 4. Density

Sweep **X** slowly, fully anticlockwise to fully clockwise, with Main gliding.

**The perceived loudness should not change.** That is what `1/√N` buys.

What *should* change is the texture: sparse and hollow at 3, smooth and
seamless at 11. The layer count steps discretely and each step is audible by
nature — adding a layer respaces the whole stack, so there is no "entering
layer" to fade (finding 8). Two attempts at fading it made things worse and are
recorded in CLAUDE.md.

**Specifically check the top of the travel.** The last layer used to drop the
perceived pitch by about a semitone, because at twelve layers the top one sat
above Nyquist — inaudible, but still counted in the window (finding 23). The
cap is now eleven. The step from 10 to 11 should sound like every other step.

---

## 5. The live path — level and blend

**Patch a sustained source into Audio In 1. Y fully clockwise.**

The input is **octave-stacked**: every partial transposed into the same
octave-spaced layers as the oscillators, each rising and fading through the
same window. The source should stay recognisable — that is the whole point of
this design, and it is the third one tried. A frequency shifter and a rising
comb were both rejected by ear first.

**This section is the most recent work and is the least confirmed.** Check:

- **Level against the internal voice.** Sweep Y fully anticlockwise to fully
  clockwise. Host says the two sit within about 2 dB at every layer count.
- **Level across density.** Sweep X with Y fully clockwise. The live path used
  to get quieter at low layer counts, because `1/√N` was applied to it when its
  layers are correlated rather than independent (finding 24). It should now
  hold steady.
- **The middle of the Y sweep.** It used to lose 3 dB at the midpoint, heard as
  the internal voice dominating everywhere but the extremes (finding 25). The
  crossfade is now equal-power and the midpoint should sit between the two
  sources, not below both.

**Expect the live path to still read slightly softer**, and do not chase it.
Transposed copies of one source peak together where independent oscillators do
not, so at matched peak level it carries about 6 dB less average energy. Raising
its gain to match by ear would clip it.

**Grain length varies with shift ratio** — downward-transposed layers have
audibly longer grains than upward ones. That is inherent to a fixed-window
granular shifter; compensating for it measurably wrecks the downshifts
(finding 16).

---

## 6. Freeze and HOLD

- **Short press Down** — the stack stops moving but keeps sounding. LED 3
  steady. **Press again** — resumes.
- **Press several times quickly** — must toggle reliably every time. If it
  works only sometimes the debounce has failed: contact bounce toggling an even
  number of times leaves it off.
- **Gate into Audio In 2** — holds while high, and resumes from exactly where
  it paused rather than jumping ahead.
- **Gate into Pulse In 2** — reverses direction while held.

**HOLD moved here from Pulse In 1**, which now only ever steps or strikes. Two
things to check, because Audio In 2 is an audio input rather than a comparator:

- **A slow edge must not chatter.** Feed it a slow envelope or an LFO rather
  than a square gate. The pole should stop once, cleanly, not stutter around
  the threshold. There is 0.6 V of hysteresis for this.
- **Unpatched, it must do nothing.** ComputerCard zeroes a disconnected input,
  so with nothing in that jack the glide should never hold.

---

## 7. Page 2, and pickup

**Flick to UP.** LED 5 lights instead of LED 4.

**Every knob should be inert until you move it**, with its LED pulsing to say
so (left to right: Main, X, Y). Without pickup, every page change was three
unintended edits at once (finding 10). A knob that jumps the moment you arrive
is a pickup failure; a knob that stays dead *after* you move it past about 4°
is a capture-band failure.

- **Main** — quantisation. Anticlockwise quarter is smooth; beyond that the
  glide snaps to scale degrees. Steps should be articulated but not clicky.
  **Check that the steps are even in time** — a whole tone should hold no
  longer than a semitone.
- **X** — stereo width, deliberately subtle. **Check the mono sum**: L+R must
  not thin or cancel at any setting. Wider schemes were tried and reverted
  because they break the fusion the illusion depends on (finding 22).
- **Y** — output level. Flick between pages with Y at an odd position and
  confirm the volume does not jump.

**Flick between MIDDLE and UP repeatedly, including quickly.** No lurch. Page
changes are deferred 12 ms because Up and Middle are not adjacent.

---

## 8. Pulse In 1 — stepping

**Page 2 Main past its first quarter** (a stepped mode). **Page 1 Main at
centre.**

Each pulse advances exactly one scale degree. No skipped or double steps — the
trigger is counted rather than flagged specifically so a pulse arriving at an
awkward moment is not lost.

**Then move Main off centre: the glide should run AND the pulses should nudge
it.** This is the combination that could not work while Pulse In 1 also froze
the glide — the step arrived and the gate then pinned the pole for its whole
duration, so the pole never climbed between steps. If the glide stalls while a
gate is high, HOLD has found its way back onto this jack.

---

## 9. CV outs and CV ins

- **CV Out 1** — the master phase as a 0–1 V ramp, one octave per cycle.
  Patched to an oscillator's V/oct, it should climb *in tune* with the pole.
- **CV Out 2** — the window level at that phase. Patch CV Out 1 to an
  oscillator and CV Out 2 to a VCA and you get one layer of the stack made
  external: rising while it swells and fades.
- **CV In 1** — park Main exactly at centre, then patch an LFO. The glide
  should respond. This tests that CV is summed *after* the deadzone.
- **CV In 2** — density offset, summed with X.

---

## 10. HIDDEN BARBER (alt-boot)

**Hold the switch DOWN while powering on.** LEDs 1, 3, 5 light — odd LEDs mean
alt-boot.

The pole climbs **silently**. A trigger on Pulse In 1 voices a ping at whatever
pitches it has reached, and that ping decays *without climbing*.

| Check | Expect |
|---|---|
| Single trigger | a struck, decaying chord of octaves — not a click, not a drone |
| Repeated triggers | each lands at a *different* pitch, building a chord |
| Hold the trigger high | the pole keeps climbing underneath (finding 21 — it used to freeze) |
| Repeated triggers, stepped scale | the pitch must drift with the POLE, not walk up a degree per strike — the trigger steps the scale in normal boot only |
| Gate into Audio In 2 | the pole holds, so every strike gives the SAME chord until released |
| Page 2 Main (quantise) | changes the scale, and **must not change the decay** |
| Page 2 X (decay) | 17 ms click to a 1.7 s ring, and **must not change the pitch** (finding 20 — one knob once drove both) |

**Self-patch Pulse Out 1 to Pulse In 1** and it strikes itself once per octave.

**Trigger fast, with a long decay and the input mixed in, and it WILL
distort.** Four voices of eleven layers each with the live path running is close
to what an RP2040 can do at 48 kHz. That is the processor running out of time,
not a fault, and no setting removes it — shorten the decay, use fewer layers,
or play more sparsely.

---

## Recording what you find

Put it in `CLAUDE.md` as a numbered finding, with:

- **the symptom as heard**, in the words used at the time — several of these
  faults were solved by the description rather than by the measurement
- what it measured
- what fixed it, and what *didn't*

The wrong turns are worth as much as the fixes. Four of the twenty-six findings
are changes of mine that made things worse, and they are recorded as such so
the same idea is not tried again.
