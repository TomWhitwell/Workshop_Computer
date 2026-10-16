# Constancy: Design Brief

Oct 8, 2026 · @Matt

## Overview

Constancy is a generative Workshop Computer card that plays one Cornish morning, forever: 17 April 2024 at Calamansac on the Helford River, the last day of Tom Whitwell's five-day Music Thing Modular residency there. It holds a single dawn in memory: the tide, the sun, the wind and the cloud. The music moves only when you move through that morning.

The voice is a Vangelis-style CS-80 supersaw, built on a fixed-point C++ port of schollz's Icarus engine for norns. The note and key system carries over from Equanimity. Low tide gives short, plucky strikes. High tide stretches everything into long, washed swells. The lead only appears once the sun clears the horizon.

It sits in the series beside Discrete, Uncertainty, Scintillator, Impermanence and Equanimity. Built with Chris Johnson's ComputerCard framework on the Pico SDK, same toolchain as the others.

## The day

One fixed date and place, baked into the firmware. The card has no clock, so it never pretends to know today.

**The window.** First light to two hours after sunrise, about four hours in total. This is where light changes fastest and every parameter gets its full range. Overnight and midday are too flat to be musical.

**The date and place.** 17 April 2024, Calamansac, Port Navas Creek, Helford River, Cornwall (about 50.10° N, 5.14° W). It's the final day of the five-day Music Thing Modular residency, part of the Workshop System's origin story. The card replays the morning the residency ended.

| Moment (BST) | Time | Sun |
| --- | --- | --- |
| Astronomical dawn, window opens | 04:17 | 18° below |
| Nautical dawn | 05:05 | 12° below |
| Civil dawn | 05:47 | 6° below |
| Sunrise | 06:22 | Horizon |
| Window closes | 08:22 | 18° above |

The window runs just over four hours, and the sun climbs symmetrically: 18° below to 18° above.

**Tides.** The first quarter moon fell on 15 April, so this is a neap morning, the gentlest tides of the fortnight. Normalising matters even more here. Where low tide falls in the window is still to be confirmed from the data pull.

**Data, prepared offline by a small build script:**

