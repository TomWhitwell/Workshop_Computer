# Spatial Disorientation implementation plan

Updated 2026-10-09. Original plan © 2026 Adrian Vos (soveda), MIT.
User requirements and hardware reports are the basis for this plan. Platform/API
sources and musical inspiration are credited in ../THIRD_PARTY_NOTICES.md.
The stages below record the original plan and subsequent checkpoints. Alpha14 is
the current movement-selection pass; historical checkpoints follow. Disorientation now
has a panel/CV-controlled crossing path.

## 1. Finish the Twin Orbits listening/timing pass

Compare alpha1 and alpha2 with identical sources/settings. Distinguish audible
front/back spectral colour from convincing spatial placement. Preserve the tested
left/right behaviour, controls, clock/reset and editor persistence. Complete the
new version's stability run and profile full ISR timing at worst load at 192 MHz.
Alpha1 has 20 minutes of issue-free stability reported; alpha2 starts a new run.

If stronger analytical cues still feel like ordinary tonal changes, evaluate
short direction-dependent FIR/HRTF filters as a separate experiment. Select a
compatible licensed dataset, attribute it fully, estimate RAM/CPU before adoption,
and compare listener results. Do not claim accurate elevation from simple filtering.
Block processing is optional if the eventual filter workload warrants it.

## 2. Integrate 8mu using existing working cards

Reuse established patterns for USB host/device roles, MIDI parsing and core 1
ownership. Workshop_Computer/releases/801_WaveSeq documents both direct 8mu host
control and computer-connected editor forwarding; releases/105_voder documents
motion mappings and control ownership. Inspect their actual code/dependencies,
retain the relevant licenses and credit any reused implementation. References:
https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/801_WaveSeq
https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/105_voder

Add live MIDI CC mapping for position, rate, distance and separation, including
motion gestures. Confirm the user's 8mu version and available motion outputs
before promising raw gyro axes. Provide sensitivity, neutral/recentre behaviour,
smoothing and sensible motion limits. Decide additive modulation versus pickup
for controls shared with panel knobs, including return to panel on disconnect.
Support editor forwarding where appropriate. Stress-test rapid motion/MIDI traffic
with audio active; measure rather than quote an untested CPU percentage.

## 3. Extend configuration and presets

Add versioned preset import/export and per-function saved settings, preserving
Read/Apply/Save separation and migration of valid existing settings. Add mappings
for 8mu without making ordinary playing depend on a computer. Consider independent
source offsets/distances and elevation only alongside the corresponding DSP.
Retest malformed messages, disconnect/reconnect and power-cycle persistence.

## 4. Add the two alternative functions

Spatial Mixer: independently place the two sources, with clear source selection
and a consistent physical control layout. Disorientation: intentional motion paths
and near/far movement, with explicit control of Doppler character. Define each
function's controls before implementation, using the common spatial renderer.

Save the default function through the editor and add a simple startup selector;
only one function executes at a time. Agree the startup gesture and knob roles,
avoid hidden menus, and keep mode indication clear. A function selector belongs
with actual implementations, not inactive options in the alpha editor.

## 5. Prepare release only after validation

Finalize controls/LED feedback, test monitoring/mono compatibility and headroom,
profile timing, complete stability/USB/flash tests on available board/card variants,
and update the operator guide, source attribution and MIT notices. Validate release
metadata against the then-current canonical Workshop Computer schema. Generate a
versioned UF2 with checksums and a concrete release/PR summary. Remain in this
independent repository until the user approves copying into releases.

## Alpha3 experiment and block evaluation — 2026-10-08

Alpha2 blind listening remained insufficient. The authorized alpha3 experiment
implements the short measured-filter evaluation described in stage 1 using
Gardner/Martin MIT KEMAR data. Keep alpha2 available for comparison. Next priority:
blind horizontal localization tests and actual ISR measurements, then decide
whether 32-tap approximation quality and per-sample timing are sufficient.

The local Workshop_BlockAudioCard reference was read at the user's request. It
uses 64-frame handoff, core 1 DSP and two-block output scheduling (~2.7 ms), direct
hardware access rather than ComputerCard and no initial jack normalization. Moving
here would require explicit restoration of ComputerCard-equivalent normalization,
clock/reset capture, editor/flash coordination and USB scheduling. No block code
is copied. For 32-tap FIR, fixed-point direct convolution is a reasonable first
experiment; grouping samples does not by itself lower the operation count. Longer
FIRs may justify partitioned convolution or a revised block architecture. Measure
worst-load deadlines before choosing. Do not treat the compiler/memory report as
CPU profiling or a claim that the current callback is fast enough.

## Alpha4 supersedes the per-sample scheduling decision

Alpha3's hardware failure demonstrated that keeping FIR work inside the audio
callback was unsuitable, despite the short filter count. Alpha4 now adapts the
Workshop_BlockAudioCard handoff while retaining ComputerCard hardware service.
The required order is: validate responsive controls and callback/block timings;
validate cooperative USB/persistence under load; then blind localization and
stability. Do not extend filters or add 8mu until those deadlines are measured.
Core 1 now owns both block rendering and bounded USB, so any future host integration
must preserve audio priority and non-blocking service. The spatial filter bank
is unchanged from alpha3; this pass corrects scheduling, not localization tuning.

