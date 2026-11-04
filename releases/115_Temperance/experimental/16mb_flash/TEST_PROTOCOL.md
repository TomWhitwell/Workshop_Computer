# Temperance: original versus 16MB flash experiment

Versions: original **0.1.0** and experimental **0.1.0-flash-exp.1**.
Status: protocol prepared on 2026-11-04; hardware results have not been collected.

## What changes

| Property | Original | Flash experiment |
| --- | --- | --- |
| Storage per loop | 40,000 samples in RAM | 15,998,976 samples in flash |
| Maximum recording at 48kHz | 0.833s | 333.312s: 5m 33.3s |
| Maximum recording at 24kHz | 1.667s | 666.624s: 11m 6.6s |
| Maximum recording at 2kHz | 20s | 7,999.488s: 2h 13m 19.5s |
| Capacity ratio | 1 | About 400 times as many samples |
| Audio storage format | Packed 12-bit PCM; no additional storage loss | Lossy 4-bit IMA-ADPCM, plus a 4-byte decoder-state header per 504 samples |
| Playback speed | Forward/reverse, half/normal/double | Same six zones |
| Shared sample clock | Ten steps, 48kHz to 2kHz | Same |
| Processor | 192MHz, 1.15V | Same |
| Plate and dry path | Fixed 24kHz stereo plate; clean dry bypass | Same processing design |
| Stop-to-play behaviour | Matched 20ms event/input delay, then playback | Same event/input delay, plus queue flush and initial decode wait |
| Loop seam | Short recorded tail blended into loop start | Same method; tail kept in RAM |
| Speed-change transition | 256-tick crossfade | Retained, using two decoded cache heads |
| Starting a replacement recording | Old loop continues briefly while fading out | Last output value fades out for about 20ms |
| Storage starvation | No flash playback dependency | Last output and main play position hold until a block is available |
| After reset | Empty loops | Empty loops; flash bytes are not restored as playable loops |
| Card requirement | Standard card; extra flash does not extend loops | 16MB Winbond W25Q128; other sizes/manufacturers refused |

The capacities above are calculated limits. Half-speed playback takes twice
the recording duration at the same sample clock; double speed takes half.
The original defaults to half speed, so explicitly choose normal speed for
duration comparisons.

## Equipment and common setup

- Workshop Computer and a 16MB Winbond program card. Use the same module and
  card for both firmware runs, or clearly label two cards and keep the module,
  source and recording chain identical. Recording with the experiment replaces
  existing sample data in its flash regions.
- Repeatable source: a DAW/interface or signal generator. Prepare a 0.5-second
  phrase with a clear attack, a sustained tone, a short transient and a quiet
  ending. Keep its level comfortably below input clipping. Also prepare a quiet
  source, speech/noise and a long source with numbered or regular markers.
- Stereo recorder or DAW. Record at 48kHz or higher, with unchanged gain and
  processing disabled. Record the source separately if possible.
- For precise start/stop tests, a pulse source providing two short rising-edge
  pulses 500ms apart. A scope/logic analyser helps measure triggers and latency;
  listening-only tests are still useful.

For each firmware, boot fresh and set these values, passing through stored
values to pick up each knob:

1. Switch up: Main fully left (**Wash = 0**), X fully left (**Dry = 0**),
   Y fully left (**Crush = 12 bits**).
2. Switch middle: Main fully right (**48kHz**), X and Y about 75% clockwise
   (**forward, normal speed**, the 67–83% zone). Check the knob is well away
   from either zone boundary.
3. Audio In 1 receives the source; Audio In 2 initially unpatched. Monitor both
   outputs. Begin with both loops empty and no CV modulation.
4. Record Loop A alone using Pulse In 1. This avoids doubling the same audio
   through two loops during the sound-quality comparison. The switch records
   both loops and is tested separately below.

Keep source level, output gain, knob values and capture format unchanged
between A/B runs. Label captures with firmware, test ID, clock, speed and wash.
Judge playback after the initial fade-in. Align captures in time before
comparing them; an exact waveform null is not expected with ADPCM.

## Practical A/B tests — about 20–30 minutes

### T1 — Boot and basic controls

Boot each version three times. Check the startup chase, empty loops and normal
knob response. On the experiment, a fast all-six-LED flash indicates refusal
or a storage fault; stop the recording test and record the card identity.

Change layers, move knobs away from their saved settings, and return. Values
should wait for pickup and then move smoothly, without unintended jumps.

