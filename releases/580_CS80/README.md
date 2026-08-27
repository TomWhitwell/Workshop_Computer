# CS80

CS80 is a draft Workshop Computer card for a CS-80-inspired mono synthesiser
voice. The name is a working title and should change before release.

The first target is intentionally modest: make one expressive mono voice sound
good, keep the audio loop lean, and leave the code shaped so a second voice can
be added later.

This card is not affiliated with Yamaha and is not intended to be a circuit
emulation of the Yamaha CS-80. The goal is a playable Workshop Computer
instrument inspired by the broad musical gestures of that synthesiser: two
oscillator lanes, animated pulse/saw tone, high-pass into low-pass filtering,
performance modulation, ring modulation, and expressive envelopes.

## Current Status

This folder contains the current stable mono firmware baseline plus a draft Web
MIDI editor.

- Firmware: current stable version is the non-dual mono voice build with the
  two-bank MIDI CC layout and `CC1` vibrato depth
- Web editor: draft CS80 SysEx v5 patch apply/readback interface
- UF2: the rollback-stable firmware build is kept in `uf2/`
- Release status: draft

Release firmware should go in `uf2/` when a build is ready to publish. Local
hardware-test UF2s should go in `test-uf2/`, which is ignored by git.

## Design Target

- RP2040 clock: 192 MHz
- Firmware should use `pico_set_binary_type(... copy_to_ram)`
- Audio rendering should stay on core 0
- USB MIDI, Web MIDI SysEx, and editor communication run on core 1
- The audio loop should avoid division, float-heavy code, blocking calls, flash
  writes, and MIDI parsing
- Complete Web MIDI patches cross from core 1 to core 0 through a bounded,
  lockless SPSC queue; core 0 publishes readback snapshots through a second queue
- The first voice should be mono, with structures laid out so a second voice can
  be added after profiling

## Planned Mono Voice

- two oscillator lanes
- web-first portamento for smooth CV/pitch glides
- independent saw, square/pulse, sine, and noise mixer levels
- independent pulse width, PWM amount, LFO-to-pitch, and LFO-to-PWM controls
- oscillator spread or detune
- high-pass then low-pass filter character
- amp envelope
- filter envelope
- LFO for vibrato, tremolo, or filter movement
- ring modulation as a performance colour
- gate and MIDI note triggering
- Web MIDI editor for patch editing, readback, and saving

## Draft Web MIDI Protocol

The editor and firmware now share a small non-commercial SysEx protocol:

- manufacturer byte: `0x7d`
- card id: `CS80`
- protocol version: `5`
- commands: apply patch, save slot, request current patch, request slot map,
  request slot, slot response, delete slot, and set startup slot

pitch-CV range mode, portamento, pulse width, PWM amount, independent saw/pulse/sine/noise source levels, voice
level, performance pitch offset, HP cutoff, LP cutoff, resonance, expression
depth, ADSR envelope controls, LFO rate, independent LFO-to-pitch/PWM/VCF/VCA
depths, ring amount, and ring speed. The source levels are summed rather than
crossfaded. Save applies the patch and writes it to one of eight persistent card
slots. The first saved patch becomes the startup patch automatically; use the
editor's `Start Here` button to choose a different saved startup slot.

At startup, holding the Down switch during the short boot selection window enters
patch-slot select. Main chooses among saved slots, LEDs show the selected slot
number, and releasing Down loads the slot and stores it as the default startup
slot.

The pitch-CV range is part of each saved patch, so the startup slot also restores
its `-3 V to +3 V, C4 at 0 V` or `0 V to +5 V, C4 at +3 V` pitch reference.
If no saved startup slot exists yet, the compiled fallback patch is the Web
editor's `Doctor Who Theme` preset: an eerie mono lead using sine plus pulse,
resonant filtering, portamento, vibrato, PWM wobble, and a little ring modulation.

## MIDI CC Map

Following the Gnarly habit, CS80 now exposes a simple two-bank encoder layout
for controllers such as the M-VAVE SMK-25:

- `CC1`: vibrato depth via LFO to pitch
- Bank A, `CC20-27`: saw level, pulse level, sine level, noise level, pulse width, PWM amount, LP cutoff, resonance
- Bank B, `CC28-35`: HP cutoff, ring amount, ring speed, attack, decay, sustain, release, portamento

Secondary synth controls stay available on:

- `CC2` or `CC11`: expression depth
- `CC7`: voice level
- `CC76`: LFO rate
- `CC77` or `CC93`: LFO to PWM
- `CC91`: LFO to VCF
- `CC92`: LFO to VCA

Pitch bend is also supported and currently maps to a fixed `+/-2 semitone`
range through the existing performance-pitch path.

MIDI note-on and note-off now drive the mono voice directly. While a MIDI note
is held, it takes over pitch and gate for the synth voice, including
portamento and pitch bend. Releasing the note returns pitch control to the card
CV input and pulse gate behaviour.

## First-Pass Firmware

`CS80.cpp` is a lean starting point rather than a finished synth. It currently
drives one mono voice and copies it to both audio outputs for easy monitoring:

- `Audio Out 1`: mono voice
- `Audio Out 2`: mono copy / future alternate output reserve

Holding Down exposes performance controls for temporary pitch offset, ring
modulation, and LFO depth. The earlier two-output Voice A/B experiment has been
pulled back until the mono implementation is lean and profiled.
When `Pulse In 1` is unpatched the gate is held open for oscillator bring-up;
when it is patched, `Pulse In 1` gates the mono voice.
`Audio/CV In 1` is the pitch input. The Web editor selects either unipolar
0 V to +5 V, with C4 at +3 V (0 V is C1), or bipolar -3 V to +3 V, with C4 at
0 V. Both use 1V/oct. `CV In 1` is a wide filter
modulation input, and `CV In 2` is an expression input that pushes brightness
and resonance.

Panel controls use soft pickup so switching pages does not immediately overwrite
stored values. LEDs 0/2/4 show the stored Main/X/Y values for the current switch
page, while LEDs 1/3/5 go bright once each physical knob has picked up its stored
value.

Web MIDI never writes live audio parameters directly. Core 1 parses each complete
SysEx patch, incoming MIDI CC update, or pitch-bend change and queues a complete
patch without waiting; core 0 applies queued patches only on its 64-sample
control tick. A second, best-effort queue returns core-0 snapshots to the editor
for safe readback. If a burst fills the four-patch input queue, core 1 coalesces
it to the most recent patch rather than blocking either core.

The first pass uses small lookup tables in `CS80_LUT.*` for pitch ratios, filter
curves, envelope rates, and LFO/ring sine values. This follows the C1ZZL3 habit
of keeping expensive shaping out of the 48 kHz audio path.

Build locally with:

```sh
cmake -S . -B build -DPICO_NO_PICOTOOL=1
cmake --build build -j2
```

## References

Cards 84 and 101 are the main local references for the Web MIDI/editor pattern,
192 MHz build setup, copy-to-RAM configuration, dual-core USB MIDI worker, flash
storage, and complex synth-card release structure:

- `releases/84_CosmikC1zzl3`
- `releases/101_Gnarly_C1ZZL3`

## Attribution And License Notes

This draft card scaffold and future CS80 card firmware are by Adrian Vos and
are released under the MIT License. See `LICENSE`.

The included `computercard.h` file is ComputerCard by Chris Johnson, version
0.3.0, also MIT licensed. It is copied from:

```text
Demonstrations+HelloWorlds/PicoSDK/ComputerCard/ComputerCard.h
```

The USB MIDI host helper files `usb_midi_host.*` and
`usb_midi_host_app_driver.c` are copied from the local C1ZZL3 cards and retain
the upstream rppicomidi MIT copyright notices in the source files. The TinyUSB
descriptor/config pattern is adapted from Adrian Vos's C1ZZL3 cards for this
card's Web MIDI device interface.

The included `pico_sdk_import.cmake` file is the standard Raspberry Pi Pico SDK
import helper. It carries the Raspberry Pi (Trading) Ltd. BSD-style notice in
the file itself.