## Alpha5: current experiment

Optimize repeated per-ear work, then compare matched-gain 32- and 64-tap banks.
The source archive is unchanged; no additional HRTF profile is introduced yet.
First measure worst-load savings on opt32 against alpha4's 1181 us/block. Then
measure the longer bank before accepting any listening improvement. The 64-tap
bank reduces numerical magnitude error, but should be kept only if it helps blind
localization and retains timing margin. Continue with other generic profiles or
listener calibration if longer filters still do not provide sufficient cues.
8mu and other function work remains behind this timing/listening checkpoint.

## Alpha6: current externalization pass

Alpha5 hardware reports favor 32 taps: 916 us/block versus 1244 us with 64 taps,
with no noticeable listening improvement from 64. Keep alpha5-opt32 as baseline.
The approved alpha6 pass adds synthetic directional early reflections and increases
the reflected/direct balance with distance, using the existing room control and
32-tap bank. Validate deadlines under USB load first, then blind outside-head size,
front/back movement, distance and echo/colour tradeoffs. If room only adds width or
slap, evaluate another attributed generic HRTF profile or listener calibration
before expanding functions. 8mu/startup function work remains planned.

## Alpha8: initial direct 8mu pass

Alpha6 stability and alpha7 speed checks are accepted by the user. Alpha8 implements
motion takeover of Main and pickup faders for X/Y/five continuous settings, with
an eighth motion-depth fader and offline timing LEDs. No simultaneous editor is
required; clock division stays in saved settings. Prioritize direct-connect USB,
pickup, recenter/toggle, axis polarity/rate behaviour, disconnect recovery and full
load without warnings. Confirm editor mode separately after reset. USB work shares
core 1 with DSP; host enumeration waits must service the worker. Mapping presets,
sensitivity options, startup functions and other spatial modes remain planned.

## Preset import/export implemented (2026-10-09)

The user reports alpha8 8mu tests pass. The current editor pass adds named v1 JSON
import/export for Twin Orbits' six configuration fields, with strict ranges/function
validation, offline staging, reconnect preservation and explicit Apply/Save. It
runs against alpha8 firmware without a reflash. Browser export/import and simulated
MIDI regressions pass; verify Apply/Save/power-cycle on the user's card. Multi-preset
libraries, live 8mu snapshots and other function settings remain outside this pass.
Next planned feature: define/implement Spatial Mixer and then function selection.
Original checkpoint note: Adrian Vos (soveda), 2026, MIT.

## Alpha9 current checkpoint (2026-10-09)

User reports preset import/export tests pass. Implemented startup selector and
Spatial Mixer per the user's chosen switch A/B layout. The user asks to reserve
Disorientation, so no motion-path effect is included. Default boot is always Twin
Orbits; no saved startup default. Reserved selection is silent with right LEDs.
Host sanitizer tests and firmware build pass; hardware tests are pending.

Next: validate boot gesture, source retention/pickup, independent distance/level,
8mu interaction and worst-load timing/stability. Then extend mode-specific saved
configuration and panel-placement preset support, with v1 migration, if requested.
Design Disorientation before implementing its reserved slot. Elevation and listener
calibration remain separate possible DSP work. Keep the tested alpha8 UF2 available.
This checkpoint supersedes older planned selector/default behavior above.
Original checkpoint: Adrian Vos (soveda), 2026, MIT; source credits in
../THIRD_PARTY_NOTICES.md.

## Alpha10 current checkpoint (2026-10-09)

The user prioritizes Spatial Mixer 8mu compatibility ahead of mode-specific storage,
and explicitly excludes accelerometer control. Implemented selected-source fader
position/distance/level, A/B buttons, source-aware pickup, panel handback and offline
LED selection feedback. Hardware validation pending; host/build checks pass.
Next is the standalone 8mu test protocol, with Twin Orbits regressions separately.
Mode-specific storage and fuller Mixer presets remain possible later work, not part
of this pass. Disorientation stays reserved. Original record: Adrian Vos, MIT;
existing source licenses in ../THIRD_PARTY_NOTICES.md.

## Alpha11 current checkpoint (2026-10-09)

User reports alpha10 Mixer 8mu passes, triggering the previously authorized combined
mode settings/save/export pass. Implemented independent banks, saved/restored Mixer
placements, v2 mode-tagged editor protocol, coherent live Read/Save, v1 flash/preset
migration and twelve-field Mixer JSON. Build/host tests pass; hardware validation
pending. Prioritize old-save migration, per-mode isolation, restore pickup, preset
roundtrip and editor/flash timing. Then agree the Disorientation design before
implementing its reserved slot. Offline 8mu save is not part of this pass.
Original checkpoint: Adrian Vos (soveda), 2026, MIT; external source credits retained.

## Alpha11 user validation — 2026-10-09