| Stream | Source | Resolution |
| --- | --- | --- |
| Sun elevation | Computed (SunCalc or NOAA solar equations) | 15 min |
| Tide height | [Open-Meteo Marine API](https://open-meteo.com/en/docs/marine-weather-api), sea level variable (from Jan 2022) | Hourly |
| Wind speed, cloud cover | Open-Meteo historical weather archive | Hourly |

**Normalising.** Each stream is scaled to the full 0 to 1 range across the window. Four hours covers only a third of a tide cycle, so without this the card would sound timid.

**Format.** A few hundred values, stored as a const table in flash. The card interpolates smoothly between points, so nothing steps.

**Credit.** Open-Meteo data requires attribution to DWD and a reference to Open-Meteo. It goes in the card's README.

## Voice engine

A fixed-point C++ port of [Icarus](https://github.com/schollz/icarus) by schollz (MIT licence, credit in the README), extended towards the CS-80. The RP2040 has no FPU, so everything runs in integer maths.

**What Icarus does, per voice.** One saw with a slowly wobbling pulse width, an optional sub pulse an octave down, and random pitch drift of up to a semitone scaled by a detune amount. A cubed ADSR shapes it. The magic is the shared stereo feedback loop: a short delay, two gentle one-pole filters, a stereo rotation, soft clipping and a Moog ladder, all inside the loop. Source: [Engine\_Icarus.sc](https://raw.githubusercontent.com/schollz/icarus/main/lib/Engine_Icarus.sc).

**What Constancy changes:**

- **Oscillators.** Three to five drifting saws per voice instead of one, for the supersaw spread. Keep Icarus's random pitch wander.
- **Filters, CS-80 style.** High-pass into low-pass, as on every CS-80 voice. Here they sit after the voices, on the shared Water control.
- **Ladder.** One shared ladder inside the feedback loop, not one per voice. Four one-pole stages with a lookup-table saturator.
- **Dry tap.** Icarus only outputs the loop. Constancy taps the voices before the loop so the delay can be mixed from dry to drenched.
- **Diffusion.** A few allpass stages in the loop stand in for the Blade Runner hall reverb, which is too heavy for the chip.
- **Destruction.** Icarus's random dropouts below a quarter-second delay. Keep them, tied to the short end of the sun's delay range.
- **Drone.** A separate, simpler voice that mostly bypasses the loop (see Composition).

**Signal flow:**

&#91;embedded content: Constancy signal flow · melody through the loop, drone around it\]

The dry tap and the drone both skip the loop, so the card can be clean and close or fully drenched. Everything meets at Water before the outputs.

## Composition

Four melodic voices plus a drone. One voice leads, three loop, as in Equanimity.

**Voices 2 to 4: the loops.** Each owns one note on its own random cycle, exactly as in Equanimity. Each loop owns its own voice, so a retrigger only restarts its own note. No stealing, no clipped tails. Together they form the pad.

**Voice 1: the lead.** Plays phrases, not loops.

- Motifs of three to six notes, mostly stepwise, ending on a long held note.
- Portamento on some notes, not all.
- Delayed vibrato that eases in as a note holds.
- A brightness swell after the attack, standing in for CS-80 aftertouch.
- An occasional slow pitch fall at a phrase end, like easing off the ribbon.
- Silence between phrases.

**The sun calls the lead.** Before sunrise, only the loops play. The lead appears as the sun crosses the horizon, and phrases come more often as it climbs. Scrub back to night and it falls quiet.

**The drone.** Root and fifth, an octave or two below the melody. Fewer drifting saws, so it sits behind. Through the Water filter, mostly outside the feedback loop. Tune the fifth pure (3:2), not equal tempered, so the drone stays still.

**Keys and scales.** Equanimity's system, unchanged, with two exceptions. Drop Locrian and whole tone if present, as neither has a perfect fifth. Weight the melody so notes a semitone above the drone (flat second, flat sixth) appear less often and rarely land on long swells.

**Reseed.** Switch down picks a new root and key. The old phrase fades out before the new one enters. The drone glides to the new root.

## Data mappings

The Time position on Main reads each stream from the table. Nothing moves unless Time moves, apart from the random CVs and the loops themselves.

| Data | Drives | Low end | High end |
| --- | --- | --- | --- |
| Sun elevation | Delay time, as Icarus draws it | Pre-dawn: short, fragile, destruction active | Morning: long, settled |
| Sun elevation | Lead density | Silent before sunrise | Frequent phrases |
| Sun elevation | Brightness bias on the low-pass | Muffled | Open |
| Tide height | Envelope length, all voices | Short, plucky strikes | Long attack and release swells |
| Tide rate of change | Feedback nudge | Turning tide: calm | Running tide: pushes towards the sun |
| Tide height, wind speed | CV out 1 | Centre follows tide; still air wanders slowly | Wind makes it choppy |
| Sun elevation, cloud cover | CV out 2 | Centre follows light (sun dimmed by cloud) | Broken cloud makes it flicker |

Data sets centres and biases. The knobs still have the final say: Sun and Water offset whatever the data is doing.

## Controls and I/O

Two knob layers on the switch, with reseed on switch down from either layer.

| Control | Switch middle: Play | Switch up: Levels |
| --- | --- | --- |
| Main | **Time.** First light to two hours after sunrise | **Delay mix.** Dry to fully drenched |
| X | **Sun.** Feedback and drive; fly close to burn | **Melody level** |
| Y | **Water.** Two-way filter: centre open, left low-pass drowns, right low cut thins | **Drone level** |
| Switch down | Reseed root and key, old phrase fades out | Same |

**Knob pickup.** Changing layers leaves knobs in the wrong place. Each knob does nothing until it passes through its stored value, then takes over. No jumps in feedback or level.

**Time is manual.** The morning moves only when Main moves, or when CV moves it. Parked, the card holds that moment.

**Jacks:**

| Jack | Use |
| --- | --- |
| Audio out 1, 2 | Stereo mix of voices, loop and drone |
| CV out 1 | Tide: smooth random, centred on tide height, wander set by wind |
| CV out 2 | Light: smooth random, centred on sun dimmed by cloud, wander set by cloud |
| CV in 1 | Proposed: adds to Time, so an LFO or random source can drift the morning |

## Resource budget and DSP notes

The engine fits, but delay memory and saw count are the two numbers to watch.

- **Delay memory.** Half a second of stereo at 48 kHz, 16-bit, is about 96 KB of the RP2040's 264 KB. Workable. If other buffers squeeze it, cap at a quarter second (48 KB) or run the loop at half rate, which adds a little grit.
- **Saws.** Four voices at up to five saws, plus a three-saw drone, is about 23 oscillators per sample. Use PolyBLEP saws in fixed point. Start at three saws per voice and raise it once timing is measured.
- **Cores.** Audio on one core in ComputerCard's per-sample callback. Sequencing, data interpolation, envelopes and the random CVs on the other, at control rate.
- **Saturation and tuning.** Lookup tables for the ladder's tanh and for pitch to phase increment.
- **Smoothing.** Every data value and knob runs through a one-pole lag, as Icarus lags its delay time and filter. Delay time changes bend pitch, so lag it gently on purpose.
- **Flash.** The data table is read-only and small. No flash writes at runtime, so no audio stalls.

## Open questions

- [x] Confirm Calamansac's exact coordinates; 50.10° N, 5.14° W is approximate.
- [x] Pull real tide, wind and cloud for 04:00 to 09:00 BST and see where low tide falls.
- [ ] Does Equanimity's scale set include Locrian or whole tone?
- [ ] Uses for the audio inputs, CV in 2 and the pulse jacks.
- [x] What the LEDs show: sun position across the window is the obvious candidate.
- [x] A sunrise moment: should crossing the horizon fire a one-off Icarus-style swoop on the delay?

## Build milestones

Each step should be playable before the next begins.

1. **Data script.** Pick the date, pull tide, wind and cloud, compute sun elevation, normalise, and emit a C header.
2. **Single voice.** One drifting multi-saw voice with envelope, driven from Main. Measure cycles.
3. **Feedback loop.** Port the Icarus loop: delay, one-poles, rotation, soft clip, ladder. Sun on X.
4. **Water.** Two-way HPF and LPF on Y.
5. **Loops.** Bring across Equanimity's three looping voices and key system.
6. **Lead.** Phrase generator, glide, delayed vibrato, brightness swell, sun-gated entry.
7. **Drone.** Root and pure fifth, reseed glide.
8. **Data mappings.** Wire tide, sun, wind and cloud to envelopes, delay, feedback and CV outs.
9. **Levels layer.** Switch up, knob pickup, dry tap and delay mix.
10. **Tune and polish.** Saw count, scale weighting, LEDs, README with Icarus and Open-Meteo credits.
