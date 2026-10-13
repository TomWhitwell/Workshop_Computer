# You spin me round: approved initial design

Leslie-style rotary speaker simulation for Music Thing Modular Workshop Computer.
Approved in conversation on 2026-10-07. Implemented and author-tested in alpha3; packaged unchanged as 0.1.0-rc1.

## Performance controls

| Control or jack | Planned behaviour |
|---|---|
| Main | Rotation speed |
| X | Intensity and stereo spread |
| Y | Cabinet drive |
| Z up / middle / momentary down | Fast / slow / hold brake |
| CV 1 / CV 2 | Speed / intensity modulation |
| Pulse 1 / Pulse 2 | Slow-fast selection / brake |
| Audio 1 in | Mono source |
| Audio 2 in | Reserved; unused in the proposed effect |
| Audio 1 / Audio 2 out | Stereo left / right |

CV and pulse outputs are reserved; zero/low until useful behaviour is agreed.
Performance ranges, LEDs and gate semantics are documented in README.md.

## Listening perspective: boot selector

User-approved addition on 2026-10-07. No web editor.

- Hold Z in its momentary down position while starting or resetting the card.
- While held, Main selects one of two perspectives: left half = Outside;
  right half = Inside the cabinet. Add a small midpoint hysteresis to prevent
  ADC noise from flickering between choices.
- Light the left LED column for Outside or the right column for Inside.
- Release Z to confirm and enter performance mode. Consume this release so
  setup cannot accidentally engage the performance brake.
- Main then resumes its normal rotation-speed role. X and Y keep their normal roles.
- Normal startup without holding Z uses Outside. The choice applies to the
  current session; no flash saving is needed for this initial simple design.

Check startup switch state only after ComputerCard input scanning has settled;
read switch and knob values within ProcessSample. The selector is implemented and covered by host tests.

Outside is the conventional listener perspective. Inside is a creative close-up
perspective with stronger Doppler, amplitude and tonal movement, wider stereo
motion and short cabinet reflections. It is not a claim of physically exact
inside-cabinet acoustics. Keep horn/drum inertia in both perspectives and bound
output levels so a perspective change does not introduce excessive gain.

## DSP direction

Split the mono signal into bass and treble bands. Give the bass drum and treble
horn independent phase, speeds and inertia, with the horn accelerating faster.
Combine variable fractional delays for Doppler movement with amplitude modulation,
stereo geometry and controllable cabinet saturation. Smooth speed changes and brake
transitions. Main moves within the selected slow/fast range; README.md specifies the ranges.
There is no web editor.

## Development checkpoints

1. Independent repository, attributed dependencies, buildable hardware scaffold.
2. Core effect DSP and deterministic host-side checks for meaningful invariants.
3. Performance controls, modulation and feedback, with matching operator docs.
4. Boot selector and listening tests pass per author; measured complete-ISR timing not recorded.
5. Release metadata, versioned UF2 and eventual Workshop_Computer contribution.

Host tests, builds and author hardware/listening tests pass. The user has approved the release-folder copy.

The user approved this release-folder copy, and the reminder to start Spatial
Disorientation has been delivered. Binaural development remains a separate task.

## Alpha3 musical response revision

User reports alpha2 tests pass, but little Outside/Inside distinction and little
audible rotary movement below 9 o'clock on X. Audio remains present. Alpha3
uses a continuous piecewise square-root-like X response, preserving zero and
full-scale endpoints. Inside now has twice the previous horn Doppler excursion,
a larger drum excursion, opposing ear viewpoints, a different horn base delay
and a stronger tone sweep. Outside maximum settings are unchanged.

The goal is musical character and useful control travel. Host tests and renders
check the response; author reports alpha3 hardware/listening tests pass; measured complete-ISR timing remains unrecorded.
