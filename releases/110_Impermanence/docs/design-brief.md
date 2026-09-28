# Impermanence — design brief and decisions

The card as it stands, followed by the decisions that got it there — each
one recording what changed from the original brief and why, starting from
the review against the Workshop Computer hardware and the AI coding
directive (v1.9). The sections above the log are kept in step with the
firmware; the log itself is history and is left as written.

## Concept

Impermanence is a Workshop Computer program card built around a short audio
take, no undo, and a single knob that treats randomness as the instrument
rather than a parameter to dial in.

It captures a fixed-length sample, and every movement of the main knob
triggers a fresh reslice / reorder / pitch-scatter of that take. The knob's
resting position biases how chaotic the result is, but nothing changes until
the knob actually turns. The same gesture that shapes the sound also destroys
whatever came before it.

Built with Chris Johnson's ComputerCard C++ framework (Pico SDK), matching the
toolchain used for Uncertainty.

## Signal path

Audio In 1/2 (stereo) → 0.75-second stereo take (recorded on Pulse In 1, or
by turning Main fully anticlockwise and back up) → reslice / reorder /
pitch-scatter engine, three layers (Main = chaos, re-randomises on movement)
→ shimmer reverb (+1 octave in the feedback path) → Audio Out 1/2.

## Controls

| Switch       | Main                        | X                          | Y                              |
|--------------|-----------------------------|----------------------------|--------------------------------|
| Up           | Level, L1                   | Level, L2                  | Level, L3                      |
| Middle       | Chaos (CV1 adds, Texture)   | Reverb decay + tank sweep  | Reverb wet/dry (CV2 adds)      |
| Down         | Same as Middle — the position is momentary; a tap toggles Texture/Rhythm | | |

Main fully anticlockwise (Middle/Down) clears the take; turning it back up
records a new one.

Pot catch-up between Up and Middle/Down: each parameter holds its last value
until the physical knob passes back through it. The reverb keeps sounding in
every position.

## I/O

| Jack         | Role                                                                     |
|--------------|--------------------------------------------------------------------------|
| Audio In 1/2 | Stereo record source (In 1 alone records to both sides)                  |
| Pulse In 1   | Start a take; a second pulse ends it early. Edges are confirmed, so unplugging doesn't disturb the loop |
| Pulse In 2   | Clock: sets slot timing, not pitch. Beats play the next slot in Rhythm   |
| CV In 1      | Chaos modulation (Texture only)                                          |
| CV In 2      | Reverb wet/dry modulation, every switch position                         |
| Audio Out 1/2| Post-reverb stereo out                                                   |
| Pulse Out 1  | End-of-cycle pulse, L1                                                   |
| Pulse Out 2  | End-of-cycle pulse, L3                                                   |
| CV Out 1     | Smooth wandering random voltage (patch to CV2 In for auto-fades)         |
| CV Out 2     | Envelope follower on the loop (pre-reverb)                               |

## Build milestones (the original plan)

1. Buffer + transport, SRAM budget confirmed with the target reverb size.
2. Playback core: raw loop out, clean.
3. Toggle switch + pot catch-up, no zipper noise on switch changes.
4. Reslice/chaos engine, re-randomise on movement past a dead-zone, CV1 in Middle only.
5. Texture/Rhythm behaviour, Pulse In 2 clocking.
6. Levels + end-of-cycle pulse outs.
7. Shimmer reverb.
8. CV outs.
9. Full patch test — confirm there is no way to recall a previous loop.

## Decisions (review, 2026-09-22)

1. **Rhythm mode is a tap, not a position.** The Z switch's Down position is
   momentary (spring-loaded), so it cannot hold a mode. Tapping Down toggles
   Texture ↔ Rhythm; the mode latches after the switch springs back to
   Middle, and LED 1 shows it. Middle and Down share knob assignments, so
   catch-up only happens between Up and Middle. CV1 follows the *mode*
   (live in Texture, ignored in Rhythm) rather than the physical position.
2. **12-bit packed buffer.** Measured with a probe build (copy_to_ram,
   144MHz, 32KB reverb + 8KB shifter placeholder): a 16-bit 1s stereo buffer
   left ~13KB of RAM for all remaining code; 12-bit packing (2 samples in 3
   bytes = 144,000 bytes) left ~61KB. The audio inputs are 12-bit, so
   packing is lossless — full 1 second, full resolution. (Superseded by 18:
   0.75 seconds of plain 16-bit audio, in the same 144KB.)
3. **Recording is one-shot.** A pulse at Pulse In 1 starts a take (1 second
   then, 0.75 since 18) that stops itself; a second pulse during the take ends it early (minimum
   50ms). There is only room for one buffer, so the playheads keep reading
   while the take overwrites it: the old loop dissolves into the new one
   rather than being held until the take completes.
