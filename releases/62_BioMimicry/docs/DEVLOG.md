# BioMimicry — development log

Why the card is built the way it is, and what listening to it changed. Written
as we went, newest last.

---

## 0.1.0 — the five engines

Built the scaffold into a working card: five physics engines driving four agents,
synthesized voices, an alt-boot PCM path, and the CV/gate routing the Workshop
Computer's real I/O allows.

Three hardware constraints shaped the whole design and are worth restating,
because they look like arbitrary choices otherwise:

- **There are two pulse outs, not four.** Agents 3 and 4 fire as calibrated 5 V
  blips on the CV outs, so every agent still gets a physical trigger.
- **There is one switch, not two.** A Down tap cycles the mode; the Up/Middle
  position selects routing. Holding Down at power-on picks the boot mode.
- **There are six LEDs.** With five modes the sixth showed activity; at six modes
  they share (see 0.2.0).

Physics run at a **1.5 kHz control rate** (every 32nd sample) while voices, gates
and CV run at the full 48 kHz. That split is what makes four agents of Kuramoto
coupling affordable, and 0.67 ms of timing granularity is far finer than the ear
resolves for triggers.

### What simulation caught before any hardware trip

`tools/simulate.py` models the engine maths and reports trigger rates. Four real
defects, none of which a compiler would have found:

| Defect | Symptom |
|---|---|
| Kuramoto coupling ~2% authority | Frogs could **never** synchronise — the model was decorative |
| Geese excitation saturating | Knob was a switch: silence, or a flat 16 Hz buzz |
| Rain topping out at 100 Hz | Hiss, not rhythm |
| Meteors topping out at 47 Hz | Same |

A useful lesson from this round: **flat trigger rates do not mean coupling is
broken.** Synchronised oscillators fire at the same rate, they just align in
time. Measuring the Kuramoto order parameter showed the coupling working when
the rate table suggested it wasn't.

---

## Reality check — the gaits were wrong

Asked directly whether the horse gaits were actually correct. They were not.
Three separate errors:

- **The gallop was byte-identical to the walk.** A `kGaitSteps` table meant to
  compress it into a burst-plus-suspension was `{4,4,4,4}` and silenced behind a
  `(void)` cast. The knob's entire top quadrant was a faster walk.
- **The walk wasn't a walk.** It fired both forelegs consecutively. A real walk
  is 4-beat *lateral* — same-side legs follow each other.
- **The canter had no suspension.** Evenly spaced 90° beats read as a waltz.

Underneath was a structural flaw: **four free-running clocks at 0.95–1.03×
cannot hold a gait.** A trot's diagonal pair separated within seconds. The drift
was the mode's poetry, but only the walk survived it.

Rebuilt around **one stride clock** with per-hoof landing offsets from real
equine footfall timing. Chaos jitters each hoof's *timing* rather than its rate,
so gaits stay locked however long they run. `python tools/simulate.py gaits`
asserts all four biomechanically.

Also this round: Rain got downstream splash coupling (it was statistically close
to Meteors — Poisson with a refractory period), Geese and Meteors went to
12-member swarms, and Pulse In 2 became an entrainment clock rather than a second
spook.

---

## 0.2.0 — Cicadas, round robins, stereo

Added a sixth ecosystem coupling on **amplitude**, where Frogs couple on phase
and Geese on events. It took two attempts, both caught in simulation:

1. With insects tiring only when *calling*, the swarm self-organised into an even
   spread and never surged — defeating the entire point of the mode. Ambient
   fatigue (being in a loud field is itself tiring) more than doubled the swing.
2. The knob still did nothing, because feedback speeds calls while fatigue is
   call-driven — they cancelled. Scaling **both halves** of the loop by the knob
   fixed it.

Round robins were wired for meaning rather than variety: in Horses the variant
**is the hoof**, hinds pitched lower and heavier than fores. Stereo placement
comes from the ecosystem — fixed for the horse, spread for flocks, random per hit
for rain and meteors.

Six modes filled all six LEDs, so mode and activity now share one light: the
active mode glows dim and flares to full on each trigger.

---

## Hardware: "the horse sounds like one horse"

Correct, and a real bug rather than a tuning problem — one I introduced when
consolidating to a single stride clock.

`HorsesEngine::tick()` **never read `c.population`**. Knob X only reached the mode
through the generic agent mask, which here removed *legs from one animal*.
Turning it up added nothing; turning it down made a lame horse.

Population is now a **herd**: one stride clock per horse, all four hooves intact,
with per-animal speed offsets so the animals slide in and out of step. That is
the phasing the old per-leg drift was reaching for, at the level where it doesn't
destroy the gaits. `EngineOut::member` carries which hoof landed so each animal
plays all four of its own.

---

## Hardware: trot and canter sound like a density drop

Also correct, with an acoustic explanation. Trot and canter were the **only**
gaits with mathematically coincident landings — and two identical clops fired on
the same sample don't sound like two hooves, they **sum into one louder clop**.
So a trot genuinely was half the density of a walk, not just perceptually.

Real diagonal pairs land 10–30 ms apart. The second foot of each pair now holds
back by a fixed delay (trot 12/18 ms, canter 15 ms), given in absolute time
rather than as a fraction of the stride — a real animal's flam doesn't stretch
with tempo. Walk and gallop already had four distinct landings and are untouched.

Frogs also went to a pond of twelve. That needed the Kuramoto sum rewritten
mean-field (O(n) instead of O(n²)) — **and the first attempt had the angle
identity's terms swapped and was 100% wrong.** Checking against a direct O(n²)
sum over 300 random ponds caught it; the corrected form agrees to 0.006%. Worth
recording because it compiled and ran silently.

---

## Alt-boot became Drone

