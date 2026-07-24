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

## Fully Separate Oscillator Paths

The dual-pitch envelope experiment is intended to sit alongside the stable main
build rather than replace it. If the protocol v3 test passes, the next deeper
experiment is not simply "more pitch lanes" but a fuller split of the two
oscillator paths.

## Three Production Tracks

Future releases should avoid forcing every user onto the most complex version.
The clearer product shape is three maintained choices:

- `C1ZZL3 Core`: the current Workshop Computer release style,
  with the simpler physical interface and the least cognitive load.
- `C1ZZL3 Rad`: the current protocol v9 stable beta direction, using the
  current full-dual Web UI while preserving the Turing machine mode on the
  hardware switch.
- `C1ZZL3 Gnarly`: a future v10-style build that removes or greatly reduces
  the Turing machine role so the physical switch/knob UI can expose more direct
  oscillator 1 and oscillator 2 controls.

The v10 no-Turing `C1ZZL3 Gnarly` branch should be developed as a separate
experiment, not as a replacement for `C1ZZL3 Core` or the v9 `C1ZZL3 Rad`
stable beta. It should answer whether the extra physical dual-oscillator
control is worth losing the Turing mode.

Candidate v10 hardware UI:

- switch up: oscillator 1 edit page
  - Main: oscillator 1 pitch/base or shared pitch
  - X: oscillator 1 phase distortion
  - Y: oscillator 1 wave family
- switch middle: oscillator 2 edit page
  - Main: oscillator 2 detune, level, or pitch offset
  - X: oscillator 2 phase distortion
  - Y: oscillator 2 wave family
- switch down hold: global performance page
  - Main: detune/fine or mix behaviour
  - X: ring modulation
  - Y: noise/grit

Open questions before firmware work:

- Whether oscillator 2 needs level/mix on the panel, or whether detune/ring/noise
  are enough.
- Whether pitch remains shared with separate pitch envelopes, or each oscillator
  gets a distinct pitch base.
- Whether CV In 1 / CV In 2 should affect both oscillators equally or follow
  the currently selected oscillator page.
- How LED feedback should distinguish oscillator 1 edit, oscillator 2 edit,
  and performance edit.
- Whether a no-Turing build should keep Turing CV/pulse code compiled out to
  save CPU/RAM, or merely hide the mode from the panel.

Current decision:

- Keep `experiments/dual-pitch-envelopes` and
  `experimental-firmware/dual-pitch-envelopes` as the stable dual-pitch
  reference.
- Start the next dual-oscillator experiment separately.
- First experimental step: add two-lane PD envelopes.
- Second experimental step: expand from dual pitch plus dual PD toward full
  two-lane oscillator behaviour.
- Do not replace production/main with this later experiment unless it passes
  explicit web app and hardware testing.

Areas to inspect before changing firmware:

- Whether oscillator 1 and oscillator 2 should each have independent pitch
  envelope, PD envelope, amplitude trim, and waveform-family handling.
- Whether the shared amplitude envelope should remain the final voice envelope,
  even if oscillator pitch becomes independent.
- Whether detune should remain a static offset on oscillator 2 or become part of
  the oscillator 2 pitch path after its own envelope.
- Whether ring modulation should continue to combine the two oscillators after
  their independent shaping, or move earlier/later in the signal chain.
- Whether the Web MIDI protocol should add fully separate oscillator payloads or
  only extend the existing envelope payload with optional second-lane data.

Keep this as a new experiment. Do not promote it over stable production until
hardware tests confirm CPU headroom, envelope timing, Web MIDI compatibility,
and audio behaviour.

## Flash Size And Overclock Headroom

If future experiments move beyond dual pitch envelopes into two fuller
oscillator paths, review processor speed, RAM use, and flash size before
committing to the architecture.

Current measurements:

- Stable build UF2 is about 287 KB.
- Dual-pitch experiment UF2 is about 286 KB.
- Stable ELF reports about 150 KB text/data plus about 9.6 KB BSS.
- Dual-pitch ELF reports about 150 KB text/data plus about 10.1 KB BSS.
- The linker target currently reports 2 MB flash and 256 KB RAM.
- Both stable and dual-pitch builds already define `C1ZZL3_OVERCLOCK_KHZ=192000`.
- The firmware uses `copy_to_ram`, so RAM and CPU headroom are more important
  than raw flash size for the immediate dual-oscillator audio path.

16 MB flash may become useful if a future version adds larger wavetable banks,
patch libraries, more saved card data, sample-like resources, or multiple
firmware assets. It is not currently required for the dual-pitch envelope test.

Before increasing clock speed further:

- Measure audio stability under rapid retriggering, MIDI traffic, and high-PD
  settings.
- Check USB MIDI device and host reliability.
- Check heat/power behaviour on the Workshop Computer card.
- Prefer reducing per-sample work before pushing the clock beyond the already
  tested 192 MHz baseline.

## Sound Slots: Envelope Plus Tone Settings

Custom envelopes currently store envelope shape only. Future CZ-import and
dual-oscillator work may need a higher-level save concept that stores the
envelope together with the tone settings needed to recreate the chosen sound.

Frame this as a future `Sound Slot` or `Patch Slot`, not a replacement for the
current envelope-only custom slots.

Candidate data to save with a sound slot:

- amplitude envelope
- phase-distortion envelope
- pitch envelope, or pitch envelopes if dual-pitch remains useful
- PD amount
- oscillator 2 detune
- wave family
- ring modulation amount
- noise/grit amount

Data that probably should remain global card setup rather than sound design:

- MIDI input channel
- Turing MIDI output enable
- Turing MIDI output channel
- Turing CV octave range, unless a future sound explicitly needs it

Possible UI labels:

- `Save Envelope`: stores shape only.
- `Save Sound`: stores envelope plus tone settings.
- `Load Sound`: loads both envelope and tone settings.

Implementation notes:

- This needs a firmware storage/protocol change.
- Keep envelope-only save for users who want to reuse a shape with different
  performance settings.
- Make volatility clear: loading a sound should be temporary until explicitly
  saved, just like the current envelope/settings workflow.
- Avoid silently changing global MIDI/Turing setup when importing a CZ patch.

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