4. **Levels 1–3 are three playheads of different lengths.** Each layer loops
   its own region of the same buffer (full, 3/4 and 2/3 of its length in
   Texture; 4:3-ish slot counts in Rhythm), with its own slice map. They
   phase against each other, and Pulse Outs 1/2 (end of layer 1 / layer 2
   cycle) form two interlocking rhythms. (The pulse jacks moved twice after
   this: see 19, then 25.)
5. **ComputerCard v0.3.0** (vendored from the Workshop_Computer repo), not
   v0.2.8 — at 144MHz its CV-out PWM is an exact multiple of 24kHz, which
   removes the aliased tones older versions put on the audio inputs.
6. **Re-randomising is guarded against noise.** CV inputs are ~9-bit
   effective and knobs jitter by ~±10, so a reseed needs the effective chaos
   value to move ≥64/4096 from where the last reseed happened, at most once
   every 20ms, and every new slice map is crossfaded in (no clicks).
7. **Rhythm pulses advance, never reseed.** Each Pulse In 2 edge steps to the
   next slot of the current map; which slot plays is deterministic, so a
   held knob holds the pattern (the "no drift on its own" rule).

## Decisions (fix for garbled audio, 2026-09-22)

8. **0.1.0 overran the audio interrupt on every sample.** Measured
   afterwards by running the compiled firmware on an emulated Cortex-M0+
   (`tools/bench/cycles.py`): ~3,700–4,100 cycles per sample against a
   3,000-cycle budget at 144MHz, and 5,100 at worst. ComputerCard documents
   the result: the ADC/mux/DAC sequence desyncs and the audio garbles. The
   timing had only been estimated by hand.
9. **Two cores, split by rate.** Core 0 (the audio interrupt) runs only
   audio-rate work. Core 1 runs everything control-rate, including every
   division, and hands core 0 a double-buffered parameter block with an
   acknowledge handshake. The audio DSP itself stays on one core, as the
   directive asks. Core 1 is woken once per sample so its activity stays
   locked to the sample clock.
10. **Work is spread so no sample spikes.** The reverb runs at 24kHz, split
    across even and odd samples, with delay lengths halved and filter
    coefficients converted so it sounds the same. When all three layers
    start a grain at once, the starts are staggered by a fixed 0/1/2
    samples, which keeps each layer's own loop sample-exact.
11. **192MHz at 1.15V instead of 144MHz.** Even after the restructure the
    worst sample used ~100% of the 144MHz budget. 192MHz is officially
    supported and on ComputerCard's recommended list (a multiple of 48MHz),
    and brings the worst case to ~81% (16.9µs of 20.8µs). The card runs
    from RAM, so the flash-related caution about overclocking the Computer
    doesn't apply after boot.
12. **Startup settle.** Knob and switch readings are smoothed from zero and
    take ~10ms to settle, so 0.1.0 grabbed every knob as 0 on its first
    sample. Core 1 now waits 100ms before taking the panel.

## Decisions (refinements, 0.3.0, 2026-09-22)

13. **Main fully anticlockwise clears; turning it back up records.** Only on
    the Middle/Down page, only once Main has caught up, and never from CV1.
    Clearing fades the loop out (~5ms), zeroes the buffer a block per sample
    (~47ms, never touching frames a new take has already written), and forgets
    any held clock tempo. The bottom has hysteresis (clear below 40, record
    above 120) so knob jitter can't clear and record repeatedly.
14. **Pulse Ins survive unplugging.** ComputerCard's jack detection feeds
    random bits into an empty input and takes up to ~11ms (32 bits x 16
    samples) to report it empty; during that window a Pulse In sees a burst
    of fake edges. That was the "loop stops when I unplug Pulse In 1" bug (the
    edges toggled recording) and the lost tempo on Pulse In 2. Edges now wait
    20ms and are dropped if the jack is reported empty. Recording uses a
    20ms-delayed copy of the input, so takes still start on the trigger. The
    Rhythm clock is an internal beat locked to confirmed edges, which keeps
    hits on time and keeps the tempo after unplugging (until a new clock or a
    clear). Tested by simulating the probe's fake edges.
15. **Reverb stays on with the switch Up.** CV2 now applies in every
    position (it used to drop out in Up, taking the wet level with it).
16. **Fades.** Every grain fades in and out over at least 4ms, a reused
    voice fades out over ~5ms, and each take's first/last 4ms are faded so
    the loop point doesn't click.
17. **Whole-octave scatter.** Pitch is only ever 1/4, 1/2, 1, 2 or 4x. Each
    whole playhead can also run backwards (up to 1 in 3 at full chaos), with
    individual slices flipping on top.
18. **0.75-second 16-bit buffer (was 1 second, 12-bit packed).** The new
    features took the worst-case audio interrupt to ~93% of budget, and
    unpacking 12-bit samples was the largest single cost (~85 cycles per
    voice). The same 144KB holds 0.75s of plain 16-bit audio. With that and
    grain starts moved off the sample that requests them: worst case 86%,
    typical ~70%.