Pass: repeatable boot and the same panel mapping/pickup behaviour.

### T2 — Short-loop sound quality

Record the same 0.5-second phrase into A at 48kHz with the common setup. Capture
at least ten passes. Repeat with a sustained sine, a sharp transient, speech
or noise, and a much quieter signal. Keep the experimental B loop empty.

Compare at matched playback level:

- Attack detail, quiet-signal grain, sustained-tone distortion and noise.
- Repeating clicks at the loop seam, beyond those heard in the original.
- Dropouts, frozen fragments or periodic interference during playback.

Expected difference: the experiment adds ADPCM colour even at the cleanest
Crush setting. Decide whether that colour is acceptable; it is not an exact
PCM reproduction. Cracks, freezes and intermittent corruption are failures,
rather than expected codec character.

### T3 — Timing, speed and reversal

Use the pair of pulse edges 500ms apart to record A. Capture the stop pulse
and audio output on a common timebase if equipment permits. Repeat ten times
per firmware and log stop-to-first-playback latency. A phrase with a clear
attack makes the measurement easier; report the attack-detection threshold.

Then check each speed zone, allowing the transition crossfade to finish:

| Mode at 48kHz | Expected repeat period for the 0.5s recording |
| --- | ---: |
| Forward or reverse, normal | 0.5s |
| Forward or reverse, half | 1.0s |
| Forward or reverse, double | 0.25s |

Reverse should reverse the phrase, not just change its pitch. Move between
zones repeatedly, including immediate forward/reverse changes. With normal
speed restored, lower the shared clock from 48kHz to 24kHz: duration should
double and pitch should fall an octave. Restore 48kHz and repeat.

Pass: stable repeat periods after playback begins, the same pitch/direction
relationships, and no intermittent stalls. With accurately generated pulses,
use 1ms as a practical period-comparison tolerance. Extra experimental startup
latency must be measured and reported; no hardware latency limit has yet been
established for this version. The experiment should not gradually drift through
repeated cache holds after startup.

### T4 — Independent loops and normalisation

From empty loops, record A for 0.5s and B for 0.7s using their separate pulse
inputs. Start them at different times. Both fit within the original's 48kHz
capacity. Verify that stopping B does not stop A and that their rhythms drift
against each other. Change X and Y independently through all speed zones.

Leave Audio In 2 empty first: both loops should receive Audio In 1. Next,
patch distinctly different signals into the two audio inputs and repeat.

Test a switch-down recording: both loops should start/stop together. Test a
double tap less than 300ms apart: the affected loop(s) should clear. Test
unplugging a pulse cable: it must not unexpectedly start or stop a recording.

Pass: independent lengths/inputs, correct switch operation, and no cross-channel
clearing, accidental recordings or cable-unplug events.

### T5 — Dry path, crusher and wash

First clear both loops. Set Dry to full and feed a sustained source. Sweep the
clock and Crush, then Wash. The live dry signal should retain its pitch and
remain clean, with no reverb generated from the dry signal alone.

Next record a loop, remove the live source and return Dry to zero. Sweep Crush
from 12 down to 4 bits. Sweep Wash from zero to full and capture both outputs.
At full wash, clear the loops and listen to the tail decay.

Pass: crusher affects loops, Wash produces a stereo tail, changing the sample
clock does not retune the existing tail, and the tail decays without runaway
or persistent digital noise. Compare texture and levels with the original.

### T6 — Recording during playback: flash stress

In the experiment, record A for 10s at 48kHz. Play A at reverse double speed,
set Wash fully up, then record B for 30s. Change A's speed/direction frequently
while B records. Stop B, play both, then reverse the roles and record A again.
Use distinct source material for each take so stale fragments are obvious.

Repeat re-recording on the same card: already-used sectors are important to
test. Also run a shorter matching test on the original with 0.5s loops; its
48kHz capacity cannot support the 10s/30s case.

Pass: no storage-fault LED pattern, freezes, resets, wrong-channel audio,
old-take fragments or discontinuities tied to flash writing. A replacement
recording has a different fade from the original; assess that transition
separately rather than treating it as a playback-underflow fault.

### T7 — CV and pulse outputs

Using the short independent loops, check:

- CV Out 1 changes at A's loop starts in whole-volt steps between −2V and +2V,
  without repeating the immediately previous value.
- CV Out 2 moves smoothly between 0V and +5V over B's passes.
- Pulse Out 2 follows B's starts in both directions and at all three speeds.
- Pulse Out 1 emits random 10ms pulses approximately 200ms to 2s apart.