Alt-boot was spending a whole boot mode on a sample toggle. Sample playback is
now a **build-time** choice (bake `samples/` or don't), which freed it for
**Drone**: the same six engines driving sustained tone instead of triggers.

Both Audio In jacks were also completely unused. Audio In 1 is now loudness (a
loud room agitates geese, silences cicadas), Audio In 2 transient-sensitive —
sudden movement alarms animals, steady noise doesn't.

---

## Hardware: "samples playing far far too slow — like racing cars"

They were not playing at all. **Every boot was entering Drone mode**, whose old
Meteors root was a 40 Hz saw pair through a sweeping filter — which is exactly
what a passing vehicle sounds like.

`ComputerCard` derives the switch from `knobs[3]`, which comes off a ~60 Hz
smoothing filter **initialised to zero — and zero decodes as `Switch::Down`**.
For roughly the first 5 ms of every boot the card reports Down wherever the
switch actually is. The boot window latched on "Down seen at any point", so it
latched on *every* boot.

Two follow-ups from the bench settled it: *"both boot modes seem to be doing the
same"* pointed at the latch, and *"four sounds, rising in pitch"* was the synth
hoof pitches (153/202/164/216 Hz), proving the PCM path was never reached. The
fix is a single reading after settling — what WorkshopZX's `BootSelector` does,
and what is proven on this hardware. I had claimed to be following that pattern
and was not.

There is now a **boot splash** — Rhythm lights the left LED column, Drone the
right — because the whole diagnosis was slow for want of any way to tell the
modes apart.

Found while tracing it: the PCM end-of-sample test used `pcmLen << 16`, which
wraps above 65536 bytes. It was silently truncating the two longest meteor
swooshes to under half their length.

---

## Drone was useless, and rightly called out

The first Drone mapped engine `state[]` to oscillator pitch. Most engines put a
**phase ramp** in `state[]`, so the pitch swept upward and snapped back forever,
in half the modes — and it ignored the entire sample library in favour of saw
oscillators.

Rebuilt with **no oscillators**: Drone now granulates the same recordings, up to
eight overlapping grains per voice started at random points and played stretched.
The engines drive grain *density* and *spread*, never pitch. Grain periods were
sized against the real sample lengths so overlap lands at 2–4×; the first pass
asked for 16× overlap against four grain slots, which would have stolen grains
mid-playback and chopped.

---

## Hardware: cicadas galloping

Reported as clusters then silence, *"almost like galloping horses"* at CW. That
detail was the diagnosis — it said the clusters were **periodic**. Measured:
every 0.32 s, standard deviation 0.01 s. A metronome.

**My first guess was wrong.** I assumed the twelve insects shared one fatigue
time constant, gave each its own recovery rate and stamina, and it stayed
rhythmic in every configuration. Measuring phase clustering instead found the
real problem: phases converged from R=0.24 to **R=0.88 within five seconds** and
froze, because every insect ran off one shared rate law with no natural frequency
of its own. Detuning them dropped clustering to R=0.26.

That stopped the unison but the field still pulsed, for a reason no amount of
tuning fixes:

> One shared field, plus "everyone speeds up when it's loud and tires when they
> call", **is a relaxation oscillator**. Charge, discharge, repeat. It has exactly
> one period.

So the field became **four patches** that mostly hear themselves. Patches charge
and discharge independently — measured correlation **0.01** — and cluster spacing
went from a fixed 0.32 s to an irregular 0.4–8.6 s (cv 0.06 → 0.44–0.78).

*Note for anyone reading `simulate.py`:* `cicadas_swing` reports **one patch**,
not the mean. The mean is deliberately flat because independent patches cancel,
and that cancellation is the point.

---

## Real samples, and a way to change them

42 Pixabay recordings, already 48 kHz 16-bit mono, with levels spanning ~36 dB.
All of them fit (834 KB of 1943 KB free), so rather than discarding
three-quarters of the round robins the firmware went to **eight variants** —
except Horses, whose variants are the hooves.

Levels are matched by **RMS, not peak**. A peak is usually one transient: the
quietest goose had a body at RMS 0.004 against another at 0.059, yet both peaked
near 1.0, so peak-normalising would have left the quiet one still sounding quiet.
Corrections ran −15.5 dB to +20.6 dB; the library came out matched to 1.07×.

The web editor then removes the toolchain from the loop entirely: drag WAVs
into a browser, they are converted and loudness-matched there and streamed over
WebMIDI SysEx into a reserved 1 MB flash region, overriding the baked recordings
per slot. The card mutes during the write because flash writes halt execution —
honest about the constraint rather than glitching through it.

The firmware image now sits only ~95 KB below that region, and adding baked audio
would silently push it over, making flashing destroy uploads and uploads destroy
firmware. `tools/checksize.cmake` reads the real image end from the ELF and fails
the build if it ever reaches the boundary.

---

## A developer's correction: /32 does not buy you time

A Workshop Computer developer read the code and pointed out:

> *"Unless I misunderstand, calling the physics once per 32 samples doesn't help
> with performance — because the physics still needs to finish within that ~20us
> sample in which it is run?"*

He is right, and it invalidated a claim we had been repeating.

`controlTick()` is called **inline** from `ProcessSample()`, which runs inside
the DMA interrupt handler (`ComputerCard::AudioCallback`). So on the one sample
in 32 where the physics run, the whole engine still has to finish inside that
single **20.83 µs** slot. Dividing by `kCtrlDiv` lowers the *average* load. It
does not move the deadline.

The figure in this log — "~55 cycles/sample amortised against a 2604-cycle
budget" — was therefore measuring **throughput**, not the thing that decides
whether audio glitches. What matters is the **worst single sample**, and that had
never been measured. Static instruction counts could not settle it either: the
Geese tick is 1233 instructions but contains 243 branches, so the count is a very
loose upper bound rather than a real path.

The honest response was to measure rather than argue, so `profile.h` was added:
a SysTick-based cycle counter around the whole callback and each of its phases,
reporting the worst case on the LEDs (one LED per ~16% of budget, all six
flashing on an overrun). It compiles to nothing unless `-DBIO_PROFILE=ON`, and
this was verified by checking that the normal build is byte-for-byte identical to
the released firmware.

Geese is the mode to watch: its excitation spread is the only O(n²) path left, and
a full cascade is 12×11 = 132 inner iterations in one tick. Frogs was already made
O(n) by the mean-field rewrite.

> **Both halves of that paragraph turned out to be wrong.** Geese measured
> *fourth* of six, and Frogs' O(n) rewrite is still second worst. See "The
> measurements, and two wrong predictions" below. Left here unedited because the
> reasoning looked sound right up until it was checked.

His second point stands too: if the measurement shows headroom, the control rate
can go *up* for finer trigger timing. That is a one-line change to `kCtrlDiv` —
but worth making only once the headroom is a number rather than an assumption.

**Status: measured — see below.**

---

## The measurements, and two wrong predictions

Three rounds of hardware readings settled it. The first two rounds each fixed a
real problem and each ended with a guess about what would matter next. **Both
guesses were wrong**, and the sweep that proved it took ten minutes.

### Round 1 — the overruns were XIP flash misses

Moving the hot path into RAM: Engine 3.5× faster, Voices 3×, Outputs 4×,
overruns down ~92%. Everything the card computes itself came inside budget.

### Round 2 — USB was the entire remaining problem

`tud_task()` measured **29895–35850 cycles**, up to 14× the whole 20.83 µs
sample. TinyUSB's device stack is unbounded by design and was being called from
inside the audio interrupt. It moved to core 1.

**That worked completely.** USB now measures **0 cycles** on core 0 in every mode.
It is the one unambiguous success in this log.

The same round split `uiTick()` (LED rendering) half a divider away from the
engine so the two costs never land in the same slot. That worked too: Outputs
peaks at 336–526 cycles across all six modes.

### Round 3 — the full sweep

| Mode | Engine | Total | Overruns |
|------|-------:|------:|---------:|
| **Cicadas** | **7830** (300.7%) | 8708 (334.4%) | 1225 |
| **Frogs** | **5606** (215.3%) | 6509 (250.0%) | 1640 |
| Horses (pop 4) | 3847 (147.7%) | 4581 (175.9%) | **11030** |
| Geese | 3118 (119.7%) | 3900 (149.8%) | 249 |
| Drips | 2648 (101.7%) | 3530 (135.6%) | 3 |
| Meteors | 2139 (82.1%) | 2915 (111.9%) | 3 |

Budget is 2604 cycles. **Every mode is over on Total. Four of six are over on
Engine alone.**

### The two wrong predictions

**"Geese is the mode to watch."** It is *fourth*. The reasoning was that its
excitation spread is the only O(n²) path left, 12×11 = 132 inner iterations. But
Cicadas is **2× worse** than Horses and 3× over budget with no O(n²) path at all
— it walks its swarm up to four separate times per tick (spook, loudness, main
loop, patch reduction), and each iteration is heavy. **Iteration count did not
predict cost; per-iteration weight did.** Same class of error as the "/32 buys
you time" claim: reasoning about the code instead of measuring it.

**"Frogs was already made O(n) by the mean-field rewrite."** True, and it is
still second worst at 5606 cycles. O(n) is not the same as cheap.

### Peak and frequency are different faults

The overrun counts do not track the peaks. Horses has **11030** overruns at 3847
cycles; Cicadas has **1225** at 7830. Peak says *how far* over, overrun count says
*how often*. Horses overruns constantly by a little, Cicadas rarely but
massively. Both are audible, differently — and a fix that only chases the peak
would leave Horses' 11030 in place.

### Horses: the cost is mostly fixed, not per-agent

Sweeping population on Horses:

| Population | Engine | Overruns |
|-----------:|-------:|---------:|
| 1 | 2065 (79.3%) | 3 |
| 4 | 3847 (147.7%) | 11030 |

Three extra horses cost 1782 cycles — **~594 per horse**, leaving a fixed floor
around **1470 cycles**, roughly 70% of the population-1 cost. The `Engine` scope
wraps all of `controlTick()`, so knob reads, CV reads, clock tracking and voice
dispatch are inside that floor. Two loops in the Horses tick also run
`kNumAgents²` = 16 iterations regardless of population (`c.spook` reset and the
`gait != lastGait_` reset), ignoring the early-out at `engines.cpp:143`.

A prediction that per-agent splitting would quarter the cost was therefore also
wrong: it caps the variable part but leaves the floor untouched.

### What the numbers rule out

The developer's second point — that headroom could buy a *higher* control rate —
is dead. There is no headroom. `kCtrlDiv` cannot go below 32 until Engine fits.

Per-mode micro-optimisation is the wrong strategy: the two modes worth starting
on by intuition (Horses, Geese) are third and fourth. The costs share one shape
— **every engine does its whole swarm in one tick, inside one 20.83 µs slot** —
and `kCtrlDiv` cannot help, because it lowers average load and not the deadline.

**Caveat on this data:** one reading per mode, and peaks reset on each read. The
Horses sweep showed the range *within* one mode is wider than the gap between
modes, so Drips and Meteors sitting at 3 overruns is provisional — it may only
mean that knob position was never swept.

**Status: measured. Fix not yet attempted.**

---

## Drone was the real glitch, and the uploader never worked at all

The measured sweep above was **Rhythm mode only**, and that turned out to matter.

### The ear found what the sweep missed

Asked whether the overruns were actually audible, the answer was that they were
mostly masked — chirps and hooves are broadband and transient-dense, so a
one-sample discontinuity hides in material that already sounds like noise. Fair,
and it nearly stopped the work.

But then: discontinuity **in Drone**, on Geese, Frogs and Meteors, with a guess
that it was "the longer samples". Meteors measures 2139 cycles — the one mode
never over on Engine — so Engine could not be the cause. Reading timing in Drone:

| bucket | Rhythm (Geese) | Drone (Geese) |
|--------|---------------:|--------------:|
| Engine | 3118 | 3145 |
| **Voices** | **800** | **17546** |
| Total | 3900 | 18230 |
| overruns | 249 | **373862** |

**674% of budget, and 1500x the overruns.** The engines were never the problem in
Drone. `droneRender()` is, and unlike the physics it runs on *every* sample.

The cause was one line: a triangular grain window computing `(dist << 16) / half`
per grain per sample — 8 grains x 4 voices = **32 hardware divides at 48kHz** on a
core with no divider. `g.len` never changes once a grain launches, so the whole
thing was recomputing a constant 48000 times a second. The reciprocal is now
taken once at launch and the render loop multiplies.

The "longer samples" hunch was right, and better than my reasoning: longer grains
stay active longer, so more of the 32 divides are live at once.

### Getting the reciprocal right took three attempts

Worth recording, because the first two would have shipped audible bugs and a
Python check caught both before flashing:

1. **Q16 reciprocal** — `(0xFFFFFFFF/half)>>16`. Fine for short grains; on
   `meteors_5` (118596 bytes) it truncates to 1 against a true 1.105, a **10%
   window error**, and an uploaded 1MB sample truncates it to **0** — silence.
2. **Q48 reciprocal** — fixes the long grains, **overflows 32 bits** on short ones.
3. **Q32 with a clamped `dist`** — the range of `half` (8 to ~500000) is too wide
   for any single fixed scale. The survivor also needs `dist < half`, because an
   odd `len` lets `dist == half`, and `half * (2^32/half)` is exactly 2^32, which
   wraps to zero and turns the window's **peak** into silence.

Verified exhaustively against the original across every real sample length:
zero overflows, max error 0.19% of full scale on the shortest baked recording.

### The uploader deadlocked the card, every time

Reported as: web UI says it will go silent, then no sound, no LEDs, no control,
until a power cycle. Three separate defects, all in a path that had **never once
run on hardware**:

1. **The park loop was inside the DMA interrupt handler.** Core 0 spun in
   `ProcessSample()` waiting for core 1 to finish writing flash. Spinning for the
   seconds an erase takes starves the audio DMA, and it never restarts.
2. **`ProcessSample()` is `virtual`.** Merely *dispatching* to it reads a vtable
   that lives in flash — so with XIP down, core 0 faults before reaching any
   guard. The RAM-residency of the function body cannot help you get to it.
3. **Every upload erased the whole 1MB region** and `memset` the slot table, so
   replacing one 6KB recording destroyed the entire library, after a multi-second
   stall the browser read as a hang.

Fixed by giving up on the pretence. `EnterUploadMode()` disables `DMA_IRQ_0`
outright, the whole USB path is now RAM-resident, core 1 drives the progress
LEDs (core 0 is not running at all), and the card **reboots** when the upload
finishes. The web UI already said "the card mutes while uploading"; this makes
that true instead of aspirational.

Uploads are now incremental: the header is seeded from what is already on the
card and new audio is appended, so untouched slots survive. The append point is
rounded **up** to a sector boundary — erase works a sector at a time, and
starting mid-sector would have wiped the tail of the previous recording, which is
the exact corruption the change existed to prevent. Space is reclaimed with
"revert to built-in", which empties the region.

### Drips: the first 40% of the knob did nothing

Reported as basically never firing until ~40%, then firing often. Measured, and
exactly right: **0.00 triggers/sec everywhere below 45%**, then straight to 3.3.

This was not a knob-taper problem, so remapping the curve would only have moved
the cliff. Below a certain inflow the buckets leak as fast as they fill, so the
level sits at an equilibrium under the threshold and *nothing ever fires* — a
uniform random drop cannot cross it, no matter how the knob is scaled.

Two changes. The inflow law is square-rooted (`fast_sqrt_q16`, a new bitwise
integer sqrt — no libm, no float, no divide) so the bottom of the travel moves
fastest, with a small floor. And the drop is now **heavy-tailed**: cubing the
random keeps the mean low but lengthens the tail, so a weak downpour still tips a
bucket occasionally. That tail is what removes the dead zone, and it is closer to
real dripping, where water gathers and then lets go.

Result: drips from 2% of travel, rising smoothly to ~19/sec, under the ~25/sec
ceiling at which a rhythm stops reading as one.

The obvious `* 4 / 5` gain trim compiled to an `__aeabi_idiv` call **inside the
inner loop**, checked in the disassembly and replaced with a Q16 multiply. The
same M0+-has-no-divider lesson the Cicadas patch walk already learned.

**Status: all three built and staged, none heard on hardware yet.**

---

## Round two on all three, from hardware

### Drone: the divide was half of it

`droneRender()` went **17546 -> 9608** cycles. Real, and not enough — still 369%
of budget, and Geese still audibly glitched.

The other half was in the same loop and the same shape: `mul_q16()` widens to
`int64_t`, which on the M0+ is an **`__aeabi_lmul` library call**. Two of them per
grain per sample, up to 32 grains, at 48kHz. Confirmed in the disassembly rather
than guessed. Neither needed 64 bits: `|s| <= 2048` and `win <= 65536`, so the
product peaks at 1.3e8 and the accumulator at 5.4e8, both comfortably inside
int32. They are now plain 32-bit multiplies, and `droneRender()` contains **no
library calls at all**.

`droneUpdate()` had three more (`* 2 / 5`, `% 32`, `% kNumGrains`) — the first
became a Q16 multiply, the other two masks, since both divisors are powers of
two. It runs at control rate so it matters ~32x less, but it sits in the same
Engine bucket that measured 4676 in Drone.

The one remaining 64-bit divide is the window reciprocal at grain launch, which
is the whole point: one divide per grain instead of one per grain per sample.

### The uploader: the ack never left the device

"Upload did nothing, revert reset to stock samples" — so the header never
committed, and the browser saw nothing.

`tud_midi_stream_write()` only fills a FIFO. **`tud_task()` is what puts bytes on
the wire**, and this TinyUSB has no MIDI flush call. Every reply was queued and
then immediately followed by an erase, a page program, or a `busy_wait` before
the reboot — all of which block without servicing USB. So:

- chunk acks sat in the buffer while the browser waited for one before sending
  the next chunk, and
- the final ack died with the `watchdog_reboot()` 120ms later.

`Send()` now pumps `tud_task()` itself (guarded against unbounded recursion,
since it is called from inside `tud_task()`), and the reboot paths spin on
`FlushUsb()` instead of a blind `busy_wait`.

Worth noting this bug was invisible to the profiler reads, which worked fine:
those reply once and then return to the normal `Task()` loop, which flushes them
on the next iteration. Only the upload path blocks immediately after replying.

### Drips: right shape, wrong speed

The dead zone was gone but the first drips arrived faster than one a second,
where the ask was nearer one every four. Lowering the floor barely moved it and
lowering the gain capped the torrent at the top.

The fix was to stop treating gain as a constant: it now **ramps 0.4 -> 0.8 across
the sweep**, so the bottom is sparse without flattening the top. With the floor
raised to 9000, drips start at ~4% of travel at 0.41/s (one every 2.4s), pass 1/s
around 10%, and reach ~18/s at full.

**Status: built and staged; not yet heard.**

---

## The uploader, third attempt: masking an interrupt is not stopping a core

Two rewrites in, upload still hung the card. The mistake was the same both times,
just better hidden: **`irq_set_enabled(DMA_IRQ_0, false)` stops the interrupt from
firing again. It does not stop core 0 from executing flash.**

Three things were still live when `flash_range_erase` dropped XIP:

- `ComputerCard::AudioCallback` and `BufferFull` are **in flash**. Core 0 can be
  inside them at the moment of the erase — masking the IRQ does not evict it.
- `AudioWorker`'s outer `while(1)` is RAM-resident, but it returns into and calls
  flash-resident code.
- The CV outputs run a **second flash-resident ISR**, `PWM_IRQ_WRAP ->
  OnCVPWMWrap`, which was never masked at all and kept firing throughout.

Any one of them faults the chip. `ProcessSample` being `__not_in_flash_func` was
never the point: what matters is everything *around* it.

Core 0 now parks inside `ProcessSample` itself — which is RAM-resident — mutes
its outputs, sets `core0Parked`, and **spins forever**. It never returns, so the
flash-resident caller never runs again. Core 1 raises `uploadMode`, waits for the
acknowledgement (the next callback is 21us away), and only *then* masks both
IRQs and touches flash. Order matters: masking first would mean `ProcessSample`
never runs again, never sees the flag, and never parks — a deadlock built out of
the fix for a deadlock.

This is safe now in a way it was not in the first attempt, and the reason is
worth stating: USB moved to core 1, so spinning core 0 no longer stalls the very
transfer it is waiting on.

### Two browser bugs, both reported rather than found

- **The file picker opened twice per sample.** Each slot was a `<label>` wrapping
  a hidden file input, so the browser opened the dialog natively *and* the
  slot's `onclick` called `inp.click()` again. Now a `<div>`.
- **"Erasing… the card is muted while this runs"** described the old behaviour,
  where every upload wiped the whole 1MB region. It erases only the sectors it
  needs now and reboots at the end, so the message says that instead.

### Drone has plateaued

Two reads of the identical build: Voices 3398 then 3499, overruns 3688 then 7321.
That spread is run-to-run variance, not a regression — the mode sits at ~130% of
budget and there is nothing left in the loop to remove. Further gains need fewer
grains or fewer voices, which is an audible change to the texture and therefore a
decision to make by ear rather than an optimisation to slip in.

**Status: built and staged. The uploader has still never completed.**

---

## The uploader works — and the LEDs are what found it

Six attempts. The one that mattered was not a fix at all: it was giving up on
guessing and making the card **say where it stopped**.

`WebUI::stage` counts the steps and core 1 renders it on the LEDs as a binary
number. First run reported **LEDs 0 and 2 — stage 5**, and that single reading
ended the guessing. Everything hard had *already worked*: core 0 parked in RAM,
boot2 primed, the header read, and the **erase completed without faulting**. It
hung on the very next line — sending the ack.

Which meant the flash hazard, the thing three rounds of work had been aimed at,
was never the problem after the park went in.

### The real constraint

**TinyUSB's device stack is entirely in flash**, including its `USBCTRL_IRQ`
handler `dcd_rp2040_irq`. So core 1 cannot service USB and write flash: *the
stack it needs lives in the memory it is erasing.* Masking `DMA_IRQ_0` and
`PWM_IRQ_WRAP` was never sufficient — the moment interrupts came back, a pending
USB interrupt vectored into dead flash and the stack never recovered. The ack was
queued and could never leave.

That is why patching details kept failing. The design could not work, so the
design changed:

> Receive the **whole** upload into a 128KB RAM buffer with everything running
> normally — audio, USB, LEDs — then ack, flush, and touch flash exactly **once**,
> after the last byte has already arrived.

The card stops being a synth only for the final commit, not for the transfer.

*(Later correction: it stops well before that. USB is modal — holding the switch
ends the ecosystem and hands the card over, so it is silent for the whole
session, not just the commit. The claim above described the transfer mechanism
and quietly became a claim about what you hear, which is not the same thing.)*

### What it cost

One upload is now capped at **128KB by RAM**, not by the 1MB flash region.
`MSG_INFO` reports the staging size so the browser rejects on the true limit. The
largest baked recording is meteors_5 at 116KB, so one big sample or several small
ones fit per pass; a whole library does not. RAM sits at 69%.

Slot offsets are staged as buffer offsets and rewritten to flash offsets at
commit once the append base is known, with a `touched_` table so untouched slots
keep their existing offsets instead of being shifted.

**Confirmed on hardware: uploads complete, and the samples survive the reboot.**

### The lesson, which is the same one as last time

Every real advance this session came from a measurement, not from reasoning about
the code. The profiler found the Drone divide. The Drone *listening* found what
the Rhythm-only sweep missed. The stage LEDs found that the flash write was
already fine and USB was the casualty. Three times running, the confident
diagnosis was wrong and the instrument was right.

When a fix fails twice, the third attempt should probably be a measurement.

---

## v1.0.1, and what v1.1.0 is for

**v1.0.1 is corrective.** Everything in it fixes something that was wrong in
1.0.0 and was found by playing the card, not by reading it:

- Drone's audible glitch — `droneRender()` **17546 -> 3398** cycles, overruns
  down 99.2%, by removing a per-sample divide and two `__aeabi_lmul` calls.
- Drips' dead first 40% of travel — the buckets leaked as fast as they filled, so
  no knob taper could have fixed it; the inflow needed a heavy tail.
- Meteors' too-busy CCW end — **0.53/s -> 0.06/s**, an empty sky you wait on.
- An uploader that hung the card, now working, incremental, and RAM-staged.

### v1.1.0: decouple what the two cores do

The remaining problems are all the same problem. Core 0 does **everything**
audio — physics, voices, outputs, LEDs — inside one 20.83 µs interrupt, while
core 1 does nothing but poll USB. In Drone that leaves core 0 carrying ~7000
cycles against a 2604 budget while core 1 idles.

The plan, in the order the constraints force:

1. **USB becomes modal.** Hold the momentary switch to enter an explicit upload
   mode; normal operation never calls TinyUSB at all. This is the user's
   suggestion and it is the right one: sample management is a setup activity, so
   the card should not carry the USB stack, its flash-resident ISR, or the
   enumeration risk while performing.
2. **That frees core 1**, which is the only reason step 3 is possible.
3. **Split engine and voices across the cores** — the `second_core` pattern from
   `../WorkshopZX`: one core free-runs the physics at control rate, the other
   handles 48kHz I/O and rendering, communicating through a lock-free struct with
   a single writer per field. Drone's Engine (4924) and Voices (3499) would stop
   stacking in the same slot; the worst core carries ~4900 instead of ~7000.

**What this does not fix:** Voices alone is 3499, still 134% of budget. Splitting
stops the costs stacking; it does not make the granular renderer cheap. Getting
Drone fully clean still means fewer grains or fewer voices, which thins the
texture — an audible trade, and one to make by ear.

**The known cost:** the profiler is read over USB, so in a modal design timing
can only be read after playing, by entering upload mode. Peaks are already sticky
until read, so this is survivable, but it is a real loss — every fix in this log
came from reading those numbers.

---

## v1.1.0 Stage 0 — does a busy core 1 starve core 0?

The whole v1.1.0 design rests on one assumption: that core 1 can run the physics
without hurting core 0. That is not obvious, because **PCM playback reads flash on
every sample** — `droneRender()` reads `g.pcm[idx]` from XIP-mapped flash at
`voices.cpp:446`, and today that is free only because core 1 is idle.

So before writing any of the split: a throwaway build where core 1 hammers flash
in a 4096-byte read loop, which is far harsher than the real physics (those idle
~90% of each tick). Checked in the disassembly that the loop survived
optimisation, because a probe that compiled away would have given a falsely
reassuring answer.

Drone Geese, against the same mode's baseline:

| bucket | baseline | busy core 1 | delta |
|--------|--------:|-----------:|------:|
| Voices | 3499 | **3092** | **−12%** |
| Engine | 4924 | 5419 | +10% |
| Outputs | 528 | 839 | +59% |
| Total | 6976 | 7894 | +13% |

**The assumption holds.** Voices — the PCM path, the thing at risk — went *down*,
not up. Core 1 activity does not starve core 0's sample reads, so the split is
sound and the physics can move.

The cost landed somewhere else entirely: **Outputs +59%**, Engine +10%. Contention
hits register work and non-PCM flash access, not the audio data. Worth watching at
Stage 3 rather than assuming it is free, but bounded, and from a probe far worse
than the real thing.

---

## v1.1.0 Stage 1 — USB becomes modal

Sample management is a setup activity, so the card should not carry USB while it
is being played. It now doesn't: **`tusb_init()` is never called until the
momentary switch is held for two seconds.** Until then the card does not enumerate,
`USBCTRL_IRQ` (whose handler lives in flash) is never armed, and core 1 sits in a
`tight_loop_contents()` doing nothing.

That idle wait is the point. It is the space v1.1.0 puts the physics into, and it
is why USB had to become modal first.

**The switch had to be rebuilt to allow it.** The old handler fired the mode
change on the *first* control tick Down was seen (`main.cpp:316-332`), which cannot
coexist with a hold — starting the hold would cycle the ecosystem out from under
you. The mode change now fires on **release**, and a hold past `kHoldTicks`
consumes the tap. The LEDs fill left to right as the hold counts, so the gesture is
watched rather than guessed at; all six stay lit in USB mode, which is the
"power-cycle to play again" indicator.

Two smaller corrections fell out of it:

- **The profiler's Down-tap reset is gone.** It conflicted with mode cycling — one
  tap both changed the ecosystem and threw away its numbers.
- **Entering USB mode deliberately does *not* reset the peaks.** That is the exact
  moment you stop playing in order to go and read them, so wiping them on entry
  would guarantee a reading of zero. The web UI now says so next to the button:
  play the mode, *then* hold, connect, read.

The browser also watches `onstatechange`, so a card that enumerates while the page
is open is picked up without pressing Connect again — "card not found" now much
more often means "not in USB mode yet" than "unplugged".

**Status: built, not yet flashed. Expected: every mode identical to the tables
above — anything that moved means the switch rework broke something.**

---

## v1.1.0 Stage 3 — the physics leave the audio interrupt

**Zero overruns.** First time in this whole investigation that a mode has been
inside budget.

| mode | before | after | |
|------|-------:|------:|---|
| Geese | 3900 (150%) | **2209 (85%)** | overruns 249 → **0** |
| Frogs | 6509 (250%) | **2291 (88%)** | overruns 1640 → **0** |

Frogs is 2.8× faster — from second worst to comfortably inside. And the rhythms
sound right, which was the real risk: the trigger ring is neither dropping nor
reordering.

### The split itself

`controlTick()` keeps only what genuinely needs core 0 — the switch, draining
note-ons, applying gate/CV targets — and everything else is `physicsTick()` on
core 1. **Core 0's control work went 2411 → 185 cycles.**

Note-ons cross as **packed 32-bit words through a ring**, not as calls, and that
is not a stylistic choice: `note()` writes ~20 fields of `Voice` including a
128-entry `ks[]` buffer, while `render()` mutates the same struct every sample.
Calling it from core 1 would tear the struct, and a half-applied note-on means a
new `pcm` pointer with an old `pcmLen` — an out-of-bounds flash read, not a
glitch. The whole payload fits in one word, so the store cannot tear.

### Two things the numbers corrected

**"Engine 2411" was measuring the wrong thing.** After the split that scope wraps
`controlTick()` — which no longer contains the engine. A stale label, and exactly
the class of mistake that had me misreading this profiler before. The buckets are
now named for what they measure: `Ctrl`, `Voices`, `Outputs`, `Notes`. There is
no Engine row on core 0 because there is no engine on core 0.

**The residual was my own.** With the labels fixed, `Notes` read 2040 — the whole
remaining problem. `selectVariantAndPan` had **three software divides**, added
during the round-robin work, one of them **inside a do-while retry loop that can
spin**; plus `VariantCount()` and `PickUserVariant()` each walked eight slots
through XIP flash *per note-on*, for an answer that only changes when samples are
uploaded (which reboots the card). Fixed by advance-and-wrap instead of
retry-until-different, tables resolved once in `init()`, and a mask-and-fold
instead of `% 17` in the Random pan case. 430 instructions → 166, three divides →
none.

Note-ons are also **drained one per sample** now, on the samples that have neither
the control tick nor the UI tick. A full Geese tick fires four at once, so taking
them together put ~1200 instructions on a single sample — the very cost the split
removes, just relocated. The ring already decouples consumption from production,
so there was never a reason to take them all in one slot.

### A measurement that said "stop"

Frogs' `Notes` is **1332** against Geese's **1356** — identical within noise.
Frogs is the mode with the Karplus-Strong length calculation: two chained divides
plus a 128-entry noise fill, and the obvious next thing to cache. The numbers say
it costs nothing measurable. Left alone.

**Status: Rhythm fixed and confirmed on hardware. Drone still runs the old path
on core 0 — Stage 4.**

---

## Voice truncation: what a fix can and cannot be

Reported as samples audibly cut off when hits overlap. Two causes, one of them
invisible from outside: there was **one voice per agent, hard-retriggered**, and
**the PCM path had no envelope at all** — `v.env` was written by `note()` and only
ever read inside the synth branch, so a recording played flat to its end and a
retrigger jumped straight to the new sample's first byte.

Now a pool of 8 with an allocator that steals the voice *furthest through its
recording* — the least audible thing to interrupt — and a 2 ms fade-in on every
note so a steal is a crossfade rather than a step.

### It cannot be "fixed", and that is not a cop-out

Voices needed for **zero** steals, being sample length × trigger rate × agents:

| mode | sample | voices for zero steals |
|------|-------:|----------------------:|
| Drips | 0.10 s | ~8 |
| Cicadas | 0.15 s | ~10 |
| Geese | 0.18 s | ~12 |
| **Meteors** | **2.47 s** | **~168** |

`meteors_5` alone would want 168 voices. Any sampler with finite polyphony steals
eventually, and a user uploading a five-second pad into Drips will steal
constantly whatever the pool size. **The goal was never zero steals — it is that a
steal does not sound like a fault.** Confirmed by ear: at X≈50%, full Geese is a
big gaggle with no audible clipping.

### The costs were flash, not arithmetic

Three rounds of measurement, and the pattern repeated:

- **`mul_q16` widens to `int64_t`** → an `__aeabi_lmul` call, one per voice per
  sample. This is the *same* mistake fixed in `droneRender` two rounds earlier,
  re-introduced in new code. The lesson had not become a habit.
- **`ResolveSample()` read the user header through XIP on every trigger** — magic,
  version, size, offset: four flash reads while core 1 contends for the same bus.
  Cached at init, and Note-ons fell 1492 → 1099.
- **`CVOutMillivolts` calls `MillivoltsToDAC`, which is flash-resident** in the
  vendored ComputerCard — twice per sample at 48 kHz, nearly always re-sending a
  value the DAC already had. Now guarded by a change check.

**Voices is simply linear**: 835/4 = 209 cycles per voice before the pool,
1591/8 = 199 after. The pool added voices, not overhead — that is what eight
concurrent interpolating PCM readers cost, and no micro-optimisation changes it.

### Where it stopped

Full Geese, the worst case on the card: **Total 2652 of 2604 — 1.8% over, one
overrun in ~1.4 million samples.** Down from 3071 and 251 overruns.

Stopping here deliberately. Chasing the last 48 cycles would be optimising a
number rather than a sound, and the planned 192 MHz bump makes 2652 into 66% of
budget. The remaining item is `Voices`, which means fewer voices — a musical
trade, not a bug.

---

## The remaining noise is the recordings, not the format

Reported as the samples still sounding "a bit noisy (8 bit, or source, who
knows)". Worth settling, because it decides whether there is anything left to
fix.