## Decisions (0.3.1, 2026-09-23)

19. **End-of-cycle pulses moved to L2 and L3** (they were L1 and L2). L1 is
    the full-length layer -- its loop is the one you hear as the loop -- so
    the jacks now carry the two shorter, more rhythmically useful layers.
    In Texture that makes the two outputs 562.5ms against 500ms (9:8,
    drifting through a full cycle every 4.5s) rather than the old 4:3. In
    Rhythm the pair follows the slot counts: 3/3 at the lowest chaos (both
    outputs in step), then 6/5, then 12/11. (Superseded by 25: that 3/3 at
    low chaos, and the drifting 9:8, are exactly why they moved again.)

## Decisions (shimmer rework, 0.4.0, 2026-09-23)

20. **The shimmer sounded flat because of the pitch shifter's window.** A
    two-tap shifter recycles each head once per crossfade window, and the
    ripple lands at (rate / window): 23Hz with the original 1024 ticks at
    24kHz, which is a buzz rather than a shimmer. The Barber's Pole card's
    `octave.h` reaches the same conclusion by measurement ("the window has
    to be long enough that the taps are still meaningfully separated").
    Now 4096 ticks (171ms, ripple 5.9Hz), with a smoothstep crossfade in
    place of the triangle so the recycle leaves no kink. Costs 16KB more
    RAM.
21. **A DC blocker on the shimmer return.** The shifter passes DC, so any
    offset recirculated and grew -- the reason the return had been limited
    to 0.2, leaving the octave 22dB below the fundamental at the start of
    the tail. With the low end blocked (~15Hz, filter state kept 8 bits
    finer so it can't stick), the return runs at 0.5 and the octave arrives
    within about 1dB of the fundamental. Measured stable at 0.6; 0.5 keeps
    margin.
22. **Sub-LSB writes into the tank are flushed to zero.** Integer rounding
    round the loop left a single unit circulating forever: a -1 DC offset
    still present a minute after the input stopped, at several decay
    settings. One unit is an eighth of an output LSB. Every setting now
    reaches true silence.
23. **Tank sweep tied to the decay knob** (Dattorro's excursion, as Reverb+
    implements it): the first allpass of each tank half has its read tap
    swept, the two halves in opposite directions, depth following decay. 14
    ticks at full decay measures as +/-0.4% to +/-6.4% of level movement
    across the knob; 16 and above jump to 17-38%, which is chorus rather
    than animation.
24. **Paying for it.** The rework cost ~250 cycles per sample, taking the
    worst case to 93% of budget. Recovered by having core 1 precompute each
    slot's start frame and pitch (grain starts were the worst samples),
    keeping the tank dead zone off the lines outside the feedback loop, and
    halving the input diffusion (4 allpasses to 2). Back to 86%.

## Decisions (0.4.1, 2026-09-28)

25. **End-of-cycle pulses moved to L1 and L3** (19 had put them on L2 and
    L3). L2 and L3 differ by only 9:8, so the two jacks drift slowly past
    each other: measured over two minutes, 12.6% of pulses land within 20ms
    of each other, but they arrive in long runs of near-misses rather than
    on a cycle -- flams. In Rhythm at the lowest chaos the two layers share
    a slot count (3 and 3) and fire together outright. L1 against L3 is 3:2
    in Texture and 4:3, 8:5 or 16:11 in Rhythm: more pulses coincide (50% in
    Texture) but each coincidence is exact and on a 1.5s cycle, which reads
    as a polyrhythm instead of sloppy timing.

## Decisions (0.4.2, 2026-09-28)

26. **A clock changes timing, not pitch.** The brief had Pulse In 2 "clock
    the buffer/loop speed", and that was built as varispeed: one `speed`
    scaled both the slot clock and the playheads, so tempo dragged pitch
    with it like tape. In use that was messy -- the loop detuned itself
    whenever the clock moved, especially in Rhythm. Now `slotSpeed` (how
    fast slots are played through) and `readRate` (how fast the audio is
    read) are separate, and a clock only moves the first. Measured with a
    recorded 1kHz tone clocked to 1.33x: all the energy stays at 1000Hz,
    where before it moved to 1333Hz. What gives instead is the material --
    slots cut short, or overrunning and repeating -- which is the normal
    sound of a sliced loop following a clock. Setting `readRate` to
    `slotSpeed` restores varispeed in one line.
27. **The slot clock carries 16 fractional bits, not 12.** A clock period
    rarely divides the take exactly, and at Q12 the rounding left the loop
    drifting about a sample per cycle against the clock -- ~3.6ms a minute.
    At Q16 the worst case measured over 90s is 1.15ms (0.8ms a minute).
    The loop follows the clock's rate; it isn't phase-locked to it, which
    would mean resetting the loop's phase on every edge.