Then patch CV Out 1 to CV In 1 and check musical clock jumps; patch CV Out 2
to CV In 2 and check wash movement. Use the same routing with both versions.
Different random sequences between power cycles are expected.

Pass: the same ranges, timing relationships and useful self-modulation. On a
long experimental loop, smooth random must keep moving rather than rounding
its phase increment down to zero.

## Capacity and longer runs — about 15–25 minutes, plus optional soak

### T8 — Automatic full-buffer stop at 48kHz

Boot the experiment, select 48kHz and normal forward playback, and start both
loops together. Use a long source with recurring markers, including identifiable
material near the beginning and end. Leave recording running until it stops
automatically, around **333.312s per loop**. Capture the state LEDs and audio.

Check that both loops stop, begin playback and repeat correctly. Listen across
the seam. Reverse playback to inspect the end of the recording first. Repeat
at least once on already-written flash. A coarse stopwatch tolerance of ±1s is
adequate for a manual run; a precise pulse/capture measurement should be much
closer to the calculated sample count, with playback startup latency logged
separately from recording duration.

For comparison, the original should auto-stop near **0.833s** at 48kHz. Compare
the state transition and seam behaviour, not the amount of recorded material.

Pass: both full-length loops are usable; no truncated final block, stale ending,
failure to stop, stuck state or storage fault.

### T9 — Low-clock capacity and long-run stability

First compare automatic stop at 2kHz: original capacity is **20s**. The
experiment's full 2kHz recording test takes **2h 13m 19.5s** and is optional
for the initial trial. Recording only a few minutes at 2kHz proves extended
operation but does not prove the full storage limit.

For a shorter soak, play full-length 48kHz experimental recordings for at least
15 minutes, varying direction, speed and clock. Observe the LEDs and CV outs.
There should be no accumulated timing stalls, stuck random CV, resets or
growing noise. An overnight soak is useful before considering a stable release,
but is not required to report the first hardware findings.

### T10 — Reset behaviour

After a successful recording, power-cycle each version. Both should return to
empty loops. The experiment does not implement recall despite storing audio
bytes in flash. Check it can immediately record a new take after restarting.

Pass: expected empty state and a clean new recording, without old-take audio.

## Optional engineering measurements

Use the matching ELF from the experimental build with an SWD debugger to inspect
`temflash::diagnostics`. Take snapshots between runs; halting the processor
during live audio invalidates that interval as a real-time listening test.
There is no USB diagnostic console in the packaged UF2.

| Field | Interpretation |
| --- | --- |
| `queueOverruns` | Must remain zero |
| `hardwareFault` | Must remain zero; 1 program timeout, 2 erase timeout, 3 suspend failure, 4 invalid decoder header |
| `maxQueued` | At most 32 pages; repeated occupancy near 32 indicates little scheduling margin |
| `readMisses` | Initial cache loading can increment this; continuing increases during settled, constant-speed playback need investigation |
| `erases`, `programs` | Should advance during recording and finish flushing after stop |

For CPU timing, make a separate instrumented build and use a verified spare
GPIO or a cycle-timing method to measure the **complete audio interrupt**,
including ComputerCard overhead. Do not choose a pin without checking the
Workshop Computer connections. The existing UF2 has no timing output.

The complete interrupt must finish within **20.83µs at 48kHz**, with useful
margin. Stress both recording queues, both playback heads, direction changes,
full wash and flash erases. A reading around `instrument.Audio()` alone does
not establish the full interrupt budget. Record measurement method, maximum
observed time and test duration; host execution time is not a substitute.

## Results sheet

For each run, note:

| Field | Record |
| --- | --- |
| Firmware / UF2 hash | |
| Module / flash-chip identity | |
| Test ID / source capture | |
| Clock / A speed / B speed / wash / dry / crush | |
| Expected / observed recording duration | |
| Stop-to-play latency: minimum / median / maximum | |
| Repeat period / any accumulated drift | |
| Clicks, freezes, noise or stale audio | |
| Diagnostic counts before / after, if available | |
| Capture filenames / timestamps of problems | |
| Pass / fail / acceptable difference / untested | |

A useful first report is T1–T6 plus one T8 run, with short original/experimental
audio captures. Passing the listening protocol supports an initial hardware
trial; it does not establish every flash-chip variant, long-term endurance or
a proven worst-case execution bound.
