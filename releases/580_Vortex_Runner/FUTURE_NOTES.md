# Future Notes

These are working notes for the Vortex Runner draft card. They are not release notes.

## Monophonic Invariant

Build and retain one monophonic instrument. The first success criterion is
stable hardware behaviour under gate retriggering, MIDI input, Web MIDI
editing, and high modulation depth.

Shared across both outputs:

- pitch and pitch source
- gate/trigger behaviour
- amp and filter envelopes
- portamento
- pitch-CV reference and CV routing
- MIDI note, bend, and CC handling
- LFO timing and destinations
- preset and startup-slot state

Voice A/B differences are limited to oscillator/filter colour and detune.
Do not turn Voice B into an independently gated, enveloped, modulated, or MIDI
played voice. Add an A/B Web UI control only with a corresponding firmware
parameter.

## Architecture

Use card 84 and card 101 as references, but tighten the core split from the
start.

- Core 0: audio rendering only
- Core 1: USB MIDI, Web MIDI SysEx, editor protocol, control aggregation, LED
  planning if practical
- Inter-core handoff: bounded lock-free event queue or double-buffered parameter
  snapshot
- Audio loop: no MIDI parsing, no flash writes, no blocking waits, no dynamic
  allocation, no divisions in the per-sample path

## Build Settings

Target the tested complex-card baseline:

- `C1ZZL3_OVERCLOCK_KHZ=192000` equivalent for this card
- `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`
- `pico_set_binary_type(target copy_to_ram)`
- `pico_enable_stdio_usb(target 0)`
- `-Wdouble-promotion`
- `-Wfloat-conversion`
- `-Wl,--print-memory-usage`

## DSP Notes

Prefer fixed-point integer DSP.

Candidate first voice:

- phase-accumulator oscillators
- saw and pulse generated cheaply from phase
- table or hardware-interpolator support only where it saves cycles
- one-pole or state-variable-inspired high-pass and low-pass stages
- precomputed envelope increments
- precomputed pitch/filter mapping tables
- simple saturation/drive only after timing headroom is known

## Second Oscillator Options

If this card grows beyond the current mono draft, prefer a stronger single-voice
architecture over note polyphony. The next step should be a monophonic voice
with a second oscillator/layer path, not a MIDI-only duophonic split that makes
CV and MIDI behave like different instruments.

Current constraints in the draft firmware:

- one `VoiceState`
- one cached pitch path
- one gate/envelope path
- one active MIDI note
- both audio outputs currently carry the same mono render

Realistic next routes:

### 1. Split-output oscillator B

One played note, with a second oscillator lane exposed on the second output.

- `Audio Out 1`: main voice / oscillator A path
- `Audio Out 2`: oscillator B or alternate layer of the same note
- one pitch source, one note, one playing model
- oscillator B can be detuned, interval-shifted, balanced, or shaped
  differently
- simplest route toward a more CS-80-like voice structure without confusing the
  control model

Pros:

- keeps CV and MIDI behaviour consistent
- closer to the real CS-80 idea than note duophony
- easy to debug by ear with separate outputs
- maps naturally to the Workshop Computer output layout
- useful even before internal mixing is finalised

Cons:

- panel and web control ownership between oscillator A and B must be designed
  explicitly
- per-layer filter/envelope duplication still costs CPU if taken far
- if the two outputs are not later mixable in firmware, external mixing may be
  needed for some patches

### 2. Internally layered mono voice

One played note, with oscillator A and B combined internally.

- separate oscillator lanes inside one voice
- optional balance, detune, interval, or level relationship between them
- summed output to `Audio Out 1`, with `Audio Out 2` available as duplicate,
  dry tap, or alternate filter tap

Pros:

- closest to a real “single rich CS-80-style note”
- no mismatch between CV and MIDI playing models
- keeps the card musically self-contained without external mixing
- can still preserve a debug tap on output 2

Cons:

- higher clipping/headroom management burden
- more expensive than split-output development
- harder to debug early than exposing oscillator B separately first

### 3. Hybrid development path

Start with oscillator B on output 2, then add an internal layered mode later.

- build oscillator B as a real second lane of the same note
- expose it first on `Audio Out 2`
- add internal balance or mix control only after timing and headroom are known

Pros:

- safest development path
- preserves a clear monophonic playing model
- makes it easy to hear what oscillator B is contributing
- leaves room for later internal mixing or alternate output roles

Cons:

- takes longer to reach the most polished internally layered result
- still needs a later decision on how much of oscillator B becomes shared,
  mixed, or separately routable

### Recommendation

Best next experiment: option 3.

In practice that means:

1. keep one monophonic played note for both CV and MIDI
2. duplicate only the oscillator lane, not the note allocator
3. route oscillator A to `Audio Out 1`
4. route oscillator B or its alternate tap to `Audio Out 2`
5. add detune, interval, level, or balance control for oscillator B
6. decide later whether oscillator B should gain independent filtering or
   envelopes
7. only add internal mixing after profiling and listening tests

### Preferred operating model

For this card and this hardware, formalise the preferred expansion as:

- one monophonic playing model
- same note behaviour from CV and MIDI
- `Audio Out 1` as the main voice output
- `Audio Out 2` as oscillator B, alternate layer, or debug tap of the same note

Do not prioritise a MIDI-only duophonic mode, because it makes CV and MIDI feel
like different instruments and moves the card away from the CS-80-style goal of
making one note richer.

### Control strategy questions for later

Before implementing oscillator B, answer these explicitly:

- Is oscillator B simply detuned, or can it also take a fixed interval?
- Are filter controls shared by both oscillator lanes, or does output 2 become a
  timbral variation of output 1?
- Should saved patches remain global, or eventually store oscillator-B-specific
  offsets?
- Is ring modulation shared, per lane, or only meaningful after internal
  layering?
- Should `Audio Out 2` stay a raw alternate lane forever, or later become an
  optional mixed/dry/alternate tap?

## Web Editor Notes

Borrow the C1ZZL3 Web MIDI habits:

- versioned SysEx commands
- separate Apply and Save actions
- settings readback
- save verification delay
- clamped firmware-side parameter parsing
- compatibility fields reserved from the beginning

Do not create large preset payloads until the firmware parameter model is
settled.

## External Control Plan

Long-term control expansion should assume two separate roles:

- compact MIDI controller for many performance parameters
- mono pitch-CV source for playing notes from the modular/keyboard side

Preferred future setup:

- use an `8mu`-style controller for expanded parameter control over MIDI CC
- use something like an Arturia Keystep for mono pitch CV and gate note entry
- keep the card's pitch-CV model mono even if oscillator B is added later

As of August 27, 2026, the current public `8mu` page describes the existing
8mu as an eight-fader MIDI controller with buttons, banks, gesture-derived
controls, USB-C, hardware MIDI out, and a web editor, and it explicitly notes
that the current hardware cannot send i2c or CV due to size constraints. That
means:

- MIDI CC mapping is the correct first integration step now
- do not plan around 8mu CV output on current hardware
- revisit deeper 8mu-led expanded control when the new 8mu version actually
  exists and its capabilities are known

Working assumption for later:

- parameter expansion via 8mu-family MIDI control
- note playing via mono pitch CV/gate from an external keyboard or controller

## Startup Preset Selection Follow-Up

After the Web MIDI save/load/readback path is fixed and promoted, debug manual
startup preset selection. Current observed issue: holding the switch down while
restarting the card does not enter manual saved-preset selection, even when
slots are saved and visible in the web UI.