The user reports "alpha 11 passes" after the supplied combined settings/preset
protocol. Record this as the user-reported alpha11 pass, without inferring new
timing numbers or a specific run duration. No firmware change. Disorientation
remains reserved; agree its design before implementing the third mode.
Source: user hardware report in this conversation. Record: Adrian Vos (soveda),
2026, MIT; existing platform/dependency attribution remains applicable.

## Alpha12 first Fig8 checkpoint and next work

User authorizes the first Fig8; no separate pendulum is needed for this pass.
Panel/CV path, held freeze, clock/reset, third settings bank and tagged presets
are implemented. Build and host checks pass; the user reports the alpha12
Fig8 hardware tests pass (2026-10-09), without new timing numbers or run duration.
Original implementation/plan: Adrian Vos (soveda), 2026, MIT; external credits in
../THIRD_PARTY_NOTICES.md.

The first panel path is accepted. Next, agree and implement a mode-specific 8mu
mapping and test its pickup/ownership offline. Collect timing readings when
available; adjust path range only if new listening results justify it. Further path variants or additional Fig8 editor
shape settings need a separate design pass; they are not implemented here.
Preserve normal Orbits startup, independent banks and existing Mixer mappings.

## Alpha13 Fig8 8mu checkpoint

User authorizes the next 8mu pass following alpha12 hardware acceptance. Initial
fader/motion/buttons, source-independent pickup, panel handback and offline LED
feedback are implemented. Build and host checks pass; the user reports the
alpha13 8mu protocol passes (2026-10-09). No new timings or run duration supplied.
Next review remaining release requirements and documentation before a release
candidate; additional path/settings work needs a separate design decision.
No new editor parameters, persistence fields or pendulum path in this pass.
Original implementation/plan: Adrian Vos (soveda), 2026, MIT; existing sources
credited in ../THIRD_PARTY_NOTICES.md.

## Alpha14 Pendulum/Wander checkpoint (2026-10-10)

User requests two new movements and short D press selection. Implemented Pendulum,
Wander, live D tap cycle, retained D-hold timing, LED identification, editor saved
movement and schema/preset migrations. Build/host tests pass; the user reports
alpha14 tests pass (2026-10-10), with callback/block peaks 13/997 us, below warning
thresholds. USB role and run duration were not supplied. Keep alpha13 as fallback.
Next review release requirements, documentation, attribution, metadata and
packaging before a release candidate. Further paths need a separate design pass.
Original plan/implementation: Adrian Vos (soveda), 2026, MIT; sources credited in
../THIRD_PARTY_NOTICES.md with all existing notices preserved.

## Future refinement: elevation rendering (staged 2026-10-10)

User requests staging up/down localization as a future refinement. This is
planned work, not implemented in alpha14 and not a prerequisite for its release.
Keep the current two headphone outputs and tested horizontal renderer as baseline.
Original plan: Adrian Vos (soveda), 2026, MIT; research/data sources below retain
their own ownership and terms.

1. Evaluate a small bank of measured below/level/above HRTFs from the existing
   Gardner/Martin KEMAR data. Verify the exact source measurements and preserve
   their attribution terms. Start with stationary positions and a separate
   experimental build, before adding vertical movement or control mappings.
2. Generate 32-tap elevation profiles using the existing offline workflow, then
   implement bounded fixed-point selection/interpolation in the block worker.
   Retain the horizontal option for direct comparison; additional direction
   profiles do not imply longer filters. Profile actual RAM, callback/block time
   and USB host/device load rather than assuming unchanged processing cost.
3. Run blind headphone comparisons with broad-spectrum/percussive and harmonic
   sources. Separate perceived height from simple brightness changes. Generic
   HRTFs may not match the listener's ears; ordinary stereo-speaker playback is
   not the acceptance target. Proceed only if elevation is useful by ear.
4. If that experiment passes listening, timing and stability tests, design
   elevation range/amount, editor persistence/preset migration and possible
   8mu control. Then add optional vertical components to movement paths without
   changing the existing horizontal defaults. Exact mappings remain undecided.

Acceptance: useful above/level/below contrast without visual cues, no regression
of horizontal placement or existing modes, callback below 18 us warning and
64-frame block below 1200 us warning (1333.3 us deadline), no queue faults and
hardware stability in editor and offline 8mu roles. Alpha14's reported 13/997 us
is the comparison baseline, not a prediction of elevation performance.

Research attribution: Bill Gardner, binaural spectral cues and nonindividualized
HRTF listening tests, [thesis](https://sound.media.mit.edu/Papers/gardner_thesis.pdf);
Bill Gardner and Keith Martin, MIT Media Laboratory (1994),
[KEMAR measurements](https://sound.media.mit.edu/resources/KEMAR.html).
Measurement/elevation overview: [SOFA project](https://www.sofaconventions.org/mediawiki/index.php/General_information_on_SOFA).
Existing measurement terms: ../vendor/KEMAR/SOURCE_TERMS.md; full dependency
notices remain in ../THIRD_PARTY_NOTICES.md. No additional data/code imported here.