8-bit's theoretical floor is **49.9 dB**. Measured peak-to-tail on the baked
library:

| mode | peak | tail RMS | peak-to-tail |
|------|-----:|---------:|-------------:|
| Frogs | 94 | 8.04 | **21.4 dB** |
| Cicadas | 75 | 1.83 | 32.2 dB |
| Meteors | 97 | 2.17 | 33.0 dB |
| Geese | 75 | 1.40 | 34.6 dB |

All far above the format's floor, so **the noise is in the source recordings** —
room tone and mic hiss in the Pixabay originals. More bits would faithfully
reproduce more noise. (Frogs' 21.4 dB is partly a false alarm: segmenting the
file shows real call structure through to the last eighth, so what looked like a
noisy tail is mostly the ribbit still going.)

The +0.8 bits recovered by the louder re-bake were real and worth having; there
is simply nothing further to win in the format. Reverb is the practical answer,
and it belongs outside the card.

---

## v1.2.0 — the alt boot stops being a different synth

Drone was proposed twice and rejected twice. The plan going in was a bank of four
oscillators whose pitch and level followed each agent's engine state. What came
back was a much better brief:

> *I think I just want them as they are in the rhythm side, but without the pitch
> modulation from the actual rr samples. I can see an instance where I might want
> to upload (say) notes, and have them 'gallop' like hooves. Or drip like rain.*

