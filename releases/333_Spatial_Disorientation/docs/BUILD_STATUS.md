# Build verification history

Original verification notes © 2026 Adrian Vos (soveda), MIT. Platform/dependency
credits: [../THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Current alpha10
results are in the final section; the scaffold record below is historical.

## Initial scaffold

Verified 2026-10-07 on macOS with Pico SDK 2.3.0 and ARM GCC 15.2.1.
Both CMake configuration and Release compilation completed successfully and
produced ELF and UF2 files in the ignored build directory.

The scaffold uses approximately 19 KB flash and 23 KB main RAM, with a 2 KB
core-0 stack reservation. These figures do not predict the finished effect size.
No hardware flash, audio listening test, reset test or callback timing measurement
has been performed. Effect DSP and the Spatial Disorientation editor are pending.

The scaffold was rebuilt successfully after changing the initial CPU target to
192 MHz and core voltage to 1.15 V, following ComputerCard's clock guidance.
Audio remains 48 kHz. This build check does not constitute hardware validation
of the forthcoming spatial DSP or editor.

## Alpha9 build checkpoint — 2026-10-09

Pico SDK 2.3.0 / ARM GCC 15.2.1 build passes at 192 MHz / 1.15 V, 48 kHz,
32 HRTF taps. Flash image 80,064 bytes; main RAM 113,528 bytes; scratch banks
2,048 bytes each. ComputerCard 0.4.0 remains unmodified. Startup, modes, DSP,
room, 64-frame transport, 8mu and MIDI TX host tests pass with AddressSanitizer /
UndefinedBehaviorSanitizer. Editor lifecycle/preset/mode tests pass in Node.
Block regression includes independent source distances and panel levels.
At this build checkpoint, alpha9 hardware validation was pending. Alpha8 and preset passes are
user reports, not newly repeated hardware measurements.
Original record: Adrian Vos (soveda), 2026, MIT; sources in ../THIRD_PARTY_NOTICES.md.

## Alpha9 Spatial Mixer hardware report — 2026-10-09

The user reports the supplied Spatial Mixer test protocol passes. Two reported
peak callback / 64-frame block readings are 7 / 1021 us and 8 / 996 us, both
with 32 HRTF taps. Both are below the callback 18 us / block 1200 us warning
thresholds; actual 64-frame deadline is 1333.3 us. No change to firmware is made.
The USB roles corresponding to the two readings were not specified. Do not infer
separate alpha9 8mu or full Twin Orbits regression passes from this Mixer report.
Source: user hardware test report in this conversation, 2026-10-09.
Original record: Adrian Vos (soveda), 2026, MIT.

## Alpha10 Mixer 8mu build — 2026-10-09

Firmware builds at 192 MHz / 1.15 V, 48 kHz with 32 taps, unmodified ComputerCard
0.4.0. Flash image 80,492 bytes; main RAM 114,048 bytes; scratch banks 2,048 bytes
each. Host sanitizer checks pass for startup, modes, Mixer 8mu mapping, original
8mu controls, DSP, room, block transport and MIDI TX. Editor lifecycle tests pass.
The mapping supports source-aware fader pickup, A/B button selection and safe
panel handback; Mixer ignores motion, C and fader 8. No DSP or storage change.
Alpha10 hardware validation is pending. Alpha9's user Mixer pass is historical,
not an 8mu result for this build. Original record: Adrian Vos (soveda), 2026, MIT;
source/dependency attribution in ../THIRD_PARTY_NOTICES.md.

Alpha10 Mixer 8mu tests reported passed by the user on 2026-10-09. No new timing
readings supplied. Record: Adrian Vos (soveda), 2026, MIT.

## Alpha11 mode-settings build — 2026-10-09

Firmware build passes: flash image 82,892 bytes, main RAM 116,528 bytes and 2,048
bytes in each scratch bank. 192 MHz / 1.15 V, 48 kHz, 32 taps; ComputerCard 0.4.0
unmodified. Nine sanitizer-backed host suites and actual editor-script lifecycle /
preset tests pass. Native browser checks confirm the default page loads and hides
Mixer-only fields. Storage migration/isolation and restored pickup pass host checks;
hardware verification is pending. User reported no editor reply while development
changed the page to v2; old firmware responses now show an explicit upgrade message.
Original verification notes: Adrian Vos (soveda), 2026, MIT; sources credited in
../THIRD_PARTY_NOTICES.md. Firmware packaging includes preserved notices.

## Alpha11 user validation — 2026-10-09

The user reports "alpha 11 passes" after the supplied combined settings/preset
protocol. Record this as the user-reported alpha11 pass, without inferring new
timing numbers or a specific run duration. No firmware change. Disorientation
remains reserved; agree its design before implementing the third mode.
Source: user hardware report in this conversation. Record: Adrian Vos (soveda),
2026, MIT; existing platform/dependency attribution remains applicable.

### Alpha11 timing report — 2026-10-09

Following the alpha11 pass, the user reports peak callback / 64-frame block times
9 / 1015 us and 11 / 991 us, both at 32 HRTF taps. Both are below warning thresholds
18 / 1200 us; the actual block deadline is 1333.3 us. USB roles and run duration
were not specified. These supersede the earlier absence of alpha11 timing numbers,
without changing firmware. Source: user hardware report in this conversation.
Record: Adrian Vos (soveda), 2026, MIT.

## Alpha12 first Fig8 build — 2026-10-09

Build passes: flash image 86,096 bytes, main RAM 121,872 bytes, 2,048 bytes in each
scratch bank. 192 MHz / 1.15 V, 48 kHz, 32 taps, unmodified ComputerCard 0.4.0.
Ten address/undefined-sanitizer host suites and actual editor-script tests pass.
Geometry is checked against a mathematical double-precision reference across
100,000 parameter/phase samples; DSP uses fixed point. Tests also cover freeze,
clock/reset, CV clamps, independent distance, output bounds, Fig8 block transport,
v1/v2 migration, third-bank isolation and preset/editor behaviour.

Hardware timing, perception and stability are pending. Alpha11’s user-reported
9/1015 us and 11/991 us are historical, not this build’s results. Alpha11 remains
available as fallback. First Fig8 performance controls are panel/CV/editor; 8mu
D timing feedback remains, with no Fig8 fader/motion mapping yet.
Original verification record: Adrian Vos (soveda), 2026, MIT; platform and measured
HRTF attribution retained in ../THIRD_PARTY_NOTICES.md.

## Alpha12 user validation — 2026-10-09

The user reports "tests pass" following the alpha12 Fig8 hardware protocol.
Record this as the user-reported first Fig8 pass. No new callback/block timing
readings, detailed listening observations or run duration were supplied; do not
infer those from this confirmation or from alpha11 results. Firmware is unchanged.
Fig8 8mu performance mapping remains future work; panel/CV/editor controls and
D-held timing diagnostics are the current implementation.
Source: user hardware report in this conversation. Record: Adrian Vos (soveda),
2026, MIT; existing platform/dependency attribution remains applicable.

## Alpha13 Fig8 8mu build — 2026-10-09

Build passes: flash image 88,336 bytes, main RAM 124,248 bytes, 2,048 bytes in each
scratch bank. 192 MHz / 1.15 V, 48 kHz, 32 taps, unmodified ComputerCard 0.4.0.
Eleven address/undefined-sanitizer host suites and actual editor-script lifecycle
tests pass. New control tests cover independent fader pickup/mapping, held phase
freeze/resume, reset edges, bounded tilt/gyro depth, motion amount, panel handback
and fresh USB session safety. Existing Orbits/Mixer control/DSP tests pass.

No renderer, flash, JSON or SysEx schema change; host controls remain volatile.
UF2 block headers, RP2040 family/address bounds, saved-sector exclusion and all
firmware checksums verified. Alpha13 hardware/offline 8mu timing and stability
remain pending; alpha12 hardware acceptance does not validate this build.
Original verification notes: Adrian Vos (soveda), 2026, MIT; external sources
credited in ../THIRD_PARTY_NOTICES.md with preserved license/measurement terms.

## Alpha13 user validation — 2026-10-09

The user reports "tests pass" following the alpha13 Fig8 8mu hardware protocol.
Record this as the user-reported alpha13 8mu pass. No new callback/block timings,
LED-band measurements or run duration were supplied; do not infer specific
values from the confirmation or previous builds. Firmware remains unchanged.
Source: user hardware report in this conversation. Record: Adrian Vos (soveda),
2026, MIT; existing platform/dependency attribution remains applicable.

## Alpha14 movements build — 2026-10-10

Build passes: flash image 90,496 bytes, main RAM 127,504 bytes, 2,048 bytes in each
scratch bank. 192 MHz/1.15 V, 48 kHz, 32 taps, unmodified ComputerCard 0.4.0.
Twelve address/undefined-sanitizer host suites and actual editor lifecycle tests
pass. New/extended checks cover pendulum retracing/collapse, deterministic Wander
continuity/reversal across phase wrap/reset/freeze, all-path audio bounds, exact
block transport, D tap-on-release vs diagnostic hold, old-record migration and
movement isolation, v1/v2 preset migration and v3 export/validation.

UF2 headers, RP2040 family/address bounds, reserved saved-sector exclusion and
all firmware checksums verified. Alpha14 hardware testing is pending; alpha13's
user pass does not establish new-path sound or this build's timing. Original
verification record: Adrian Vos (soveda), 2026, MIT; existing platform/dependency
and measured-HRTF notices remain in ../THIRD_PARTY_NOTICES.md.

## Alpha14 user validation and timing — 2026-10-10

The user reports "tests pass" following the alpha14 movement-selection protocol,
with peak callback 13 us and peak 64-frame DSP block 997 us. Both are below their
18 us / 1200 us warning thresholds; the block deadline is 1333.3 us. No run
duration or USB role for these measurements was specified. Do not attribute
these timings to an offline 8mu session or infer full ISR timing from callback
telemetry. Firmware remains unchanged.
Source: user hardware report in this conversation. Record: Adrian Vos (soveda),
2026, MIT; existing platform/dependency attribution remains applicable.
