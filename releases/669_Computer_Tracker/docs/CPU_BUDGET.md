# CPU budget

## Target

* RP2040 at **192 MHz** (core voltage raised to 1.15 V, a common, safe RP2040
  overclock; 192 MHz is a multiple of 48 MHz, which the ComputerCard docs recommend
  for clean ADC/PWM timing).
* `ProcessSample()` runs at **48 kHz** in the ADC DMA interrupt on core 0 →
  **4000 cycles per sample**. ComputerCard's own per-sample work (ADC
  de-interleaving, normalisation probe, PWM/DAC writes, knob smoothing) also has to
  fit in that window.
* Core 1 does USB, the protocol and flash writes. It has no hard deadline.

## How it is measured

1. **On the module**, every `ProcessSample()` is timed with SysTick. The card
   reports the average and the peak over each 50 ms window in the **Status**
   message, and the web app shows them (`CPU now / peak`, turns orange >75 %,
   red >90 %). This number does **not** include ComputerCard's overhead.
2. **On the host**, `tools/cpu_bench` compiles the same engine code
   for Cortex-M0+ with the firmware's flags (`-O2 -mcpu=cortex-m0plus -mthumb`)
   and runs it in the Unicorn emulator with a simple per-instruction M0+ cycle
   model (1 cycle ALU and multiply (the RP2040 has the single-cycle
   multiplier), 2 for loads/stores, 1+N for LDM/STM/PUSH/POP, 2 for taken
   branches, 3 for BL, and 26 cycles flat per divide, which stands for the SIO
   hardware divider plus call overhead). Results are approximate (no bus
   contention, all code in RAM as in the real copy_to_ram build).

```
source /workspace/workshop_computer/env.sh          # ARM toolchain
tools/cpu_bench/build.sh
python3 tools/cpu_bench/run_bench.py tools/cpu_bench/bench.elf 4800 [--profile]
```

## Results (engine only, cycles per sample, 4800 samples = 100 ms; firmware 0.4.0)

| Scenario | 0.3.2 avg / p99 / max | **0.4.0** avg / p99 / max | max % of 4000 |
| --- | ---: | ---: | ---: |
| Idle (stopped, inputs connected) | 1193 / 1452 / 1531 | **822 / 1046 / 1046** | 26 % (was 38 %) |
| Demo song playing | 1596 / 1939 / 1968 | **1437 / 1726 / 1811** | 45 % (was 49 %) |
| Pathological (below) | 2902 / 3285 / 3540 | **3058 / 3417 / 3630** | 91 % (was 89 %) |
| Demo song + reverb (sends 12/80/64/72 on DR1–4, size 84, decay 72 = 1.5 s, lo-fi 48) | – | **1677 / 2090 / 2126** | 53 % |
| Pathological + reverb from all 6 channels at full send, max size and decay, wobble on | – | **3307 / 3648 / 3874** | 97 % |
| Stopped, only the reverb tail ringing | – | **1262 / 1414 / 1487** | 37 % |

(0.3.2 re-measured with the same bench in this session; the numbers in the
older table below were taken with a slightly different scenario mix.)

**What 0.4.0 changed (and what it gave).** All changes keep the drum sounds;
see *Sound identity* below.

1. **Event spreading:** a trigger, a Modal resonator setup, the panel scan
   and the sequencer's row read / scheduled event share one "token": at most
   one of them per sample. A hit starts **one sample after its trigger**
   (Modal two: the first resonator is set up in between); a ringing voice
   holds its last value meanwhile. Ratchets keep their spacing (they are
   timestamps; a deferred one is late by the same 1–2 samples as the hit
   before it).
2. **LEDs one per sample** instead of six in one sample.
3. **CV written only when it changes**, at the exact sample of the note
   (main.cpp), and the sequencer's CV update has a fast path when no CV track
   moves.
4. **Mixer coefficients recomputed only when a parameter changed** (a 4-word
   signature per channel is compared; was a full recompute of one channel per
   sample, 136 cycles average in the pathological case, now 18–29).
5. **Macros read once per panel scan** (cached), not per use.
6. **Saturation LUT: evaluated, not built.** `softclip()` costs ≈15 cycles
   (3 multiplies); a table with interpolation is not cheaper on the M0+.
   Instead `sat()` got a branch hint so the in-range path falls through
   (≈ −1 cycle × 40–60 calls per sample in the worst case).
7. **Noise loudness compensation** (a square root) is recomputed only when
   the filter frequency changed; the square root itself became a table-seeded
   integer `isqrt32_fast`, **checked exhaustively against the old one for all
   inputs it can see** (bit-identical).
8. **32-bit kick pitch:** the sweep increment is computed with two 16×16
   multiplies instead of a 64-bit one (bit-identical).