That is a smaller change and a stronger idea. The engines are already good at
generating organic rhythm; the thing stopping them being *usable* on pitched
material was never the voices, it was two deliberate detunings:

- **`kPcmAgentSpread`** — a fixed per-agent rate offset. On animals that is four
  different-sized bodies. On a tuned sample it is four permanently out-of-tune
  copies.
- **`kPcmPerHit`** — random detune per event. On honks it stops two overlapping
  calls fusing into one doubled sound. On a struck note it is just wobble.

Measured worst case: Meteors **±2.9 semitones**, Frogs ±2.2, Geese ±1.6 — audibly
out of tune, and randomly so from hit to hit. Both are gated off by a `tuned_`
flag, and the alt boot becomes the same instrument playing honestly.

So the boot switch now means something coherent: **Rhythm is an ecosystem, Tuned
is an instrument.**

### The routing, and the one output worth patching first

Specified directly rather than designed by me:

| | Switch Up | Switch Middle |
|---|---|---|
| **Pulse 1** | Agent 1 | Any agent fired |
| **Pulse 2** | Agent 2 | Pulse 1 ÷ 4 |
| **CV 1** | Agent 1 density | Overall density |
| **CV 2** | Agent 2 density | **1 V/oct — which sample fired** |

CV 2 is the interesting one: each round-robin slot maps to a semitone, so an
external oscillator plays a melody chosen by whichever recording the ecosystem
picked. It needed a new path — the CV outs are slewed, and **a pitch CV must step,
not glide**, or a sequence becomes portamento smear. One bit in `gXC.cvStep`
bypasses the smoother for that output only.

