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
