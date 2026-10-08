# You spin me round — RC1 test protocol

For firmware 0.1.0-rc1. Allow about 25–35 minutes. Record each test as
Pass / Fail / Unsure, with notes. The blank results template is for repeat runs; prior author testing is recorded below.
Original protocol: soveda, 2026, MIT. Hardware/API reference: Chris Johnson's
ComputerCard 0.4.0; see ../THIRD_PARTY_NOTICES.md.

## RC1 confirmation

The author reports all alpha3 tests pass and the sounds work nicely. RC1 is
byte-identical to that tested firmware. The checks below remain a repeatable
protocol for confirmation and other hardware, not a claim that all measured
engineering data has been recorded. The alpha3 musical changes to check are:

1. With Main noon, Y low, switch Up, move X from fully left through 8–10 o'clock.
   Motion should emerge earlier and smoothly; fully left stays stationary.
2. Compare Outside/Inside at X around 9 o'clock, noon and fully right. Restore
   Main to noon after boot selection and let speeds settle before comparison.
   Inside should sound conspicuously more enclosed/extreme, with opposing stereo
   motion and deeper Doppler changes. Report whether it is musical or excessive.
3. Check sustained low/high notes and chord material in both slow/fast ranges.
   Note any excessive pitch wobble, tonal holes, clipping, or apparent level loss.
4. Recheck momentary brake, selector release and LED 5 while moving/modulating
   controls. No measured complete-ISR maximum has yet been recorded.

Then use the full protocol below as needed.

## Setup

- Workshop Computer, program card, stereo monitoring (headphones through a mixer
  or headphone amplifier, or two speakers), and a mono source.
- Ideally use a sustained organ/saw sound, plus low (~100 Hz) and high (~2 kHz)
  sine tones. A slow bipolar LFO and two sustained gate sources cover modulation.
- Feed Audio In 1 only. Audio Out 1 = left, Audio Out 2 = right. Leave CV/pulse
  inputs unpatched initially. Start monitor level low and increase to comfort.
- Main at noon, X at noon, Y fully left, Z Middle. Note source level and mixer
  settings so comparisons are repeatable. Keep any downstream effects off.
- Firmware: uf2/You_Spin_Me_Round_0.1.0-rc1.uf2.
  SHA-256: 678ff40523d110a466d91bd1bf5e8671b69ac3ca8562f02f22cdf838826ec249.

## 1. Boot and reset — about 2 minutes

Flash the UF2, then reset with Z Middle. Audio should start after a short muted
startup and fade-in. Bottom-left LED 4 should be off: Outside is the default.
Reset three times. Repeat with Main fully right and Z Middle: still Outside.
Power-cycle once. Record any failure to start, pop, frozen control or loud burst.

## 2. Perspective selector — about 3 minutes

Hold Z Down while resetting and keep holding. Audio should remain muted.
Turn Main fully left: only the left LED column lights. Turn fully right: only
the right column lights. Move slowly around noon: the small deadband should
prevent flickering. Release Z to confirm Inside, then return Main to noon.
Audio resumes; LED 4 is on and brake LED 3 is off. Main now changes speed.
Reset without holding Z: LED 4 goes off and Outside returns. Repeat the selector
with Outside selected to confirm either choice can be made explicitly.

## 3. Basic effect and stereo spread — about 3 minutes

Use a sustained harmonically rich source. With Z Middle, sweep X slowly from
fully left to fully right. Left should give matching left/right DSP outputs;
movement and stereo difference should increase toward the right. Cabinet tone
and latency remain at X=0; it is not a dry bypass. Compare each output separately,
then stereo, and listen for continuous rather than stepped motion.

Pass: both channels carry audio; movement grows with X; no dropouts or abrupt
jumps. If desired, record X=0 stereo and compare the channels; the real analogue
output paths may differ slightly even though the digital signals match.

## 4. Slow/fast and Main range — about 4 minutes

With X around three-quarters and Y low, sweep Main left/noon/right with Z Middle.
Then select Z Up and repeat. Allow around six seconds to settle after changes.
Repeat Middle → Up → Middle with Main at noon. Listen for gradual speed changes,
not an instantaneous rate jump. The horn should respond ahead of the drum.
Use the high and low tones separately to hear the different responses.

Nominal steady targets (rotations/second):

| Range | Main left: horn / drum | Main right: horn / drum |
|---|---|---|
| Slow | 0.4 / 0.3 | 1.4 / 1.1 |
| Fast | 4.5 / 3.8 | 8.5 / 7.2 |

The upper LEDs show each rotor, so fast motion may look blurred; musical motion
and pitch/tone changes matter more than counting fast flashes by eye.