Verified the mapping arithmetic against the DAC calibration rather than trusting
it: within 0.1 mV of a true semitone across all eight slots.

### What deleting Drone bought

The granular renderer went: `droneUpdate`, `droneRender`, `Grain`, `DroneVoice`
and `droneControlTick()` — **308 lines**, and with them the last engine still
running its physics inline on core 0. The mode that was 130% over budget stopped
existing rather than being optimised.

---

## The editor round trip: three bugs that all looked like display faults

Uploading worked. Reading back what had been uploaded did not, and it took four
rounds of hardware reports to see why, because every symptom pointed at the page
while two of the three causes were elsewhere.

**Cicadas played hooves.** Samples uploaded to Horses and Rain also came out of
Cicadas, which had never been touched. My first instinct was a display shift —
the panel said "Cicadas 3 = built-in Horses 3" and Cicadas is mode 5, Horses 0.
Wrong: *"No — they sound like horses too."* The card really was holding it.

A **baked reference** points a slot at a built-in recording. In a mode with no
uploads it buys nothing, since an empty slot already plays its own built-in — so
the editor skipped sending them. But the test only caught a reference pointing at
its **own** mode and variant, and a cross-mode one went straight through. Then it
became self-sustaining: the page seeds its mapping from the card, so the bad
reference came back on every reload and was re-sent by the next sync. **It
outlived the firmware that created it**, which is exactly why it kept looking
like a display bug I had already fixed.

