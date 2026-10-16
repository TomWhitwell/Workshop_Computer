# Spatial Disorientation — design options

Design sketch by Adrian Vos (soveda), 2026-10-07. Original notes: MIT.
These are options for discussion, not implemented features or a replacement for
the approved initial design. The Leslie card has now been merged upstream;
Spatial Disorientation is the next card. Twin Orbits and initial Spatial Mixer are implemented in alpha9. Disorientation
is reserved for later. The options below are historical design candidates; current
controls and licensing/source attribution are in README.md and THIRD_PARTY_NOTICES.md.

## Musical directions

| Direction | Musical result | Three-knob emphasis | Trade-off |
| --- | --- | --- | --- |
| Twin orbits | Two mono sources circle the listener together or in opposite directions | Position / orbit speed / distance | Immediate and close to the approved concept; individual source offsets use defaults or configuration |
| Place two sources | Build a headphone scene with independently positioned sounds | Angle / distance / source separation, or select which source is being edited | Better manual mixing; independent editing adds a page or selection gesture |
| Disorientation instrument | Sweeps, figure-eight-like paths and near/far movement, with controllable Doppler | Path / motion rate / excursion | More overt effect character; less direct control of source coordinates |

Twin orbits is the recommended first prototype. Two audio inputs remain independent,
then both render to left/right. Try one rich sustained sound plus a contrasting
percussive source; one input alone also works without duplicating the other.

## Proposed first-prototype controls

- Main: base angle around the head.
- X: orbit rate, centre stopped, left/right for the two directions.
- Y: distance, linking attenuation, darkening and direct/reflected balance.
- Switch Up: linked orbits with a fixed source separation.
- Switch Middle: opposite orbits.
- Momentary Down: reset orbit phase, matching the saved initial design.
- CV In 1: position offset; CV In 2: distance offset.
- Pulse In 1: clock-sync orbit; Pulse In 2: phase reset.
- Audio In 1/2: two mono sources; Audio Out 1/2: binaural left/right.

A hold-to-freeze Down gesture could replace the switch reset if preferred, with
reset retained on Pulse In 2. This is an alternative, not an approved change.
Clock division, angle convention, zero-rate deadband and source separation need
explicit definitions before implementation. Reserved CV/pulse outputs could later
provide source-position modulation or a once-per-orbit pulse if useful.

## Rendering choices

### Lightweight parametric spatial cues — start here

Use interaural time/level differences, direction-dependent head-shadow filtering,
and short early reflections. This is small enough to prototype while retaining
ComputerCard 0.4.0 and per-sample audio, with slower geometry updates.

The goal is audible moving sources around the listener, with stronger distance
character than ordinary pan. Front/back externalization and elevation are
listening-test goals, not guaranteed outcomes. Begin with horizontal motion and
headphone tests, including mono compatibility.

### Measured HRTF rendering — possible later experiment

Direction-specific filters from a measured head/ear response may offer more
specific front/back/elevation cues. This needs a suitably licensed dataset with
full attribution, filter interpolation, memory/CPU estimates and listener testing.
Long convolution could justify block processing; short FIR filters might not.
Alpha3 now experiments with 32-tap horizontal minimum-phase filters derived
from Gardner and Martin's MIT KEMAR data; see ../vendor/KEMAR/SOURCE_TERMS.md.
Hardware timing and blind localization still require testing.

Do not make the first card depend on convincing elevation before horizontal
movement and distance are musically useful.

## Doppler and room character

Doppler can be audible and deliberately exaggerated for fly-bys, or minimized
when moving sources so pitched material stays steadier. Choose whether it should
be a musical feature or a subtle by-product. This is independent of orbit direction.

Use short early reflections first for near/far cues. A larger reverb or rhythmic
spatial delay is an extension, not a requirement for the first prototype. The
primary goal is spatial movement rather than another rotary-speaker effect.

## Configuration approach

The saved initial scope includes a WebMIDI/SysEx editor for source separation,
elevation, room, modulation and presets. It can follow the initial fixed-default
DSP prototype, while the three physical knobs remain the performance interface.

A hardware-only card is a simpler alternative, using carefully chosen defaults
and possibly a boot selector for one or two options. That would reduce the
previously agreed editor scope and needs the user's choice before implementation.
Avoid an extensive hidden menu system.

## Reference and attribution

Inspired by Neuzeit Instruments Quasar's two-source binaural spatial positioning.
The manufacturer's manual describes angle, height, distance and room parameters,
and discusses listener-dependent limitations of spatial perception:
https://www.neuzeit-instruments.com/mediafiles/Manuals/Quasar_Manual_1_0.pdf
No Quasar firmware, graphics, manual text or transfer-function data is included.
See ../THIRD_PARTY_NOTICES.md for platform attribution.

## Subsequent user decisions

Target 192 MHz / 1.15 V with 48 kHz audio from the start. Retain a web editor
as the configuration interface. The three musical directions may coexist as
separately configured functions selected at startup, with one active at a time.

## Future option: 8mu live control

User-requested option recorded on 2026-10-07: integrate 8mu for live performance,
including its gyroscopic controls to control the sound. Candidate mappings include
source position, orbit speed/direction, source separation and distance; elevation
could be a target if implemented later. These are possibilities, not fixed mappings.

Consider editor-configurable assignments, sensitivity, smoothing and a neutral
position/recentre gesture so motion feels playable. Confirm the available motion
messages and connection/USB roles against official 8mu documentation before
choosing a transport or implementing support. Preserve physical knob/CV control
and define how simultaneous control sources interact.

This is future work; 8mu control is not implemented in Twin Orbits alpha1.
Concept requested by Adrian Vos; this note © 2026 Adrian Vos (soveda), MIT.
No device protocol or compatibility claim has been verified for this planning note.

## Staged refinement: headphone elevation (2026-10-10)

The user requests keeping up/down binaural rendering as a future refinement.
Start with a small measured elevation bank and stationary blind listening tests,
then validate timing/RAM before defining saved controls, 8mu mappings or vertical
movement paths. The current firmware remains horizontal; this does not block
alpha14 release work. Detailed stages and acceptance criteria:
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md#future-refinement-elevation-rendering-staged-2026-10-10).
Original option record: Adrian Vos (soveda), 2026, MIT. Gardner/Martin KEMAR data
and research attribution are recorded in that plan and THIRD_PARTY_NOTICES.md.
