# Future Notes

These notes are not a change request for the stable production build. The
current firmware has passed hardware testing and should remain the baseline.
Only revisit these ideas if future testing shows missed MIDI events, lockups,
audio glitches, timing problems, or maintainability issues.

## Inter-Core MIDI Handoff

The current application-level handoff between the USB/MIDI core and the audio
core uses shared state and `volatile` flags. It is effectively a small set of
single-value mailboxes, not an event queue.

Examples:

- MIDI note input writes `pendingMidiNote`, `pendingMidiVelocity`, and
  `pendingMidiNoteOn`.
- Turing MIDI output uses `pendingTuringMidiNote`,
  `pendingTuringMidiNoteOn`, and `pendingTuringMidiNoteOff`.
- MIDI CC updates write directly to shared control values such as `pdControl`,
  `waveControl`, `osc2Ring`, and `osc2Noise`, with pickup-reset flags for knob
  handoff.

TinyUSB and the USB MIDI host driver have their own internal FIFOs, but there
is no dedicated C1ZZL3 ring buffer between the two RP2040 cores.

If MIDI timing problems appear, consider adding a small lock-free ring buffer
from the USB/MIDI core to the audio core. It could carry compact events such as:

- note on/off
- pitch bend
- CC updates
- settings changes that should be applied by the audio core

Keep the queue bounded and cheap. Avoid blocking either core.

## Divisions In The Audio Path

There are still divisions reachable from `ProcessSample()`. The stable build
has passed testing, so this is only worth revisiting if future features push the
processor too hard.

Known places to review:

- envelope interpolation in `updateEnvelopeRunner()`
- pitch conversion in `pitchFrequency()`
- noise scaling in `applyCZNoise()`
- Turing CV/note scaling in `stepTuring()` and `queueTuringMidiNote()`
- helper mappings such as `pitchUnits()` and `currentPitchUnits()`

Possible future optimisations:

- Replace envelope per-sample division with precomputed per-stage increments.
- Replace constant divisions with multiply-and-shift approximations where the
  musical error is acceptable.
- Cache pitch calculations where possible, especially when controls have not
  changed.
- Keep Turing step calculations outside the per-sample fast path when possible.

Any optimisation should be tested against audible behaviour before promotion.

## Code Cleanup

The code has grown through hardware-led iteration. If future work becomes
difficult, consider a cleanup pass after creating a rollback UF2 and source
snapshot.

Possible cleanup areas:

- Separate MIDI parsing, Web MIDI SysEx, persistence, oscillator rendering, and
  Turing machine logic into smaller sections or files.
- Make inter-core shared state explicit in one struct.
- Document which values are written by the USB/MIDI core and read by the audio
  core.
- Keep the production behaviour unchanged during cleanup.

Do not combine cleanup with new audio/MIDI features. Make one kind of change at
a time and hardware-test before promotion.