Fixed on both sides, and enforced on the card at `UP_END` rather than per message
— only the finished header shows whether a mode has real audio, since references
can arrive before the uploads they sit beside. That makes the next sync *repair* a
card already carrying them.

**A sample in a high slot vanished but still played.** Slots 1–4 and 5–8 come
back as two separate replies, because all eight never fitted the USB transmit
buffer. The request loop read one reply per request and skipped anything that was
not a `SLOTDET` — so a stray message was consumed **in place of** a real reply,
shifting every later reply by one and leaving the last request with nothing.

The tell was mine to miss and someone else's to spot: *"Might not have been 5.
Have just re-tried with slot 2 in geese, that DID work."* Slot 2 is the first
reply, the high slot the second.

Worth recording that my previous fix caused this. Keying replies on the mode the
*card* reports (rather than the one requested) stopped data being **mis-filed** —
but it could not conjure back a reply that was never read. **It converted a wrong
answer into a missing one.** I fixed the labelling without checking that the loop
consumed replies correctly, and the symptom moved instead of going away.

And once Geese's second reply was lost, Geese looked upload-free, so every slot
fell back to its built-in. **The page was faithful to what it had been told; it
had been told wrong.** A display that derives everything from one query is only
ever as honest as that query.

**"Update mapping" was greyed out until you had done an upload** — which is
precisely the case where you have not. `connect()` enabled every other button but
that one; the sole place it was enabled was the `finally` of `sync()`. The button
whose whole purpose is to avoid an upload required one first.