## 5. Brake — about 3 minutes

Run fast for six seconds. Hold Down. LED 3 lights and the rotors coast toward
rest; keep holding for up to 15 seconds for the drum to stop completely. Audio
must continue through the stationary cabinet. LED 0/1 brightness freezes at the
stopped rotor phases; it does not necessarily go dark.
Release Down: LED 3 goes off and rotation resumes without a second press. The
switch returns to Middle, so the resumed range is slow; move Up to select fast.
Repeat with shorter holds: motion should begin slowing, then accelerate again
on release. Each new hold brakes; there is no retained brake latch.

## 6. Cabinet drive — about 2 minutes

With X at noon, sweep Y from left to right on the same source. Expect increasing
compression/grit, rather than a fourfold output-volume rise. Repeat with a
quieter and a stronger source. Note the point at which the distortion becomes
musically excessive. Listen for crackles unrelated to the intended saturation.

## 7. Outside versus Inside — about 4 minutes

Compare at Main noon, X three-quarters, Y low and Z Up. Let speeds settle before
judging. Use the boot selector to switch perspective, then restore Main to noon;
keep source and monitoring levels unchanged. Compare the same sustained phrase.

Inside should give stronger close-up motion, pitch/tone/loudness variation and
short cabinet reflections. Report whether that reads as being inside a cabinet,
merely more extreme rotary modulation, or an unpleasant comb-filter sound.
A perceived quality difference is useful feedback, not automatically a pass/fail.
Neither mode should produce a sudden excessive level or an unstable tail.

## 8. CV modulation — about 3 minutes

With Main/X at noon, patch a slow bipolar LFO into CV In 1. Confirm rate moves
smoothly; unplug it and confirm Main takes over immediately. Repeat into CV In 2
for movement/spread. Sweep the knobs toward their ends while modulated: values
must clamp rather than wrap. If controllable, try approximately ±1 V, then ±3 V.
CV In 1/2 affect Main/X respectively; neither directly changes Y or perspective.

## 9. Gate priority — about 3 minutes

Use sustained high/low gates of at least 2 ms; long holds make the motor response
clear. These inputs are gate controls, not edge-triggered toggles.

1. With Z Up, patch Pulse In 1 held low: LED 2 goes off and slow is selected.
2. Raise that gate: LED 2 lights and the rotors accelerate toward fast.
3. Set Z Middle while high: fast remains selected. Unplug: switch regains control.
4. Raise Pulse In 2: LED 3 lights and rotors coast to rest. Lower it: resume.
5. Hold Down and Pulse In 2 high together. Releasing Down alone must keep
   braking while the gate is high. Repeat, lowering the gate while Down remains
   held: braking must continue. Release both: rotation resumes.

## 10. Silence, unused jacks and mono — about 2 minutes

Unplug Audio In 1 and wait for the short signal tail to clear. There should be
no sustained oscillation or loud normalisation-probe noise. Patch only Audio
In 2: it should not be heard. CV outputs remain nominal zero and pulse outputs
low (measure if convenient). Sum the stereo outputs in a mixer; note objectionable
bass loss or cancellation. Some colour from moving delay differences is expected.

## 11. Stability soak — at least 5 minutes

Run Inside with high X/Y and fast selected. Exercise all knobs, CVs and gates;
then test Outside similarly. Controls should remain responsive, with no frozen
values, channel swaps or dropouts. LED 5 must stay off. If it lights, record the
settings and sequence and reset to clear it; do not consider that build ready.

An unlit LED 5 is only a callback check, not proof that the complete audio
interrupt meets its deadline. Full timing verification is a developer follow-up:
measure the entire ComputerCard ISR against the 20.83 microsecond sample period
using a scope/debug build. Record maximum duration under simultaneous control,
LED and geometry work. Neither scope instrumentation nor complete-ISR timing
measurement is included in the supplied release candidate.

## Results to return

```text
Firmware: 0.1.0-rc1
Computer hardware revision / card flash size:
Source / approximate level:
Monitoring: headphones or speakers; model if useful
Tests: 1 __ 2 __ 3 __ 4 __ 5 __ 6 __ 7 __ 8 __ 9 __ 10 __ 11 __
Timing-warning LED ever on? Settings/gesture:
Clicks, noise, dropouts or boot issues:
Preferred perspective and reason:
Inside: convincing / useful exaggeration / too extreme / other:
Speed/acceleration/braking feel:
Drive and stereo spread feel:
LED clarity:
Most useful next change:
```

This protocol does not itself approve publication. The user has approved the release-folder copy. The reminder to start Spatial
Disorientation has been delivered.
