# Wild Pebble

MIDI-enabled Wild Pebble for the Music Thing Modular Workshop Computer.

Wild Pebble is a generative rhythm and melody organism inspired by the spirit of Jonah Senzel's Pet Rock. It creates evolving rhythmic structures, quantised melodic patterns, drum voices, and modulation voltages that slowly transform while staying musically connected.

The goal is not precise repeatability, but constrained musical evolution.

---

# MIDI Additions

Wild Pebble includes USB MIDI device support.

It does not use USB MIDI host mode.

## MIDI Clock Input

Wild Pebble listens for USB MIDI realtime clock messages.

* MIDI Clock `0xF8` drives the sequencer when MIDI transport is running.
* MIDI Start `0xFA` resets the sequence to the beginning and starts clock following.
* MIDI Continue `0xFB` resumes MIDI clock following without resetting the step.
* MIDI Stop `0xFC` stops MIDI clock following and releases any active MIDI note.

MIDI clock is 24 PPQN. Wild Pebble advances one sequencer step every 12 MIDI clock ticks, so incoming MIDI clock drives the sequencer at an 8th-note step feel.

When MIDI clock is active it overrides the internal clock. If Pulse Input 1 is active and running faster than MIDI, the pulse clock takes priority. If no MIDI or pulse clock is active, the Main knob controls the internal clock speed.

## MIDI Note Output

The generated melody is sent over USB MIDI as note events.

* MIDI channel: 1
* Note source: the current generated `currentMIDINote`
* Trigger source: Pulse Output 1
* Velocity: shaped by internal energy and tension
* Note length: shaped by the active clock period, energy, and tension
* Retriggers: become more likely as energy and tension rise

This keeps the MIDI line feeling connected to the card's internal motion instead of acting like a rigid copy of the raw pulse width. The analogue CV and pulse outputs continue to work as in the original card.

---

# Features

* USB MIDI device clock input
* USB MIDI sequencer note output
* Dual probabilistic trigger streams
* Quantised melodic CV generation
* Internal kick and snare percussion voices
* Self-mutating sequence behaviour
* Phrase replication and variation
* Slowly evolving scale randomisation
* Internal tension modulation
* External pulse, MIDI, or internal clocking
* Swing timing modes for the internal clock
* Low CPU usage

---

# Outputs

## Pulse Output 1

Primary rhythm stream.

Used internally to drive:

* melodic progression
* kick drum voice
* USB MIDI note gate

## Pulse Output 2

Derived companion rhythm stream.

Used internally to:

* trigger the snare voice
* create correlated rhythmic variation

## CV Output 1

Quantised melodic pitch output.

Generated from evolving scale-constrained note sequences. Wild Pebble slowly changes between related scales over time depending on mutation amount and switch mode, creating harmonic drift without fully losing musical coherence.

## CV Output 2

Energy/tension modulation output.

A smoothed evolving modulation source derived from:

* step energy
* accent
* internal tension state

## Audio Output 1

Kick drum voice driven from the primary trigger stream.

## Audio Output 2

Snare and percussion voice driven from the companion trigger stream.

---

# Controls

## Main Knob

Controls internal clock speed.

Ignored when pulse clock or MIDI clock is active.

## X Knob

Controls rhythmic density.

Higher values increase trigger probability. CV Input 1 modulates density.

## Y Knob

Controls mutation intensity.

Higher values increase:

* sequence mutation
* melodic movement
* scale changes
* structural instability

CV Input 2 modulates mutation amount.

## Switch Modes

### Up

Steady mode. Stable melodic motion, restrained mutation, slower harmonic movement, and tighter rhythms.

### Middle

Drift mode. Balanced mutation, moderate swing, evolving melodic variation, and gradual harmonic drift.

### Down

Surge mode. Aggressive mutation, strongest swing, wider melodic jumps, more active scale changes, and denser companion rhythms.

---

# External Inputs

## Pulse Input 1

External pulse clock input.

Automatically overrides the internal clock while active.

## Pulse Input 2

Freeze input.

While held high:

* mutation is disabled
* current structure is preserved

Clocking and playback continue normally.

---

# LED Behaviour

| LED   | Function     | Behaviour                                           |
| ----- | ------------ | --------------------------------------------------- |
| LED 1 | Main Trigger | Flashes when `PulseOut1` fires                      |
| LED 2 | Density      | Brightness follows Density control (`Knob X + CV1`) |
| LED 3 | Mutation     | Brightness follows Mutation amount (`Knob Y + CV2`) |
| LED 4 | Energy       | Displays smoothed internal energy/modulation level  |
| LED 5 | Clock Source | Fully lit when pulse or MIDI clock is active        |
| LED 6 | Tension      | Displays evolving internal tension state            |

---

# Building

Requires:

* Raspberry Pi Pico SDK
* Workshop Computer `ComputerCard.h`
* TinyUSB from the Pico SDK

Build from this folder:

```bash
mkdir build
cd build
cmake ..
make
```

The build links `pico_multicore`, `tinyusb_device`, and `tinyusb_board`. USB stdio and UART stdio are disabled.

The firmware runs the Workshop Computer audio/card engine on core 1 and the USB MIDI device task on core 0. This keeps TinyUSB calls out of the 48 kHz `ProcessSample()` path.

---

# Notes

This is an AI-assisted card.

The MIDI implementation is intentionally small:

* no SysEx editor
* no USB host mode
* no MIDI note input
* no configurable MIDI channel yet

The analogue behavior remains the same unless MIDI clock is connected and running.
