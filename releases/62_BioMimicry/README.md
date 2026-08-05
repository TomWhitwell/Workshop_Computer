# BioMimicry — Organic Rhythms for the Workshop Computer

**A generative stochastic rhythm card born under the dark skies of rural Wales.**

BioMimicry uses **six distinct mathematical physics engines** to generate triggers, CV and
audio that feel organically alive.

It was built to capture the semi-random behaviours of the natural world: the polyrhythmic clopping of horses on a road, the cascading panic of a flock of geese, the rushing
accumulator of a waterfall walk, and the sudden silent clustering of a meteor shower overhead.  (I didn't hear frogs, but the swamp mathematics were too good to leave out.)


---

## The Ecosystems

Tap the momentary switch (Down) to cycle habitats. Each has its own internal logic.

| Mode | Model | Behaviour |
|---|---|---|
| **Horses** | Equine gaits | A **herd**: each horse has its own stride clock driving four hooves at their true footfall offsets, and the animals slide in and out of step the way real horses travelling together never quite match pace. Walk, trot, canter and gallop are the actual biomechanical patterns — including the suspension phase where all four feet leave the ground. |
| **Geese** | Stochastic contagion | A cascading probability network across a **flock of twelve**. One bird honking raises the odds for all the others, creating tight reactive clusters that erupt and fade. |
| **Frogs** | Coupled oscillators | The Kuramoto model. Voices pull on each other's timing, fighting between perfect metronomic synchronisation and chaotic swarms — and they'll entrain to an external clock if you give them one. |
| **Rain** | Leaky integrate-and-fire | Buckets fill with noise and constantly leak. An overflow **splashes downstream** into the next bucket, so drips pull each other along into rushing clusters, then fall apart. |
| **Meteors** | Inhomogeneous Poisson | An invisible slow-moving weather system dictates the density of a **swarm of twelve**. Long eerie silences swell smoothly into heavy overlapping barrages. |
| **Cicadas** | Amplitude feedback | Twelve insects in **four patches**, each hearing mostly its own neighbours. They call faster the louder their patch already is, and tire from being in it. Patches swell out of step with each other, so the field surges and subsides irregularly. Where Frogs couple on *phase* and Geese on *events*, Cicadas couple on *loudness*. |

### The gaits are based on real horses

Horses mode is clocked with each hoof landing at its correct point in the stride:

| Gait | Footfall (fraction of stride) | Suspension |
|---|---|---|
| **Walk** | LH 0.00 → LF 0.25 → RH 0.50 → RF 0.75 | — (4-beat lateral, same-side legs consecutive) |
| **Trot** | [LH+RF] 0.00 → [LF+RH] 0.50 | 2-beat diagonal |
| **Canter** | LH 0.00 → [LF+RH] 0.22 → RF 0.44 | 56% float |
| **Gallop** | LH 0.00 → RH 0.10 → LF 0.21 → RF 0.31 | 69% float (rotary — both hinds, then both fores) |

Because each horse's four hooves share one stride clock, its gait *holds together*
however long it runs; Knob Y jitters each hoof's timing without breaking the pattern.
Knob X adds **whole horses**, never partial ones; a three-legged horse is not a
smaller herd. Each animal runs slightly off its neighbours' pace, so the herd phases
continuously. `python tools/simulate.py gaits` verifies the footfalls biomechanically.

## Panel

| Control | Function |
|---------|----------|
| **Knob Main** | **The Physics** — the fundamental law of the current ecosystem (Gait & tempo · Contagion · Decoupling · Downpour · Debris density) |
| **Knob X** | **Population** — how many agents are alive: **horses in the herd**, birds in the flock, frogs in the pond, buckets on the leaf, meteors in the sky, insects in the field |
| **Knob Y** | **Chaos / Humanize** — per-mode randomness and spread (timing jitter, spark rate, frequency spread, threshold variance, LFO wander) |
| **Switch Down** (momentary) | **Tap to cycle** the ecosystem. **Hold** for two seconds to hand the card to USB, hold again to return to playing. **Hold at power-on** to boot **Tuned** mode instead of Rhythm. |
| **Switch Up** | Routing: **Discrete** |
| **Switch Middle** | Routing: **Summed / CV** |

### Per-mode meaning of Knob Main

| Mode | Knob Main |
|---|---|
| **Horses** | **Gait and stride rate.** `0.00–0.25` Walk · `0.25–0.50` Trot · `0.50–0.75` Canter · `0.75–1.00` Gallop. Each gait sweeps its own stride-rate band, so a gallop is genuinely faster than a walk rather than the same pattern sped up. |
| **Geese** | **Contagion** — 0.0 birds ignore each other; 1.0 one honk sets off a panicked chain reaction |
| **Frogs** | **Decoupling** — 0.0 is maximum coupling (locked metronomic sync); 1.0 is zero coupling (total chaos) |
| **Rain** | **Downpour** — 0.0 leak exceeds input (silence); 1.0 rapid stuttering torrents |
| **Meteors** | **Debris density** — 0.0 rare isolated hits; 0.5 long silences swelling into dense waves; 1.0 constant barrage |
| **Cicadas** | **Coupling depth** — CCW: independent insects, a steady even drone. CW: the field drives itself into deep surges that collapse into near-silence and swell back. Intensity stays up as you turn clockwise; what changes is how strongly the field pulses. |

### Per-mode meaning of Knob Y (Chaos)

| Mode | Knob Y |
|---|---|
| **Horses** | Per-hoof timing jitter — an uneven, real animal rather than a machine |
| **Geese** | Spontaneous spark rate — how readily a bird honks unprompted |
| **Frogs** | Natural-frequency spread — how hard sync is to reach |
| **Rain** | **Leak rate** — slow leak lets buckets accumulate into heavy irregular drips; fast leak keeps only the strongest bursts |
| **Meteors** | Density wander on top of the hidden weather system |
| **Cicadas** | Rate spread across the field, so it never sounds like one insect multiplied |

## Inputs

| Jack | Function |
|------|----------|
| **Pulse In 1** | **The Spook** — a hardware interrupt that disrupts the environment. Horses: the herd startles into step, every horse landing together before drifting apart again. Geese: spook the flock into a guaranteed cascade. Frogs: splash — scramble every phase, destroying sync. Rain: wind gust — dump energy into every bucket. Meteors: bolide — spike density to maximum. **Cicadas: a footstep in the grass — the whole field falls silent at once**, then creeps back in. |
| **Pulse In 2** | **The Clock** — an external tempo the ecosystem *entrains to* rather than obeys. Frogs treat it as a phantom frog in the pond and couple to it with whatever strength Knob Main is set to, so you can dial anywhere from locked-to-the-clock to completely indifferent. Horses lock their stride to it. Geese lean their honks toward the beat; Rain tops up every bucket so the nearest tips on the beat; Meteors swell the debris field. Stop the clock and the ecosystem drifts back to its own timing within ~3 seconds. |
| **CV In 1** | Modulates **Knob Main** (the physics variable) |
| **CV In 2** | Modulates **Knob X** (population) |
| **Audio In 1** | **Loudness** — the ecosystem hears the rest of your patch. A loud room agitates the geese into honking and shuts the cicadas up; the shy modes thin out as the patch gets busy and fill back in when it quietens. |
| **Audio In 2** | **Disturbance** — transient-sensitive rather than level-sensitive, because it is sudden movement that alarms an animal, not steady noise. A sharp attack counts as a Spook. |

Both audio inputs only act when something is patched in.

## Outputs

Routing is chosen with the toggle. The Computer has two pulse outs, so in Discrete mode
agents 3 and 4 fire as **calibrated 5 V blips on the CV outs** — every agent gets a
physical trigger.

**Switch Up — Discrete**

| Jack | Function |
|------|----------|
| **Pulse Out 1 / 2** | Agent 1 / Agent 2 triggers (5 ms gates) |
| **CV Out 1 / 2** | Agent 3 / Agent 4 triggers, as 5 V blips |

**Switch Middle — Summed / CV**

| Jack | Function |
|------|----------|
| **Pulse Out 1** | All active agents logically OR'd |
| **Pulse Out 2** | **Accent** — fires when two or more agents hit at once |
| **CV Out 1** | Continuous internal state of agent 1 (phase ramp, bucket level, excitation…) as 0–5 V |
| **CV Out 2** | Global ecosystem state — debris density, flock agitation, chorus coherence |

**Audio Out 1 / 2** — all agents rendered and placed in the stereo field (see below).

**LEDs** — one LED per ecosystem. The active mode's LED sits at a dim "you are here"
glow and flares to full on every trigger, so one light carries both meanings.

Both tables above describe the **Rhythm** boot. Tuned routes differently — see
[Two Modes](#two-modes-rhythm-and-tuned).


### Round robins

Every trigger picks a variant, and the variant *means* something:

| Mode | Variants | What a variant is |
|---|---|---|
| **Horses** | 4 | **One per hoof.** The engine reports which hoof landed and the voice plays *that hoof* — hinds lower and heavier than fores. Every horse in the herd plays all four of its own. This is most of what stops a gait sounding like a drum machine. |
| **Geese** | 8 | Birds of different size |
| **Frogs** | 8 | Species in the chorus |
| **Rain** | 8 | Drip sizes |
| **Meteors** | 5 | Distances — the baked library ships five swooshes, not eight |
| **Cicadas** | 8 | Insects, tightly spread — a real field is fairly uniform |

Everything except Horses picks at random with a **no-immediate-repeat** rule, so you never
hear the same honk or drip twice running. On top of that each agent has a fixed playback
rate — a body size, so agent 1 is always the largest animal — and the crowd modes jitter
slightly per event, which stops two overlapping calls fusing into one doubled sound.
Horses deliberately does *not* jitter: a horse is one animal, and a clop that changes
pitch hit to hit stops sounding like a horse.

### Stereo placement

Panning comes from the ecosystem, not from a knob:

- **Fixed** (Horses) — each horse holds its own place in the field, its four hooves
  sitting just either side of that spot (near side / off side). The animals stay put;
  you hear a herd spread in front of you, not four wandering sounds.
- **Spread** (Geese, Frogs, Cicadas) — every swarm member has its own place, so twelve
  birds occupy twelve positions and a cascade sweeps across the field.
- **Random** (Rain, Meteors) — each hit lands somewhere new, because each is a new object.

## Two Modes: Rhythm and Tuned

Hold the momentary switch **Down at power-on** to boot **TUNED** instead of the normal
**RHYTHM** card. On power-up the LEDs announce which you got: Rhythm lights the left
column, Tuned the right.

Both play the same engines, the same recordings and the same one-shot voices. The
difference is **pitch**.

**Rhythm** humanises every hit. Each agent carries a fixed rate offset — four
different-sized bodies — and every trigger adds a random detune on top. That is what
stops four hooves sounding like one sample fired four times, and what stops two
overlapping honks fusing into a single doubled sound.

**Tuned** switches both off. Samples play at their recorded pitch, every time. On
animal recordings that is a subtle tightening; on *pitched* material it is the whole
point — the detuning that flatters a goose spreads a struck note across ±3 semitones,
randomly, hit to hit. Upload notes, drips or hits and the ecosystems become rhythm
generators for them: a gallop of plucks, rain made of woodblocks, a Kuramoto chorus
that stays in tune.

**Tuned's outputs** are the same triggers, with the CV outs describing the ecosystem
rather than firing blips:

| | Switch Up | Switch Middle |
|---|---|---|
| **Pulse Out 1** | Agent 1 | All agents |
| **Pulse Out 2** | Agent 2 | Pulse 1 **÷ 4** |
| **CV Out 1** | Agent 1 density | Overall density |
| **CV Out 2** | Agent 2 density | **1 V/oct — which sample fired** |

That last one is the useful one. Each round-robin slot maps to a semitone (slot 1 = 0 V,
slot 2 = 1/12 V, and so on), stepped rather than slewed, so patching CV 2 to an
oscillator's pitch input gives you a melody whose notes follow whichever recording the
ecosystem chose. Pair it with Pulse Out 1 as the gate.

The CV outs are always continuous in Tuned — it never fires CV trigger blips.

## Replacing samples without a rebuild

Open [`web/index.html`](web/index.html) in Chrome or Edge, plug the card in over USB and
click **Connect**. Drag WAVs onto the mode slots and press **Upload** — the browser
converts them (any rate, mono or stereo), matches loudness across everything you load,
and streams them into a 1&nbsp;MB region of the card's flash over WebMIDI SysEx.

Uploaded samples **override the baked ones per slot**, so you can replace just the geese
and keep everything else. **Revert to built-in** forgets them again. A full library
takes a few seconds.

> The card is **silent throughout**. Holding the switch stops the ecosystem and hands
> the card to USB; it **reboots** back into playing when you are done. Writing flash
> halts the RP2040 — and takes USB down with it, since the USB stack itself lives in
> flash — so the whole transfer is buffered in RAM and committed in one go at the end.
> That caps a single upload at **160 KB**, about 3.4 seconds of audio, but uploads
> **append**, so the full 1 MB region is reachable over successive passes. Uploading is
> a setup activity, not a performance one.

Flash layout: firmware and baked samples occupy the first 1&nbsp;MB, user samples the
second. The build fails loudly if the firmware ever grows into the user region, because
that would make flashing destroy uploads and uploads destroy the firmware.


## Advanced Use

## PCM samples (baked at build time)

Sample playback is a **build-time** choice, not a boot mode: bake recordings into
`samples/` and they replace the synthesized voices in Rhythm boot. With no `samples/`
directory the firmware still builds and uses synthesis.

Each mode takes up to **eight round-robin recordings**, and the variants are not decoration.

Drop WAV files into `samples/incoming/` (or any folder) and run the importer:

```sh
python tools/importwav.py samples/incoming
```

The converted `samples/*.raw` are committed here, so this builds as-is — you only need
the importer to replace the recordings. (The original source WAVs live in the
[upstream repo](https://github.com/uglifruit/WorkshopBio); they are not duplicated here.)

It accepts **any sample rate, mono or stereo, 8/16/24/32-bit or float**, and converts to
the 8-bit signed mono 48 kHz `.raw` the build bakes in — resampling, summing to mono and
trimming silence. Standard library only, no ffmpeg needed.

**It also matches loudness across the whole library.** Sample packs are typically all
over the place; the importer measures every source and scales each to a common RMS, then
soft-limits the result. RMS rather than peak, because a sample's peak is usually a single
transient — two recordings peak-normalised to the same ceiling can still sound nothing
alike. A real pack needed corrections from **-15 dB to +21 dB**, and came out matched to
1.07x with no clipping.

Name them `mode_variant.wav`, or use common animal names (`HORSE_1.wav`, `GOOSE_3.wav`,
`WHOOSH_2.wav`, `DRIP_5.wav`, `CICADA_8.wav`) — the importer maps those onto modes:

```
horses_1..4    (four: the hooves)
geese_1..8   frogs_1..8   rain_1..8   meteors_1..8   cicadas_1..8
```

**`horses_1..4` are LH, LF, RH, RF** — left hind, left fore, right hind, right fore. The
firmware asks for the hoof that actually landed, and hind hooves strike lower and heavier
than fores on a real animal, so putting them in the right slots is most of what makes a
gait sound like an animal. For the other modes the four are simply different individuals,
picked at random with no immediate repeat.

Fewer is fine: missing variants reuse whichever you supplied — and the baker points the
repeats at one copy in flash rather than storing it twice. A bare `horses.wav` with no
number covers every slot.

**What makes a good source:** a single isolated hit, trimmed tight to the transient (the
attack is what identifies the sound), and **dry** — the card has no reverb, so any
recorded ambience is baked in forever.

The library that ships with the card, for scale:

| Mode | Variants | Average length | Flash |
|---|---|---|---|
| Horses | 4 (the hooves) | ~147 ms | 27 KB |
| Geese | 8 | ~179 ms | 67 KB |
| Frogs | 8 | ~770 ms | 289 KB |
| Rain | 8 | ~104 ms | 39 KB |
| Meteors | 5 | ~1461 ms | 342 KB |
| Cicadas | 8 | ~148 ms | 55 KB |

That is **822 KB** in total. Firmware and baked samples share the first 1 MB of flash and
currently end ~95 KB short of the boundary; the build fails with an explanation if they
ever reach it, because past that point flashing would destroy uploaded samples and an
upload would destroy the firmware. The second 1 MB is the user region — about 21 seconds
of audio — which the web uploader writes to.

`python tools/gensamples.py` writes procedural placeholders in the same layout, so the
whole path works before you have a single recording.

---

## Voices

**The fallback — synthesized.** Recordings are the normal case; this is what plays if a
card is built with no PCM baked in and nothing uploaded. Each mode has its own DSP
timbre, built around whatever detail actually identifies the sound: hooves get a pitch-dropping body *plus a sharp band-passed noise transient* for shoe-on-stone; honks are a saw whose filter opens at the
attack for that nasal kink; ribbits are Karplus-Strong; **drips rise in pitch as they
decay**, which is the acoustic signature of a bubble collapsing in liquid and the reason a
drip sounds like a drip; meteors are noise swept by a closing filter; cicadas are a high
tone ring-modulated by a wing-beat buzz.


## Under the hood

`ProcessSample()` runs at 48 kHz, but the physics don't need audio rate: engines tick at
**1.5 kHz** (every 32nd sample), which cuts average CPU load 32× and still gives 0.67 ms
timing resolution — far finer than the ear resolves for triggers. Voice rendering, gate
timing and CV output stay at the full 48 kHz.

That divider buys **throughput, not slack**. `controlTick()` is called inline from
`ProcessSample()`, which runs inside the DMA interrupt, so on the sample where the physics
fire the whole engine must still finish inside that one **20.83 µs** slot. The worst
single sample is what decides whether audio glitches, not the average.

To measure it, build with the profiler on:

```sh
cmake -B build-profile -G Ninja -DBIO_PROFILE=ON
cmake --build build-profile
```

That build times the whole callback and each phase with the Cortex-M0+ SysTick counter
and shows the worst case on the LEDs — one LED per ~16% of the 4000-cycle budget, all six
flashing if any sample ever overran. A Down tap clears the peaks so each ecosystem can be
measured separately, and Pulse Out 2 mirrors the callback duration for a scope. It
compiles to nothing when off: the normal build is byte-identical either way.

Everything is **integer fixed-point** — Q16 for levels and probabilities, `uint32_t`
phase accumulators that wrap for free, a 257-entry quarter-wave sine LUT, and xorshift32
for randomness. There is no `float` in the hot path and no libm; the RP2040 has no FPU.

| File | Purpose |
|---|---|
| [fastmath.h](fastmath.h) / [fastmath.cpp](fastmath.cpp) | Sine LUT, PRNG, fixed-point helpers |
| [biomimicry.h](biomimicry.h) | Shared types, `Engine` interface, control-rate constants |
| [engines.cpp](engines.cpp) | The six physics models |
| [voices.cpp](voices.cpp) | Synth + PCM voice rendering, panning |
| [main.cpp](main.cpp) | I/O, mode/routing UI, LEDs, boot dispatch |

### Verifying the physics

`tools/simulate.py` models the engine math in Python and reports gait correctness, the
Kuramoto sync curve, and triggers/second per agent across the knob range — used to
confirm every mode sweeps a musically useful range before flashing hardware:

```sh
python tools/simulate.py          # everything
python tools/simulate.py gaits    # just the biomechanical gait check
```

It has caught real defects that compiled perfectly cleanly: Kuramoto coupling too weak
to ever synchronise, contagion saturating into a flat buzz, a gallop table byte-identical
to the walk, and modes topping out at rates that read as hiss rather than rhythm.

(`tools/simulate.cpp` compiles the *real* engine sources natively if you have a host C++
compiler; the Python model is the fallback.)

## Building

Raspberry Pi Pico SDK 2.2.0, Arm GCC 14.2, Ninja:

```sh
cmake -B build -G Ninja
cmake --build build
```

Produces `build/biomimicry.uf2`. Hold **BOOTSEL** while plugging in USB and drop it on
the mounted drive. On Windows, `cmake`/`ninja` live in `~/.pico-sdk/` and are not on the
default PATH — see [CLAUDE.md](CLAUDE.md) for the exact invocation.

---
## Use

Runs on a Music Thing Modular Workshop System Computer. Built on the RP2040 with
[ComputerCard](https://github.com/TomWhitwell/Workshop_Computer).

Why the card is the way it is — including the things that were wrong first — is in
[docs/DEVLOG.md](docs/DEVLOG.md). Questions, bugs and patches:
[**the BioMimicry thread**](https://discord.com/channels/1210238368898879569/1533895615442849812)
on the Music Thing Discord.


---
## Credits

- **Music Thing Modular Workshop System Computer** — Tom Whitwell / Music Thing Modular.
  **ComputerCard** by **Chris Johnson** (MIT, header-only).
- **Raspberry Pi Pico SDK** / RP2040 — Raspberry Pi Ltd.
- The Kuramoto model — Yoshiki Kuramoto. Leaky integrate-and-fire and inhomogeneous
  Poisson processes are standard computational-neuroscience and point-process models.
- **Sample library** — animal and environment recordings from
  [**Pixabay**](https://pixabay.com), used under the Pixabay Content License.
- BioMimicry for the Workshop Computer — **Andy Jenkinson** (**uglifruit**), 2026, with
  **Claude Code** (Anthropic).

## Licence

**[CC-BY-4.0](LICENSE)** — Creative Commons Attribution 4.0 International.
Use it, fork it, sell it, put it in your own card; just credit
**Andy Jenkinson (uglifruit)**.

Two things in this repository are not mine to relicense and keep their own terms:

| | |
|---|---|
| [`samples/*.raw`](samples/) | Recordings from [Pixabay](https://pixabay.com), under the Pixabay Content License — see [samples/README.md](samples/README.md) |
| `ComputerCard.h` | The Music Thing card library by Chris Johnson, MIT |

---

## Release notes

**v1.2.0** — alt boot is now **TUNED**: the same one-shot voices as Rhythm, but
played at their recorded pitch with no per-animal or per-hit detuning, so
uploaded notes gallop or drip instead of being smeared across ±3 semitones. Its
Summed routing puts **1 V/oct on CV 2** telling you which round-robin slot just
fired. The granular renderer is gone.

The browser editor now maps a **pool** of uploaded files onto slots, so one
recording can be reused across ecosystems without being sent twice.

Both audio inputs now work on hardware — and Audio In 1 never had, in any
firmware: its envelope was computed on one core and read on the other, with
nothing writing it across. Patch a pad into **Loudness** and the cicadas
recede while the geese get agitated; patch a drum into **Disturb** and every
ecosystem flinches on the transient.

**Frogs synchronise again.** The knob mapped almost its whole travel to
coupling strengths that were already locked, so the chorus sounded the same
everywhere; the sync/chaos transition now sits in the middle of the sweep where
it can be played. And a footstep in the grass genuinely silences the cicadas
rather than thinning them.

**v1.1.0** — six ecosystems, two boot modes, a full library of real animal
recordings, and a browser app for swapping them over USB.

**The card now runs inside its timing budget.** The physics moved to the second
core, so the engine and the voices no longer land in the same 20.83 µs sample:
the worst mode went from **334% of budget to 70%, with zero overruns**. The
clock is 192 MHz. USB is modal — hold the switch to hand the card over, hold
again to go back to playing — so nothing of TinyUSB runs while you perform.

Audibly: samples less audibly truncate when hits overlap (eight voices, and a
steal is a crossfade rather than a cut), the library is re-baked ~1 bit louder
with dither, and the round robins finally produce as many distinct sounds as
they claim — panning no longer collapses eight variants to four, and Meteors
stops playing its first three swooshes twice as often as the rest.