---

## Playing it: Frogs, the cicada footstep, and an input that never worked

Four things came back from an hour with the card, and three were real defects.

**Frogs felt uniform across Knob Main** — with population high and Main hard
CCW the pond should lock into phase, and the knob felt much the same
everywhere. Also remembered as having coupled properly once, which made it a
regression to find rather than a design to invent.

The model was fine. I checked the mean-field factorisation first, since it was
the obvious suspect: identical to the direct O(n²) sum to three decimal places.
Clock entrainment on Pulse In 2 also worked on hardware, and that needs the same
coupling term.

The knob-to-K mapping was the fault. Simulating the order parameter against K
shows the pond is locked above K≈0.25 and scattered below K≈0.03 — **the entire
audible transition is less than one octave of K.** Against that, the cube spent
half the travel above K=0.125, all of it locked.

The comment claimed cubing "pushes the transition into the middle of the sweep".
It does the opposite: it compresses exactly the low-K region where all the
behaviour lives. Reasoned backwards, and never checked against a sweep. Replaced
with a geometric table so equal knob travel gives equal *ratios* of K.

**The cicada footstep was a dip, not a hush.** It slammed `fatigue_` to maximum,
which sounds like silence and is not — `tired` bottoms out at 0.25, so twelve
insects kept calling at a quarter rate. That floor is right for fatigue, which
has to *thin* the field during a swell rather than switch it off. A startle was
simply never the same quantity, so it became its own state that multiplies to a
true zero.