9. **Earlier voice end:** a voice stops once its output stays below 1 LSB of
   the 12-bit DAC (per-channel threshold from level, pan and master gain;
   Pluck and Modal over a 2 × 4096-sample window). In the demo song the drums
   are active **35 % less time**. Noise, Snare and Metal keep "ghost" stepping
   their noise generator / FM phases for the skipped time, so the next hit
   starts from exactly the same state as before.
10. **Master stage skipped when silent** (no live channel, master filter at
    rest, reverb stopped).
11. **Meters every 4th sample** (display only).
12. **Core-1-only code in flash: not done.** It would free ≈20–30 KB of RAM,
    which is not needed (the reverb fits). Code running from flash (XIP)
    stalls whenever core 1 erases or writes a slot, so every such function
    would have to be proven never to run during a save; flash-write timing
    was the cause of the knob freeze in the earlier Oblique card. Skipped as
    too risky for no gain.

**Why the pathological case did not get cheaper.** Its cost is the voices
and the mixer working on every sample plus a scheduled event on many
samples; the optimisations above mostly remove idle and periodic work. The
0.4.0 bookkeeping (token, early-end checks, send bus, held output) costs about
as much there as it saves. Normal songs got 10–30 % cheaper. The reverb takes
the token on its heaviest third, so it never lands on the same sample as a
trigger, a row read or the panel scan.

**RAM:** 180,500 bytes of 262,144 (**68.9 %**; 0.3.2: 156,040 = 59.5 %). The
reverb uses ≈ 17.2 KB (four 2048-sample 16-bit delay lines = 16 KB, two
allpass rings 0.75 KB, state); the rest of the growth is code (the whole image
runs from RAM) and two small tables.

