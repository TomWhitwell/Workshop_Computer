# Spatial Disorientation: approved initial design

Original design record © 2026 Adrian Vos (soveda), MIT. Platform, program-card
examples and measurement provenance: [../THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

Two-source binaural spatializer inspired by Neuzeit Instruments Quasar for Music Thing Modular Workshop Computer.
Approved in conversation on 2026-10-07. This records the approved target; implementation status is noted below.

## Performance controls

| Control or jack | Planned behaviour |
|---|---|
| Main | Position around the head |
| X | Orbit speed, with a stationary region |
| Y | Distance |
| Z up / middle / momentary down | Linked movement / opposite orbits / phase reset |
| CV 1 / CV 2 | Position / distance modulation |
| Pulse 1 / Pulse 2 | Orbit clock / phase reset |
| Audio 1 / Audio 2 in | Independent mono sources |
| Audio 1 / Audio 2 out | Binaural left / right |

CV and pulse outputs are reserved; zero/low until useful behaviour is agreed.
LED animation, ranges, CV scaling, and trigger-versus-gate details are now defined
for the initial prototype in README.md. Use soft takeover if editor
settings and physical controls can otherwise cause jumps.

## DSP direction

Render two independently placed mono sources into binaural stereo. Start with
interaural timing, head-shadow filtering, distance attenuation and early room
reflections. Move positions smoothly and support linked and opposite orbits.
Evaluate front/back and elevation perception with listening tests rather than
claiming a full HRTF model. Headphones are the primary monitoring target.

A WebMIDI/SysEx editor is part of the intended instrument: source separation,
elevation, room amount, modulation and presets. It must follow upstream editor
requirements: stable versioned parameter IDs, validated ranges, Read/Apply/Save
separation, safe flash writes, reconnect handling and documented cable/browser
requirements. Any external HRTF dataset must have compatible terms and full credit.
Long delays and the full Quasar feature set are outside the initial scope.

## Development checkpoints

1. Independent repository, attributed dependencies, buildable hardware scaffold.
2. Core effect DSP and deterministic host-side checks for meaningful invariants.
3. Performance controls, modulation and feedback, with matching operator docs.
4. Editor and persistence where specified; on-device timing and listening tests.
5. Release metadata, versioned UF2 and eventual Workshop_Computer contribution.

Do not describe the effect as working until DSP is implemented and tested.

## Confirmed platform and configuration decisions

The user selected 192 MHz as the initial CPU target based on stable experience
with other cards. Use 1.15 V per Chris Johnson's ComputerCard NOTES, set before
raising the clock. Audio remains 48 kHz with ComputerCard 0.4.0. Performance
measurements for this card must still cover its actual workload.

The user supports web configuration as complexity grows. Plan for Twin Orbits,
Spatial Mixer and Disorientation as alternative functions on one card, with
settings saved through the web UI and a startup function selector. Only the
selected function runs. The saved startup default and exact selection gesture
remain to be defined. Twin Orbits and its initial editor are implemented; the other functions remain planned.

## Initial Twin Orbits implementation (0.1.0-alpha1)

The controls above are implemented with ranges, CV scaling, LED roles and edge
semantics documented in README.md. Source levels, separation, room, strength and
clock division are editor settings; knobs have separate roles, so no takeover
is necessary. Elevation and other functions remain planned. Hardware/listening
validation remains pending; see TEST_PROTOCOL.md. Original implementation and
this addendum: Adrian Vos (soveda), 2026, MIT; upstream sources in
../THIRD_PARTY_NOTICES.md.

## Future live-control option

The user requested future 8mu integration, including gyroscopic control of sound.
See OPTIONS.md for candidate mappings and implementation questions. This does not
change the current Twin Orbits alpha1 firmware or its hardware test scope.

## Alpha2 DSP pass (2026-10-08)

Stronger rear high-frequency shadowing and a rear-weighted three-sample
feed-forward spectral cue supplement the existing interaural timing/level model.
No control or settings-schema change. The filter is an original analytical
approximation by Adrian Vos (soveda), MIT, not a measured HRTF. Tests verify
low-frequency retention, high-frequency rear attenuation and strength-zero bypass;
headphone perception and hardware timing/stability remain to be retested.
See IMPLEMENTATION_PLAN.md for the remaining staged work.

## Alpha3 HRTF experiment (2026-10-08)

After the user reported insufficient blind front/back movement in alpha2, the
user approved short direction-dependent ear-response filters. Horizontal MIT
KEMAR diffuse-field measurements by Bill Gardner and Keith Martin (MIT Media
Laboratory, 1994) are converted into a 72-direction, 32-tap minimum-phase bank.
See ../vendor/KEMAR/SOURCE_TERMS.md for data terms and processing. Original generator
and DSP integration: Adrian Vos, MIT. Timing remains analytic; distance darkening
is reduced to preserve measured directional detail. Controls/schema are unchanged.
The user also asked to consider Workshop_BlockAudioCard; its block transport was
reviewed and deferred pending measured timing needs or longer filters.

## Alpha4 block scheduling correction (2026-10-08)

Alpha3 overran the hardware callback on the user's instrument. Following the
user's request to consider Workshop_BlockAudioCard, alpha4 adapts its four-slot,
64-frame/two-block scheduling model while retaining unmodified ComputerCard 0.4.0.
Hardware/control capture remains on core 0; heavy HRTF processing and non-blocking
USB share core 1. The DSP bank/settings are unchanged. Added transport latency is
2.67 ms. Timing diagnostics and over/underrun warnings are part of the test build;
hardware validation is pending. Original integration: Adrian Vos (soveda), MIT.

## Alpha5 optimization and longer-filter comparison (2026-10-08)

The user reports alpha4 front/back localization is possibly very subtle and
requested optimization followed by a longer filter/profile comparison. Alpha5
shares distance/filter state and FIR input history between ears, uses compact
paired MACs, then supplies interaural timing with a short stereo delay. The 32-tap
bank is unchanged; a same-gain 64-tap KEMAR bank is added as a separate test UF2.
Timing telemetry identifies the active tap count. Controls, saved configuration,
block size and output scheduling remain unchanged. Hardware validation is pending.
Measured source data remains credited to Gardner/Martin (MIT Media Lab, 1994);
original optimization/integration by Adrian Vos (soveda), MIT.

## Alpha6 room externalization pass (2026-10-08)

User measured alpha5 at 916 us/block (32 taps) and 1244 us (64 taps), both with
5 us callback. 64 taps did not help noticeably; 32 gave some directional difference
close to the head. The user approved the next pass on room externalization.
Alpha6 retains 32 taps, adds three softened feed-forward arrivals per ear at
7–24 ms, position-dependent early/late and ear balance, and stronger distance
control of reflected/direct energy. Room zero retains the dry path. No additional
config fields, controls, transport latency or startup function. Original room
model/integration: Adrian Vos, MIT; KEMAR attribution/terms remain unchanged.
Hardware timing and perceived placement are pending a new test run.

## Alpha7 speed range (2026-10-08)

User reports alpha6 at 5 us callback / 1015 us block, clearer circling, less
inside-head sound and no echoes. Faster orbits were less convincing; the user
selected 75% of the current maximum. Alpha7 uniformly scales free-running X
speed to +/-1.5 turns/sec, preserving centre stop and clock-driven rates.
The alpha6 room/HRTF renderer and saved schema remain unchanged. Original
range change: Adrian Vos (soveda), 2026, MIT. Extended hardware testing pending.

## Alpha8 initial 8mu integration (2026-10-08)

The user accepts alpha6's 13-minute rapid-CV stability run and reports alpha7
passes. The first 8mu pass must work without simultaneous web editor access.
The user specifies motion taking over Main, with faders controlling X, Y and the
five continuous web settings. Faders 1–7 map to X/Y/separation/room/A/B/strength;
8 is motion depth. Clock division remains saved/default. B toggles motion on
(initially off); tilt/gyro replace Main, CV1 and orbit remain additive. A and panel
Down recenter; C held stops orbit; D held displays offline timing bands. Pickup
prevents fader jumps; disconnect returns to panel/saved settings. No flash writes
from 8mu. USB role is chosen at boot using ComputerCard's Rev1.1 power detection;
older boards retain editor mode. This first host pass needs hardware timing and
motion-polarity validation. Original integration: Adrian Vos, MIT; EightMU and
rppicomidi provenance/edits are in vendor/EightMU/SOURCE.md.

## Preset editor pass (2026-10-09)

User reports alpha8 tests pass and authorizes preset import/export next. Implement
named, versioned Twin Orbits JSON files with the six stable configuration IDs.
Import stages edits, preserves them on connect and requires Apply/Save explicitly;
export captures displayed values. Files work offline; live controls/motion state
are excluded. No firmware/schema/UF2 change. Original implementation and document:
Adrian Vos (soveda), 2026, MIT; source conventions in THIRD_PARTY_NOTICES.md.

## Alpha9 startup selector and initial Spatial Mixer (2026-10-09)

The user specifies Down held at startup, Main thirds, no LEDs for Twin Orbits,
left column for Spatial Mixer, right column for Disorientation; release confirms.
Normal startup always chooses Twin Orbits. This supersedes any planned saved mode
default. The user selects switch A/B editing with Main position, X distance and
Y level, and asks to reserve Disorientation for later. The reserved choice stays
silent with right LEDs lit; it is not presented as a completed effect.

Up selects A, Down selects B, middle retains selection. Each source stores three
volatile control values with near/crossing pickup after source changes. CV1/CV2
modulate both stored positions/distances; editor levels multiply panel levels.
32-tap HRTF/room processing and 64-frame block architecture remain intact. Mixer
ignores clock/reset pulses; normal Twin Orbits is checked against its previous
control generator. Initial selected source takes current knobs; unedited A/B is
front/back, near, full panel level. Shared six-field flash schema stays v1; per-mode
settings and panel-placement presets remain future work. 8mu follows knob roles;
C is unused in Mixer, and panel Down selects B rather than recentering motion.
Original design/code/docs: Adrian Vos (soveda), 2026, MIT. WaveSeq pickup examples,
ComputerCard and KEMAR provenance remain in ../THIRD_PARTY_NOTICES.md.

## Alpha10 Spatial Mixer 8mu control (2026-10-09)

User reports alpha9 Mixer tests pass, at callback/block peaks 7/1021 and 8/996 us,
32 taps. User requests Mixer 8mu compatibility next, explicitly without accelerometer.
Faders 1/2/3 control selected distance/level/position; 4–7 remain room/A trim/B trim/
strength; 8 is unused. A/B select their named sources, C unused, D timing diagnostics.
Motion data is ignored. Source changes rearm source faders against stored placement;
shared faders keep pickup. Panel selection becomes gesture-based to permit remote
selection while the switch is left Up. USB/panel handback rearms panel pickup.
LED 8 identifies A steady / B blinking. No persistence change. Twin Orbits mapping
is unchanged. Original mapping/integration: Adrian Vos (soveda), 2026, MIT; existing
WaveSeq/EightMU/rppicomidi credits remain in ../THIRD_PARTY_NOTICES.md.

## Alpha11 separate settings and fuller presets (2026-10-09)

The user reports Mixer 8mu passes and previously authorized this combined next
pass upon that confirmation. Add two independent six-setting banks and six raw
Mixer placement fields. V1 flash settings seed both banks with default placements;
v2 flash is written only on Save. Read captures actual panel placements using an
SPSC snapshot handshake serviced at 1 kHz; core 1 keeps rendering while waiting.
Save captures placements, retains inactive settings and preserves mute/lockout.
Restoration/Apply rearms pickup; normal boot still selects Orbits. JSON v2 extends
Mixer to twelve IDs, retaining v1 import with defined default placements. New
editor requires SysEx v2 firmware. Runtime 8mu trim overrides and motion/phase are
not automatically saved. No host-role Save gesture; editing/saving and 8mu use
remain separate sessions. Original implementation/design: Adrian Vos, 2026, MIT;
existing external source/measurement attributions remain unchanged.

## Alpha12 first Fig8 (2026-10-09)

User authorizes the first Fig8 pass and considers a separate pendulum unnecessary
for now. Activate the third startup slot with a translated Gerono figure eight:
lateral=sin(phase), depth=sin(2*phase). Main sets depth, Y excursion, X signed
traversal rate. CV1/2 add to depth/excursion. Hold Down freezes phase, release
resumes; Up/middle retain linked/opposing sources. Pulse1 clocks, Pulse2 resets.
At high depth/excursion the path reaches behind, with its crossing in front.

Resolve fixed-point Cartesian angle/radius on core 1 at each source’s staggered
32-sample geometry cadence. Mathematical atan/radius tables avoid audio-rate
floating point and keep this work outside ProcessSample. Existing HRTF/room DSP
renders independently changing source distance. No new external data or DSP
library; original implementation Adrian Vos (soveda), 2026, MIT, with existing
sine-table and renderer attribution retained. Shape/rate/phase remain live.

Add third six-field config bank; migrate v1/v2 flash in RAM, write v3 only on Save.
SysEx/JSON stay v2 and accept mode/tag 2/disorientation. Telemetry includes worker
resolved positions and a freeze byte. First pass excludes Fig8 8mu performance
mapping; D diagnostics still work. Two existing modes/mappings remain supported.
Hardware listening/timing verification is pending; see TEST_PROTOCOL.md.

## Alpha13 Fig8 8mu pass (2026-10-09)

User authorizes Fig8 8mu integration after reporting alpha12 tests pass. Reuse
the Twin Orbits eight-fader layout: speed/excursion/phase separation/room/A/B/
strength/motion amount. A resets phase and recaptures Main/pose; B toggles motion
depth takeover; C held freezes phase; D diagnostics. Panel Down and C combine
with OR; release both to resume. CV remains additive after depth/excursion.

Fig8 depth is bounded, unlike circular Main position in Orbits. Roll offsets
anchored depth; yaw rate integrates a saturated Q12 base at 1 kHz, with smoothed
clamped output. Motion amount scales tilt and rate; no trigonometry or HRTF work
is added to the ISR. Turning motion off/disconnecting preserves owned Main/X/Y
until panel pickup; fresh USB sessions discard fader/motion ownership. Saved
configuration/schema and existing mode mappings remain unchanged. No autosave.

Original implementation/design: Adrian Vos (soveda), 2026, MIT. Uses this project's
MuControls and bounded WaveSeq/EightMU/rppicomidi host adaptation; all attribution
and notices remain in ../THIRD_PARTY_NOTICES.md. Hardware tests are pending.

## Alpha14 Pendulum, Wander and D selection (2026-10-10)

User requests Pendulum and Wander in addition to Fig8, and specifies D press
instead of the proposed chord. Implement tap-on-release (<500ms) cycle and retain
D-hold diagnostics, with LED 1/2/3 identification. No A/B/C remapping.
Pendulum uses a sinusoidal angular arc (Main centre front→back, Y width±90°),
fixed distance 1024. Wander uses original hashed Cartesian nodes, cosine easing,
extended phase segments, reversible traversal and reset epochs. Main scales
front/back depth; Y excursion. All geometry stays in the block worker; existing
HRTF/room renderer and fixed-point control paths remain. Fig8 geometry unchanged.

Add saved movement ID 13 to Disorientation only, SysEx/JSONv3 and flashv4 with
migration of all prior records/presets. 8mu choices remain volatile, preserve
phase/pickup and revert to saved choice on disconnect. Hardware tests pending.
Original design/code/docs: Adrian Vos (soveda), 2026, MIT; no new external code or
data; existing source/measurement notices in ../THIRD_PARTY_NOTICES.md apply.

## Alpha20 isolated smoothing experiment (2026-10-11)

User authorizes a smoothing-only hardware pass following compiled delay/slew
inspection. New `archive/experimental/smoothing-64` copies alpha19 and changes only source
and coefficient slew arithmetic to exact bounded magnitude/sign power-of-two
steps. Legacy Slew is retained. FIR unrolling, delay addressing, geometry cadence,
scene-reference block transport, 192 MHz/1.15 V and both 64-tap sources remain.
Shared delay addressing is excluded because the compiled candidate added register
moves to every FIR iteration. Alpha19 source/UF2 remains the fallback; root source,
editor and Workshop release are unchanged. A copied experimental editor updates
only version/help text. Flash/JSON/SysEx formats and height runtime semantics match.

Original implementation/docs: Adrian Vos (soveda), 2026, MIT; existing dependency
and Gardner/Martin KEMAR notices remain. Firmware build, 756,000 exact audio frame
comparisons, 6,291,468 exact slew steps plus settling curves, DSP/room/block/modes/
movement regressions and editor checks pass. Linked FIR is still 61 instructions
per eight taps with no stack accesses. Actual hardware timing remains pending;
see archive/experimental/smoothing-64/README.md for protocol and firmware notices.

## Alpha21 Mixer 8mu elevation (2026-10-11)

User reports all alpha20 tests pass and requests fader 8 for Mixer elevation.
Independent archive/experimental/mixer-mu-elevation-64 adds selected-source Below/Level/
Above thirds using existing measured profiles, source/session pickup and boundary
hysteresis. Height remains runtime-only, retained on disconnect, reset to Level
on boot, excluded from Save/export. Existing Orbits/Disorientation motion mapping
and Mixer faders 1–7 remain. 8mu LED 8 fast-blinks for height pickup, then is steady
A / slow-blink B, with dim/medium/bright height indication; held D diagnostics win.
Core 0 publishes one aligned sequence/height word per source; core 1 alone owns
applied height state and consumes before block rendering. DSP headers are unchanged
from tested alpha20, at 192 MHz / 1.15 V with two 64-tap sources. Root source/editor
and Workshop release remain unchanged. Build, focused mapping/mailbox tests,
756,000 exact DSP frames, slew and existing 8mu/block/mode/editor checks pass.
Hardware tests are pending. Original code/docs: Adrian Vos (soveda), 2026, MIT;
existing dependency and Gardner/Martin KEMAR notices remain preserved.


## Alpha22 continuous Mixer height and persistence (2026-10-12)

User authorizes continuous Mixer elevation, saving height and staging vertical
Disorientation next. Independent archive/experimental/continuous-elevation-64 replaces
three-state height with 0–4096 continuous Level-to-measured-±40° interpolation
and continuous timing projection, retaining exact alpha21 audio at anchors.
Mixer 8mu fader 8 retains source/session pickup, now continuous; LED brightness
follows height. Other modes remain horizontal with their existing motion mapping.

Add Mixer IDs 14/15, SysEx v4, JSON v4 and flash v5. Read/Save capture live A/B
height via the existing snapshot handshake. Legacy flash v1–v4 and Mixer JSON
v1–v3 migrate heights to Level without automatic flash writes. There is no host
Save gesture; 8mu edits are volatile until reboot and editor Save persists editor
session heights. Root firmware/editor and Workshop release remain unchanged.
192 MHz/1.15 V, two sources/all 64 taps and block architecture remain; intermediate
coefficient blending adds work and needs hardware timing validation. Build and
sanitized DSP/migration/control/editor checks pass; hardware tests pending.

Vertical Disorientation is staged in the experiment's docs/NEXT_VERTICAL_PASS.md:
phase-related Fig8/Pendulum/Wander height, separate saved centre/depth, zero-depth
legacy compatibility, preserve fader-8 motion for the initial experiment, verify
headroom before increasing scope. No vertical Disorientation code is in alpha22.
Original code/docs Adrian Vos (soveda), 2026, MIT; existing Gardner/Martin KEMAR
measurement and dependency attribution/terms are preserved.


## Alpha22 hardware result (2026-10-12)

The user reports tests pass, including saving to card. Record alpha22 hardware
protocol as passed, with persistence explicitly confirmed. No new timing peaks
or run duration were provided, so measured headroom remains unspecified for
continuous height. Alpha22 is the tested fallback for the staged vertical
Disorientation pass. No firmware change. Result record Adrian Vos (soveda), MIT.


## Alpha23 vertical Disorientation experiment (2026-10-12)

User authorizes the staged vertical pass. Independent
archive/experimental/vertical-disorientation-64 builds on tested alpha22, preserving
its complete source/editor/UF2. Original phase-related rise/fall for Fig8, lifted
Pendulum ends, and deterministic third-axis Wander nodes add bounded ±40° height
to the existing renderer. Wander reuses segment/easing/reset/reversal behaviour.
Horizontal direction/distance is unchanged. Geometry stays on core 1; centre
and excursion are captured alongside sample audio/phase on core 0.

Disorientation IDs 16/17 supply centre 0–4096 and excursion 0–2048; defaults
2048/0 preserve old horizontal audio. Separate saved/exported settings use
flash v6 / JSON v5 / SysEx v5. Legacy flash v1–v5 and preset v1–v4 migrate default
vertical values, preserving supported old fields including alpha22 Mixer heights.
Worker telemetry appends rendered height. Read/Apply/Save mute/lockout and
single-writer bounded queues remain. 8mu fader 8 retains Disorientation motion
amount; no height CV/host Save gesture. Mixer height and Orbits remain supported.

192 MHz/1.15 V, ComputerCard 0.4.0, two sources/all 64 taps, 64-frame blocks and
128-frame latency remain. Build, 756,000 zero-depth/reference frames, vertical
path/bounds/freeze/reverse/reset/capture checks, migration/editor and existing
regressions pass under host sanitizers. Hardware sound, timing, persistence and
stability are pending; test intermediate moving heights/high room/rapid CV.
Root source/editor and Workshop release are unchanged. Original code/design/docs
Adrian Vos (soveda), 2026, MIT; existing Gardner/Martin KEMAR and dependency
notices/terms preserved; no new external dataset/code.


## Alpha23 initial timing report and next refinement (2026-10-12)

User reports callback 13 µs, initial Level-height Fig8 block 1,131 µs, then a
1,209 µs block peak and slow-DSP warning first displayed in Wander with nonzero
height. Subsequent Pendulum/Fig8 show the same latched peak/warning; separate
mode peaks are not established without reset. The maximum exceeds the warning
by 9 µs but remains about 124 µs below the processing deadline. No audio-queue
warning is shown; listening/stability/persistence results were not provided.
Do not mark alpha23 hardware protocol passed. Recover headroom before widening
path scope; alpha22 remains tested fallback.

The user requests future selectable sloping paths if vertical movement works.
Stage path-plane tilt (left/right, front/back or diagonal direction and amount)
as a later refinement; its interaction with existing vertical excursion and
±40° limits needs definition. No sloping path code is introduced. Original
result/design record Adrian Vos (soveda), 2026, MIT.


## Alpha24 HRTF row-address cache experiment (2026-10-12)

User authorizes the address-caching experiment to recover headroom at 192 MHz.
Independent archive/experimental/hrtf-address-cache-64 preserves alpha23 source/editor/
UF2 as fallback. Only Hrtf::Geometry resolves primary/Level current/next rows
and intermediate-height status; per-frame Update reuses those immutable pointers.
No coefficient math, rounding, smoothing or geometry rate changes. All paths,
controls, both 64-tap sources, delay reads and room model remain alpha23.
Clock 192 MHz/1.15 V and existing block/queue/flash ownership are unchanged.
SysEx v5 / JSON v5 / flash v6 remain compatible with alpha23's editor and records.
No Wander-node cache, FIR unroll or sloping path is added in this isolated pass.

1,024,000 direct HRTF plus 756,000 renderer/transport exact comparisons and
sanitized vertical/Mixer/settings/DSP/room/control/editor regressions pass.
Linked FIR remains 61 instructions per eight taps without stack accesses/MOVs.
Static Source::Process instructions reduce 784→770 and stack references 74→60
across all paths; these are not cycle/speed measurements. Build passes; hardware
timing remains pending. Root/current firmware/editor and Workshop release are
unchanged. Original code/design/docs Adrian Vos (soveda), MIT; existing KEMAR
Gardner/Martin and dependency notices/terms preserved.


## Alpha24 result and alpha25 FIR unroll experiment (2026-10-12)

User reports alpha24 peaking at 1,199 µs with no audible issues; callback peaks,
exact settings/duration and full protocol completion are unspecified. The margin
to the warning is only 1 µs. User authorizes the isolated 16-tap FIR unroll test.
archive/experimental/fir-unroll16-64 preserves alpha24 source/editor/UF2 as fallback.
Only FIR grouping changes to 16 paired taps per iteration; explicit inlining
retains alpha24's call structure after the first compiler candidate outlined
the larger kernel. All 64 taps/two sources, math/rounding/smoothing, HRTF address
cache, geometry cadence, paths, controls, room/delays and data formats remain.
192 MHz/1.15 V, ComputerCard 0.4.0 and block/core ownership are unchanged.
SysEx v5 / JSON v5 / flash v6 and alpha23/24 experimental editors stay compatible.

Final build passes with 1,024,000 direct HRTF plus 756,000 scene/transport exact
comparisons against alpha24, vertical/Mixer/scalar FIR and existing regressions.
Linked FIR is 119 instructions per 16 taps × four iterations = 476 per source
frame, versus 488 previously. It has two MOVs per iteration but no stack accesses
or calls. Surrounding static stack references rise 60→68; net timing is uncertain
until hardware A/B testing. No sloping paths/Wander cache/clock change in this
pass. Root/current firmware/editor and Workshop release remain unchanged.
Original code/design/docs Adrian Vos (soveda), MIT; existing Gardner/Martin KEMAR
and dependency notices/terms preserved.


## Alpha25 sustained readings and alpha26 slope experiment (2026-10-12)

User reports alpha25 20-minute Fig8 peak 13/1180 µs callback/block and Wander
14/1197 µs. These readings alone do not establish a useful sustained saving
against alpha24's 1199 µs. Audio/queue results were not supplied with these runs.
User explicitly requests a separate sloping-path timing test despite the margin.
archive/experimental/sloping-paths-64 retains alpha25 source/editor/UF2 as fallback.

Add Disorientation uphill bearing ID18 (0..4095 full turn) and tilt ID19
(0..2048 = 0..40°). Fixed-point horizontal unit direction rotation and original
asin lookup resolve height/azimuth at existing staggered geometry cadence.
Distance remains the existing path distance. Existing vertical centre/bob adds
after rotation and clips to ±40°. Thus combined bob+tilt is not a rigid plane;
the inclination is about the listener rather than the translated path centre.
Zero tilt bypasses geometry and preserves exact alpha25 audio. All64 taps/two
sources, 192 MHz/1.15 V, existing DSP/control/queue/core ownership and 8mu mappings
remain. No extra live slope fader is introduced; saved settings apply in host boot.

Dedicated experimental editor/protocol6, JSON6 and flash7 preserve old fields,
migrating old presets/flash to zero tilt. Apply-only leaves physical flash intact.
Before experimental Save export old presets: alpha25 rejects flash7 and JSON6;
restoring its saved banks requires old exports. Build, sanitized independent
rotation/bounds/freeze/180000 slope transport frames and settings migrations,
756000 alpha25-exact zero-tilt scene/transport frames, 1024000 direct HRTF frames
and editor lifecycle/preset/save regressions pass. Hardware timing/listening
pending. Root editor/source and Workshop release unchanged. Original code/table/
design/docs Adrian Vos (soveda), MIT; Gardner/Martin KEMAR and dependency
notices/terms retained, no new external measurements or code.


## 0.2.0-beta1 consolidation (2026-10-13)

User requests a beta for other testers and archival of old versions. Current root
promotes user-tested alpha26, retaining192MHz, two64-tap sources and all controls.
Older root source/editor/docs/UF2 are archived under archive/alpha14; all experimental
builds under archive/experimental with rebased build paths and preserved binary
checksums. Packaged beta UF2 equals alpha26 byte-for-byte; root rebuild differs only
in build-date byte. Dedicated beta editor, operator guide, protocol/migration notes,
feedback checklist and metadata replace historical alpha root documentation.
User reports slope audible; Pendulum25min14/1175µs clean/noqueue, Fig8 20min14/1176µs
(no separate listening/queue report), Wander30min14/1193µs clean/noqueue. Do not infer
an independent board/8mu/USBrole pass from these reports. Further tester acceptance
includes persistence and separateUSBhost/device sessions. No Workshop release
change or new DSP behaviour. Original code/docs AdrianVos(soveda),MIT; existing
KEMAR/dependency notices preserved.

## Separate Twin Orbits height/slope alpha (2026-10-15)

User requests the next alpha experiment adding height/slope to Orbits. Independent
archive/experimental/orbits-height-64 copies beta1 and adds a mode-owned Orbits centre
height, uphill bearing and tilt, IDs16/18/19. Defaults level/zero tilt preserve beta
Orbits; no new bob oscillator. Core0 captures these with phase; core1 resolves
SlopePath and bounded height at existing geometry cadence. Scene identifies Orbits
explicitly so Mixer and Disorientation retain behaviour. Both sources/64taps,
192MHz/1.15V, audio/block scheduling and all physical/8mu mappings stay fixed.

Separate editor/protocol7, JSON7 and flash8 add the three Orbits values, preserving
old supported fields on migration and defaulting only new fields. Beta1 presets
must be backed up before Save because beta1 rejects flash8/JSON7. No host Save
or live height/tilt mapping is introduced. Root beta source/editor/UF2 and archived
fallbacks remain unchanged. Hardware timing/listening/persistence testing pending.
Original implementation/design/docs Adrian Vos(soveda),2026,MIT; existing
Gardner/Martin KEMAR and dependency attribution/terms retained.


## Orbits repeated-save failure and alpha2 candidate (2026-10-15)

User reports alpha1 overall listening works, subtle slope, staticheight audible;
repeated saves first stop responding while audio continues, another attempt stops
audio with rapid phantomCV display and no CV patched. No warningLED. Old code locks
out core0 for flash while ADC/DMA continue, plausibly losing sampling alignment.
A separate archive/experimental/orbits-save-fix-64 retains alpha1 and beta. Local attributed
ComputerCard0.4.0 adaptation pauses ADC/DMA after mute, drains/clears FIFO, preserves
mux/buffer state, restarts round-robin/input0 and existing DMA without reclaiming
channels. Core1 requests, core0 owns hardware. Explicit flash lockout remains.
Save waits for physical pause/restart, always releases requests on failure, reports
status5 and latches a save fault for LED once callbacks run. Unchanged saves skip
flash/mute. SysEx7/JSON7/flash8 unchanged; all DSP/controls remain alpha1. Hardware
confirmation pending. Actual firmware parser/storage tested with repeated-save and
failure-injection mocks; DSP regressions/editor checks pass. Original new code/docs
Adrian Vos(soveda),MIT; ComputerCard Chris Johnson MIT and KEMAR/dependency terms
preserved. Reset init sets height/tilt tozero; it is not a DSP regression.

User clarifies height/tilt failed to reapply after init: Read returned old values,
hardware reset restored effect. Alpha2 tests actual parser/SPSC init/Apply/Read
plus browser stale-ack simulation; editor checks Orbits readback and retains staged
edits on mismatch. No independent init DSP bug has been reproduced. Final build
144300B flash/185104B RAM, tests pass; hardware confirmation remains pending.


## User-passed save fix; Orbits 8mu pages alpha3 (2026-10-15)

User reports the alpha2 save fix passes and authorizes the first Orbits8mu pages
plus editor Randomise/Reset elevation buttons. Separate archive/experimental/orbits-mu-pages-64
retains beta1/alpha2. Short D release<500ms toggles normal vs height/slope; D hold
retains diagnostics. Faders1–3 are speed/distance/separation or height/tilt/uphill;
4–8 retain room/A/B/strength/motion. Page switches rearm1–3 while holding remote
normal values and elevation values, preserve shared-fader ownership and avoid
false initial crossings. A/B/C/motion remain; C release restores prior speed.
Disconnect restores saved elevation and motion-off, with X/Y panel pickup; fresh
connection starts normal. Core0 owns mapping at1kHz and captures runtime heights
with audio. No8mu autosave or settings layout change. Host LEDs1–3 identify normal,
6–8 height briefly; longD diagnostics override notices. Root beta remains intact.

Bottom editor tools stage active-mode elevation only: Orbits centre/bearing/tilt,
Mixer A/Bheight, Disorientation centre/depth/bearing/tilt. Random values avoid
immediate clipping; Reset is level/zero tilt/depth. Apply/Save stay explicit, offline
staging supported, other settings remain. Protocol7/JSON7/flash8 and alpha2 sampler
pause/verified-resume remain. Original code/docs AdrianVos(soveda),MIT, reusing
existing WaveSeq/EightMU pickup adaptation; existing KEMAR/dependency attribution
preserved. Alpha3 hardware mapping/timing/listening validation pending.


## Beta2 promotion after alpha3 user acceptance (2026-10-15)

User reports all alpha3 tests pass, page indicator effective and randomisation
reveals effective elevation; asks promotion. Root0.3.0-beta2 copies alpha3 source/
editor/local ComputerCard adaptation without DSP/control changes. DistributedUF2
is byte-identical alpha3; root rebuild also matches. No new alpha3 timing/duration
numbers inferred. Beta1 complete root snapshot, original UF2 and testerZIP are
archived under archive/beta1. Current metadata/docs/editor/tester package updated;
22C++host and editor regressions run against promoted root. Formats7/7/8 require
beta1 backups before Save; alpha1–3 compatible. No Workshop release changes or
remote publication are implied. Original new code/docs Adrian Vos(soveda),MIT;
ComputerCard Chris Johnson MIT, all existing KEMAR/runtime/dependency notices retained.


## Direct pitch/roll positioning alpha (2026-10-15)

User requests replacing difficult yaw control and explicitly chooses pitch
front/back plus roll left/right. Separate archive/experimental/pitch-roll-position-64,
0.4.0-alpha1-pitch-roll, preserves user-passed beta2. Orbits uses the existing atan
lookup on relative gravity pitch/roll for direct circular position around captured
Main. Neutral square±64 holds last direction; fader8 scales tilt sensitivity and
zero returns anchor. No yaw integration or stale-rate timer; held pose remains valid.
A/B capture both tilt axes and Main. Automatic orbit/CV remain additive. Existing
normal/height pages, pickup, flash safety and LED diagnostics remain unchanged.
Disorientation pitch directly controls bounded Main depth; roll rotates its entire
path before world-space slope. Circular rotation smoothing runs at1kHz on core0,
returns zero when motion disabled, and is captured in each runtime scene. Mixer
ignores motion. No save/protocol layout change: SysEx7/JSON7/flash8.

Original code/docs Adrian Vos(soveda),MIT; existing WaveSeq/EightMU, ComputerCard
Chris Johnson MIT and Gardner/Martin KEMAR attribution/terms retained. No new
external code/data. Build192MHz/1.15V/64taps/two sources: flash147884B/RAM189808B,
scratch2048B each;23C++ASan/UBSan programs and editor checks pass. New tests cover
atan2 reference, directional cardinals, neutral/held pose/yaw rejection and180000
changing-rotation captured-scene comparisons. Hardware load/listening/pickup testing
of this alpha pending; beta2 remains the fallback. No remote publication implied.


## Physical roll polarity correction alpha2 (2026-10-15)

User reports pitch/roll alpha1 tests pass but roll appears reversed. Inspection:
EightMU::Roll is positive with right edge lifted; alpha1 used positive relative
roll for positive (rightward) scene angle. This makes a right-edge-down lean move
left. Isolated archive/experimental/pitch-roll-polarity-64 /0.4.0-alpha2-roll-polarity
subtracts measured roll from captured neutral in both Orbits and Disorientation.
Pitch, raw sensor API, yaw rejection, timing architecture, flash handshake,
settings formats and all DSP code stay unchanged. Tests explicitly assert
right-edge-down = right and left-edge-down = left through Orbits scene placement,
Disorientation path rotation and non-flat neutral recapture. Prior numeric tests
encoded the wrong physical convention; expanded tests name the physical edges.
Original code/docs Adrian Vos(soveda),MIT; dependency/KEMAR notices retained.
Build: flash147876B/RAM189792B,scratch2048B per core,192MHz/64taps/two sources.
23C++ASan/UBSan and editor regressions pass. Hardware confirmation pending;
alpha1 and beta2 code/editors/UF2 remain intact. No remote publication implied.


## Beta3 promotion after pitch/roll alpha2 acceptance (2026-10-15)

User reports tests pass and requests beta promotion with README/docs updated.
Root0.4.0-beta3 copies tested pitch-roll-polarity alpha2 source/tests/editor without
control/DSP changes; distributed UF2 preserves exact tested bytes. Full previous
beta2 root snapshot and original tester ZIP retained in archive/beta2, with rebased
historical links/test paths. Earlier experiments remain. Current docs describe
pitch front/back, corrected roll left/right, neutral hold, distinct fader8 semantics,
Disorientation path rotation and editor-only elevation. Formats7/7/8 unchanged.
23C++ASan/UBSan+editor regressions and root build/metadata validation pass; root
rebuild matches tested firmware. No new hardware peaks/duration inferred. MIT
Adrian Vos(soveda), existing dependency and Gardner/Martin terms retained. No remote
publishing or Workshop release update is part of this promotion.


## Repository cleanup after beta3 — 2026-10-15

User requests tidying and archiving previous tests. Move five superseded0.3/0.4
alpha folders from root experimental/ into archive/experimental/, rebasing CMake,
Markdown and comparison-test paths. Keep current regression tests at root tests/.
Move14 early offline WAV previews into archive/previews/, retain byte hashes and
attribution. Preserve both old generated build caches locally under ignored
archive/local-builds/2026-10-15/. Remove Finder metadata. Current beta3 source,
editor, firmware and settings behaviour unchanged; archived UF2/WAV byte checks
pass. Refresh archive index, operator layout and local tester bundle. Original
housekeeping documentation Adrian Vos(soveda),MIT; all third-party notices retained.