**Audio In 1 did nothing at all, and never had in any firmware.** `listen()`
computes the envelope on core 0; the engines read `gXC.loudness`. One read, zero
writes anywhere in the tree. A casualty of Stage 3, where the physics moved to
core 1 and `listen()` stayed behind — `crosscore.h` even names core 0 as the
field's writer, and the writer was never written. Audio In 2 survived the same
split only because it publishes an edge counter inline, which is exactly why
Disturb worked and Loudness did not.

### Two wrong tunings in a row, on a control that was a cliff

Publishing the envelope exposed constants that had never once run against a
non-zero input. Geese was fine. Cicadas took three attempts, and the first two
were mine to have caught:

| attempt | loudness → fatigue | result |
|---|---|---|
| original | `/40` | pins fatigue in 0.25 s — would have **muted** the field |
| second | `/4000` | an order of magnitude below ambient drive — **inaudible** |
| third | direct rate scale | proportional, and works |

The lesson is not "pick a better constant". **No constant would have worked**,
because fatigue is driven far harder by calling (0.25 per call) and ambient
drive (~0.0017/tick), *and* it caps at ×0.25. Every value is either swamped or
pinned. Scaling the call rate directly — beside the startle, where it is
proportional and unbounded below — is the mechanism the feature always needed.

I jumped from one end of that range to the other and called the second value a
fix without checking it against the terms it was competing against. The
arithmetic that showed the problem was three lines long, and I only ran it after
hardware reported the effect was inaudible. **When a value has never run, the
first thing to check is not the value but whether the path exists** — and the
second is what else is writing the same variable.

---

## Standing notes

- **`tools/simulate.py` duplicates the C++ constants.** It will drift if
  `engines.cpp` changes without it. Update it or delete it — a model that lies is
  worse than none.
- **No host C++ compiler on this machine**, which is why the harness is a Python
  model rather than `tools/simulate.cpp` compiling the real sources.
- **CPU timing is measured, and the card now runs inside its budget** — worst mode
  70% with zero overruns, after the physics moved to core 1 and the clock went to
  192 MHz. The old claim here ("~55 cycles/sample amortised") measured the wrong
  quantity and has been removed. **Never quote an amortised figure as headroom:**
  `controlTick()` runs inline in the audio interrupt, so the sample where the
  physics fire must still complete in one 20.83 µs slot.
- **Drone is gone.** It was the worst thing on the card at 674% of budget, and
  rather than optimise it the alt boot became Tuned — the same voices, undetuned.
  308 lines deleted.
- **Both audio inputs are now confirmed on hardware.** Disturb spooks every
  ecosystem; Loudness agitates the geese and thins the cicadas. Audio In 1 had
  never worked in any released firmware — see above.
- **A constant that has never run is not a tuning, it is a guess.** Anything
  gated behind a value that was always zero has never been heard, however
  carefully it was reasoned about when written.
- **Measure the mode you are asking about.** The six-mode sweep was Rhythm-only
  and said nothing about Drone, where the real defect was — and the tell came
  from someone listening, not from the numbers.
- **The USB path partly runs on hardware.** Enumeration, SysEx round-trip and the
  browser's `MSG_PROF_GET` query are proven — that is how the timing above was
  read. **Sample upload now works on hardware** — transfers complete, commit, and
  the samples survive the reboot. It took six attempts; the constraint that
  finally explained it is that TinyUSB lives in flash, so USB and flash writes
  cannot coexist. Uploads are RAM-staged and capped at 160KB per pass, and they
  APPEND, so the full region is reachable over several passes.
- **When a fix fails twice, make the third attempt a measurement.** The uploader
  took six tries; the one that solved it added a stage counter on the LEDs rather
  than changing any logic. One reading (stage 5) proved the flash write was
  already working and USB was the casualty, which no amount of re-reading the
  code had suggested.
- **The card can only report what is still alive.** USB cannot describe a failure
  that stops USB, and the LEDs cannot describe one that stops `ProcessSample()`.
  Pick the out-of-band channel that survives the thing being diagnosed.
- **A wrong display and a missing one are different bugs.** Twice now a fix to
  how data was *labelled* left a hole in how it was *read*, and the symptom moved
  rather than going away. When a report changes shape after a fix, suspect the
  fix.
- **State that round-trips through storage outlives the code that wrote it.** A
  bad baked reference survived the firmware that created it, because the editor
  seeds itself from the card and re-sends what it finds. Anything persisted needs
  validating on the way *in*, not only on the way out — which is why that check
  now runs on the card at commit, where a buggy client cannot skip it.
- **A display derived from one query is only as honest as that query.** Losing
  half of one reply made a mode look upload-free, and the page then confidently
  showed built-ins for slots the card was playing samples from. It was not
  guessing; it had been told wrong.
- **Listening beats simulation, and simulation beats reading the code.** Every
  fix in this log after the first release came from someone playing the card, and
  more than one of my confident first guesses was wrong until I measured
  something instead of reasoning about it. This round the decisive corrections
  were all one-line hardware reports — *"they sound like horses too"*, *"slot 2
  did work"* — each of which killed a theory I had spent a tool call defending.
