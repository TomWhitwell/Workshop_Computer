# Future Notes

These are working notes for the CS80 draft card. They are not release notes.

## Mono First

Build one mono voice first. The first success criterion is stable hardware
behaviour under gate retriggering, MIDI input, Web MIDI editing, and high
modulation depth.

Do not add a second voice until:

- the mono voice builds cleanly
- the mono voice has been profiled
- the audio interrupt has clear timing headroom
- the Web MIDI protocol has basic read/apply/save working
- the physical controls feel coherent

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

## Duophonic Options

If this card grows beyond mono, keep the next step to two voices and choose the
architecture deliberately rather than drifting into accidental pseudo-polyphony.

Current constraints in the draft firmware:

- one `VoiceState`
- one cached pitch path
- one gate/envelope path
- one active MIDI note
- both audio outputs currently carry the same mono render

Realistic duophonic routes:

### 1. Split-output duophonic

Two independent note lanes, one per output.

- `Audio Out 1`: voice A
- `Audio Out 2`: voice B
- separate pitch, envelope, and oscillator state per voice
- each voice keeps the same mono architecture
- simplest audible proof that two-note MIDI/CV handling works

Pros:

- easiest to debug by ear
- no internal voice mixing cost
- maps naturally to the Workshop Computer output layout
- useful even before a polished summed mode exists

Cons:

- less immediately “synth keyboard” like if the user expects both notes mixed
- filter and envelope duplication still costs CPU
- panel control ownership between voice A, voice B, and shared controls must be
  designed explicitly

### 2. Summed duophonic

Two fully independent voices are rendered, then mixed to one or both outputs.

- separate oscillator, pitch, and envelope state per voice
- per-voice filter path if the sound should stay close to the mono card
- summed output to `Audio Out 1`, optional duplicate or alternate tap on
  `Audio Out 2`

Pros:

- most familiar “two-note synth” behaviour
- keeps the card musically self-contained without requiring external mixing
- can still support later split-output debug modes

Cons:

- higher clipping/headroom management burden
- more expensive than split outputs because both voices are always combined
- harder to isolate per-voice problems during early testing

### 3. Paraphonic two-note mode

Two oscillators or pitch lanes feed a shared filter and shared amp envelope.

- separate pitch generation for two held notes
- shared downstream filter and VCA path
- one articulation envelope for both notes

Pros:

- much cheaper than full dual-voice rendering
- could preserve more CPU headroom for richer oscillators or modulation
- historically plausible for a synth-inspired instrument, even if not true
  CS-80 behaviour

Cons:

- less expressive than true dual voice
- note release behaviour becomes musically tricky
- patch expectations from the current mono envelope logic may not translate
  cleanly

### 4. Hybrid debug-first duophonic

Start as split-output dual voice internally, then add optional summed mode once
timing is proven.

- build voice A and voice B as fully separate render paths
- test them on separate outputs first
- add a selectable summed mode only after CPU and headroom are known

Pros:

- safest development path
- exposes CPU cost early
- keeps audio debugging straightforward

Cons:

- takes longer to reach the most polished end-user mode
- requires an extra routing decision later

### Recommendation

Best next experiment: option 4.

In practice that means:

1. duplicate the current mono voice into `voiceA` and `voiceB`
2. keep one shared patch for now, with only pitch/gate/note allocation per
   voice
3. route `voiceA` to `Audio Out 1` and `voiceB` to `Audio Out 2`
4. add a tiny two-note allocator for MIDI first
5. decide later whether CV should stay mono, become voice A only, or support a
   second pitch source
6. only add summed dual-voice mode after profiling

### Control strategy questions for later

Before implementing duophony, answer these explicitly:

- Are filter controls shared by both voices, or does one output become a
  timbral variation of the other?
- Does `Pulse In 1` gate both voices together, or only the most recent/voice A?
- Should MIDI be note-priority based, round-robin, or last-two-held?
- Should saved patches remain global, or eventually store per-voice offsets?
- Is ring modulation shared, per voice, or only available in summed mode?

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
