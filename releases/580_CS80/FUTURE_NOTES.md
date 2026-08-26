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