**0.4.1 (per-step reverb send `W`):** same scenarios, avg / max: idle 829 /
1054, demo 1452 / 1824, pathological 3080 / 3668, demo + reverb 1695 / 2160,
pathological + reverb 3361 / 3934, tail only 1269 / 1495. That is +5…+55
cycles (≤ 1.4 %) against 0.4.0, about the size of the bench's run-to-run
code-layout noise; the effect itself costs one byte compare per drum/IN
trigger, one flag test per sample and, only while a send glides (≤ 256
samples after a `W` change), a few cycles per gliding channel. RAM +1.4 KB
(send-law table and code): 181,916 bytes (69.4 %). Sound identity: unchanged
(the renders are bit-for-bit the same as 0.4.0's).

**0.4.2 (live mutes for the reverb return and the master):** avg / max: idle
832 / 1075, demo 1464 / 1858, pathological 3104 / 3691, demo + reverb
1715 / 2183, pathological + reverb 3367 / 3944, tail only 1296 / 1552. That is
+3…+27 cycles avg (≤ 0.8 %) against 0.4.1; the worst sample of the worst
scenario moved by +10 cycles (98.6 % of the engine budget, 0.4.1: 98.3 %).
Unmuted, the mutes cost one flag test per sample in the master stage and one
per reverb tick; the fade code is out of line and runs only while a mute is on
or fading back. A first version (a by-reference helper and per-sample compares)
cost +72 cycles avg in the reverb case because it forced the master sums onto
the stack; returning the gain by value fixed that. The rest is the bench's
code-layout noise. RAM: 182,460 bytes (69.6 %). Sound identity: unchanged
(the renders are bit-for-bit the same as 0.4.1's).

**0.4.3 (kick click, snare→clap morph, grit):** avg / max, 0.4.2 → 0.4.3:

| # | Scenario | 0.4.2 | 0.4.3 | max % of 4000 |
| --- | --- | --- | --- | --- |
| 0 | idle | 832 / 1075 | 831 / 1072 | 27 % |
| 1 | demo | 1464 / 1858 | 1478 / 1865 | 47 % |
| 2 | pathological | 3104 / 3691 | 3133 / 3756 | 94 % |
| 3 | demo + reverb | 1715 / 2183 | 1729 / 2249 | 56 % |
| 4 | pathological + reverb | 3367 / 3944 | 3395 / 3981 | **99.5 %** |
| 5 | reverb tail only | 1296 / 1552 | 1295 / 1549 | 39 % |
| 6 | 4 + grit 31 on all four drums | – | 3631 / 4218 | **105 %** |
| 7 | 4, snares at B 64 (morph), grit on the Modal voices | – | 3542 / 4130 | **103 %** |
| 8 | 3 + click 100, snare B 64, grit 31 on drums 2–4 | – | 1835 / 2253 | 56 % |

With click and grit at 0 the new code costs one byte test per trigger, one
mask test per sample (`grit_mask_`) and a branch-free noise tap in the snare:
+0…+29 cycles avg, the worst sample of the worst case +37 (99.5 %, 0.4.2:
98.6 %). The kick click runs as its own internal engine code only for the
≈ 1.5 ms it sounds, so the plain kick path is the 0.4.2 code. Grit costs
≈ 59 cycles per voice and sample while it is on (a first version inside the
voice cost ≈ 90 and slowed every voice through the member layout; it now
runs in the engine on the voice output, only for voices whose grit is on).
The morph adds nothing per sample outside the trigger (the blend is set up
once per hit; the noise tap is one multiply).

**Honest reading:** real songs, even with click, morph and grit on three
voices plus reverb (scenario 8), stay near 56 %. The synthetic pathological
case (every voice retriggered on every row at 300 BPM, reverb at maximum)
already sat at 98.6 % in 0.4.2; with grit on all four voices on top it goes
over the engine budget at its peaks (max 105 %, p99 4016 cycles, so ≈ 1 % of
the samples are over), i.e. it can crackle on hardware. ComputerCard's own interrupt overhead is not even included. Grit on
one or two voices, or the pathological case without reverb, fits.
RAM: 183,508 bytes (70.0 %). Sound identity: with click/grit at 0 every render
is bit-for-bit the same as 0.4.2's, and a hash over all engines and B values
matches except snare B 45–83 (the morph zone, changed on purpose).

### Sound identity (tools/sound_check)

`tools/sound_check/render.cpp` renders single hits of every engine (normal
and hot levels), the demo song (with and without the audio inputs) and a
dense pathological song with the engine, writing the mix, every voice's
output ("stem"), the trigger/start times and the CV/gate events.
`compare.py` lines up 0.3.2 and 0.4.0 (lag search, start times, stems).

| Scenario | Result |
| --- | --- |
| Single hits, all 6 engines and variants (Kick, Kick drive, Pluck C3/C1, Noise, Noise sweep, Metal ×2, Modal membrane/marimba/gong, Snare, Clap), normal and hot | **bit-identical** after the intended start delay (1 sample, Modal 2): max difference **0** |
| Demo song | Kick, Snare, Metal stems **identical (0)**; Pluck 1.75 LSB (a retriggered, still ringing string whose retrigger moved by one sample); hits start 0–2 samples later (mean 0.96 = 20 µs); CV/gate events ≤ 4 samples (83 µs) |
| Dense pathological song | start shifts ≤ 7 samples (0.15 ms); Snare/Clap noise differs because the noise sequence is drawn at shifted times (statistically the same sound); at retriggers of loud ringing voices the held sample replaces 1–2 samples of tail (up to 514 LSB, at a point where the old version jumped anyway) |
| Reverb off (sends 0, every old song) | the reverb never runs; nothing changes |

### Lo-fi reverb (0.4.0)

Send bus (mono, post-fader) → 3:1 average to **16 kHz** → two Schroeder
allpasses (75 and 225 samples, g = 0.6) → **4-line FDN** (1129 / 1391 / 1687 /
1999 samples at full size, Hadamard mix, per line a one-pole damping low-pass
and a decay gain from the T60 table, 16-bit lines with optional low-bit
truncation, slow triangle wobble on line 0) → L = line 0 + 2, R = line 1 + 3 →
linear (or sample-and-hold) upsampling → return gain → master bus before the
master filter. The 16 kHz tick is split into three parts (diffusers + tail
detection / read + lines 0–1 / lines 2–3), one per audio sample. Rounding in
the loop is toward zero (decay gain) and away from zero (damping), so the
tail always reaches exactly 0; after a 128 ms window below ½ LSB with no input
the reverb stops (tested for every tone/lo-fi/decay corner). Cost: ≈ 250
cycles per sample on average while running (≈ 6 %, including the send bus),
≈ 300 on its heaviest third; when the song is stopped and only the tail
rings, ≈ 440 (the master stage has to run for the tail too).

## Older results (firmware 0.3.2)


| Scenario | avg | p99 | max | max % of 4000 |
| --- | ---: | ---: | ---: | ---: |
| Idle (stopped, inputs connected) | 1185 | 1446 | 1525 | 38 % |
| Demo song playing (4 drums, 2 CV tracks, IN1 track, both inputs, effects) | 1590 | 1934 | 1962 | 49 % |
| Pathological: 4 voices alternating Snare/Modal at max decay, an effect on every row of all 8 tracks at 300 BPM (drums ratchet ×4, CV1 slide, CV2 ratchet ×3, IN1 ratchet ×4, IN2 ratchet ×8), IN2 following CV2, all mixer FX | 2896 | 3280 | 3709 | 93 % |

For comparison (avg / max): firmware 0.3.1 idle 1208 / 1549, demo 1610 / 1982,
pathological 2919 / 3711 (0.3.2 removed the CV velocity and folds the CV pitch by
octaves; both are noise-level changes); firmware 0.3.0 (two effects per step) idle 1207 / 1548,
demo 1608 / 2044, pathological 2923 / 3708; firmware 0.2.0 (6 tracks) idle
1155 / 1352, demo 1555 / 1886, pathological 2836 / 3718.

### What 0.3.x (8 tracks, IN tracks) costs, and what removing FX2 gave back

* **CPU:** 8 tracks cost about +50 cycles average (+1.3 % of the core) over 0.2
  in every scenario: two more tracks to read and the IN VCAs with their own
  envelopes. Removing the second effect column in 0.3.1 changes the CPU
  numbers only within noise: the extra effect was a few compares per row read,
  and the worst case is set by the voices and the mixer.
* **Busy-sample deferral (kept):** re-measured for 0.3.1 with the deferral
  disabled, the pathological case was 2898 avg / 3865 max (96.6 %), with it
  2919 / 3711 (92.8 %). It costs about 20 cycles on average but takes ≈150
  cycles off the worst single sample, which is the number that matters.
* **RAM:** a song is 32,980 bytes (0.3.0: 49,364; 0.2: 24,788), and the firmware
  keeps two (live + spare for glitch-free loading). RAM use is **156.0 KB of
  256 KB (59.5 %; 0.3.1: 155.6 KB)**, down from 188.5 KB (72 %) in 0.3.0.
* **Flash:** firmware image 65 KB. A slot record is 32,996 bytes, written as
  9 sectors (36 KB) per save. The slots keep 0.3.0's 52 KB spacing so that songs
  saved with 0.3.0 can still be loaded; 6 slots plus the untouched 0.2 slots
  reserve 484 KB at the top of the 2 MB flash.
* **USB:** a full dump or upload is 512 messages of 73 bytes (≈37 KB; 0.3.0:
  ≈53 KB, 0.2: 384 messages, ≈28 KB).

Add **≈150 cycles** for the `ProcessSample()` wrapper (I/O mapping, SysTick,
LEDs) and an estimated **400–700 cycles** for ComputerCard (not measured; it
depends on the library version). That gives:

* normal use: **≈55–65 %** of the core;
* the pathological case is **close to or slightly over budget** for single
  samples (0.4.0: 3630 engine max, 3874 with the reverb at maximum on every
  channel). A rare overrun of one sample does not crash anything, but it may
  produce a click, and ComputerCard's ADC sequencing may glitch for a
  sample. This is the biggest performance risk; check the on-module CPU
  readout on real hardware.

144 MHz (the ComputerCard default suggestion) would **not** be enough
(3000 cycles); this is why the card runs at 192 MHz.

The v2 knob feedback costs the audio core almost nothing: core 0 only copies
a few words into the status block every 64 samples; the Panel report is
built and rate-limited on core 1.

## Master send effect: the 0.3.2 assessment

0.3.2 estimated the options (tempo-synced delay ≈64 KB / 2 %, small reverb
≈6–20 KB). 0.4.0 built the **lo-fi reverb** (above) instead of a delay, as
asked, after taking the idle and periodic work out of the audio core.

## What keeps the worst case flat

* Control-rate work (envelopes, filter coefficients, pitch sweeps) every
  **8 samples**, with linear per-sample amplitude ramps.
* Periodic jobs are **staggered** on different sample phases: panel scan
  (every 8th sample, phase 1), LEDs (every 16th, phase 3), status snapshot
  (every 64th, phase 5), one mixer channel coefficient update per sample.
* The sequencer reads **one track every other sample**, and at most **one drum
  trigger per sample** is started (a queue absorbs ratchets/simultaneous hits;
  worst case 4 samples ≈ 83 µs of skew).
* (v3) Ratchets, delays and gate ends are **timestamps** (one compare per sample
  instead of per-track counters), and at most **one scheduled event fires per
  sample**; a second one due on the same sample waits one sample (21 µs).
* (v3) On a **busy sample** (a voice was triggered or the sequencer read a row)
  the mixer coefficient update and the input VCA control step are skipped; a
  guard runs them anyway after 32 busy samples in a row. The input VCA control
  runs every 4th sample with per-sample ramps.
* The Modal engine computes its three resonator coefficients **one per
  sample** after a hit.
* Silent voices and **unplugged audio inputs are skipped** entirely.
* No floats, no 64-bit multiplies in the audio path (Cortex-M0+ has neither);
  tables are built once at boot.

## Profile of the pathological case

`run_bench.py --profile` attributes cycles per function. The big items are the
Modal resonators (3 SVFs per voice), the snare's noise SVF, and the mixer
(drive soft-clip + tone + pan for 6 channels, then the master SVF and
soft-clip).

## Ideas if it is too tight on hardware

* Reduce Modal to 2 resonators when 3+ voices are Modal.
* Run the master filter at 24 kHz.
* Cap ratchets at 4.
