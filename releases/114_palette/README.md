# Palette

An experiment for a larger card: a simple synth voice where **every** parameter is
read from a Turing-Machine-style shift register.

The register is 64 bits long. In the [web editor](web/index.html) you split it into
1–8 loops (one 64-bit loop, four 16-bit loops, 20 + 12 + 16 + 16, anything that adds up
to 64 or less). Each loop rotates on its own clock divider, and each has a chaos amount
that scales how often the Main knob flips its bits. Then you drag each of the 18
synthesis parameters onto any window of 1–16 bits and give it an output range.
Windows may overlap, and may straddle two loops.

## Quick start

1. Insert the card and press reset. It starts with a four-loop layout, so it plays straight away.
2. Patch **Audio Out 1** to a mixer or speaker.
3. Turn **Y** to set the clock speed, or patch a clock into **Pulse In 1**.
4. Main at noon gives an ever-changing pattern. Turn Main fully clockwise, or push the switch up, to lock the loops.
5. Open the web editor in Chrome or Edge, connect with a USB-C data cable, and start dragging parameters around.

## The voice

Two oscillators (morphing sine → triangle → saw → square) → mix → resonant 12 dB low-pass
(state-variable, run at 96 kHz) → VCA. Osc 2 can frequency-modulate Osc 1 (through-zero FM). Two attack/decay envelopes, one for the filter
and one for volume, each with a curve control.

| # | Parameter | Range | Notes |
|---|---|---|---|
| 0 | Env Trigger | 0–1023 | Fires both envelopes when ≥ 512. Checked only on clock steps where its own bits moved. Unassigned, it fires every step if Min ≥ 512. |
| 1 | Osc 1 Pitch | 0–48 scale degrees | Quantised to the editor's scale, counted up from the root note |
| 2 | Osc 2 Pitch | −24…+24 scale degrees | Relative to Osc 1, same scale |
| 3 | Slew | 0–1023 | Glide, off to about 1 s |
| 4 | Osc 1 Shape | 0–1023 | sine → tri → saw → square |
| 5 | Osc 2 Shape | 0–1023 | sine → tri → saw → square |
| 6 | Osc Mix | 0–1023 | Osc 1 → Osc 2 |
| 7 | Cutoff | 0–1023 | 20 Hz – 14.5 kHz, exponential |
| 8 | Resonance | 0–1023 | Self-oscillates near the top |
| 9 | Cutoff Env Amt | −1023…+1023 | Bipolar |
| 10–12 | Filter Attack / Decay / Shape | 0–1023 | 1 ms – 4 s; shape: slow start ↔ linear ↔ snappy |
| 13–15 | Vol Attack / Decay / Shape | 0–1023 | as above |
| 16 | FM On | 0–1 | Osc 2 modulates Osc 1's frequency when 1. The depth glides in and out over ~3 ms, so switching doesn't click |
| 17 | FM Amount | 0–1023 | FM index 0–4 (256 = index 1). Linear through-zero FM: above index 1 Osc 1's frequency swings through zero and its phase runs backwards, so the pitch stays centred at any depth. Osc 2's pitch and shape set the ratio and colour |

### How bits become values

For each assigned parameter, the bits `start … start+width−1` are read with `start` as the
least significant bit, either

* **Binary**: as a binary number, normalised to 0…1, or
* **Count**: the number of set bits divided by the width (smoother, more "probabilistic" movement),

optionally **inverted** (1 − x), then scaled to `Min + x × (Max − Min)`. Setting Min above
Max also reverses the direction. An unassigned parameter holds its Min value.

### How the loops move

On each clock step, every loop whose divider is due shifts its bits up by one place.
The bit falling off the top of the loop goes back in at the bottom, and is flipped with
probability `p × chaos`. `p` comes from the Main knob (plus CV In 2), as on the Turing Machine:

| Main | p | Result |
|---|---|---|
| fully CW (or switch up) | 0 | locked loop |
| noon | 0.5 | random |
| fully CCW | 1 | locked, inverting every pass, so it repeats at twice the loop length |

Bits outside every loop never move. Assignments can still read them, as fixed values.

## Panel

| Control | Function |
|---|---|
| Main | Randomness (see above) |
| X | Transpose 0 to +24 semitones |
| Y | Internal clock, about 1–16 steps/s (used when Pulse In 1 is unplugged) |
| Switch up | Lock |
| Switch middle | Run |
| Switch down | Scramble every bit inside a loop |
| Pulse In 1 | Clock |
| Pulse In 2 | Fire the envelopes |
| CV In 1 | 1V/oct pitch, semitone quantised |
| CV In 2 | Added to Main |
| Audio Out 1 | Voice |
| Audio Out 2 | Filter envelope |
| CV Out 1 | Osc 1 pitch, calibrated 1V/oct, MIDI note 60 = 0 V |
| CV Out 2 | Volume envelope |
| Pulse Out 1 | Envelope trigger |
| Pulse Out 2 | Clock |
| LEDs 0–3 | Register bits 0–3 |
| LED 4 | Clock |
| LED 5 | Volume envelope; blinks fast while a knob is waiting to pick up |

## Web editor

`web/index.html` is a single static page, styled and connected the same way as the CVSeq and
WaveSeq editors. It works without a card: it simulates the register so you can design layouts
and watch the parameter values move. With a card connected over USB (WebMIDI SysEx, Chrome/Edge
on desktop or Android; not iOS) the card is the source of truth: the page reads the card's
layout when it connects, then shows the card's register, knobs and switch about 30 times a second.

* **Knobs & clock** shows Main, X and Y as the card is using them, and the switch position.
  Move a slider to set that value on the card: the module's knob then **picks up** (soft
  takeover). It does nothing until it is turned to, or past, the new value, so nothing jumps.
  While a knob is waiting, its box has a dotted outline, an orange line shows where the knob
  is, and LED 5 on the module blinks fast.
* Drag a parameter chip from the tray onto the grid to assign it. Drag a bar to move it, drag
  its ends to resize, drag it back to the tray to unassign. Drag on an empty lane to draw a range.
* Drag the handles on the loop strip to move loop boundaries. The Loops table sets
  length, clock divider and chaos per loop, and can split, merge or remove loops.
* Click a state bit to flip it (on the card too, when connected).
* Every edit goes to a connected card straight away. **Save to card** stores the layout and
  the current register pattern in flash so they come back after power-off.
* Layouts can be exported and imported as JSON.

## SysEx protocol

Documented in full in [`sysex.h`](sysex.h). Every message is `F0 7D 50 <cmd> … F7`
(`50` is 'P'); the card shows up as a MIDI device named **Palette**.

| Cmd | Direction | Data |
|---|---|---|
| `01` HELLO | editor → card | card replies with STATE |
| `02` STATE | card → editor | protocol version, layout (173 bytes as nibbles), Main/X/Y settings |
| `03` SET_CONFIG | editor → card | protocol version, layout (173 bytes as nibbles) |
| `04` SAVE | editor → card | save layout and register to flash; card replies SAVED (`08`) |
| `05` SET_REG | editor → card | the register, 8 bytes little endian, as nibbles |
| `06` RESET | editor → card | factory layout; card replies with STATE |
| `07` STATUS | card → editor | ~30/s while pings arrive: register, step and trigger counters, switch / external clock / knob-waiting flags, Main/X/Y settings, physical knob positions, flip probability, the two notes |
| `09` PING | editor → card | every second; keeps STATUS flowing |
| `0A` KNOB | editor → card | knob (0 Main, 1 X, 2 Y), value 0–4095 as two 7-bit bytes; the knob then picks up |

Layout byte layout (`CONFIG_FORMAT` 2): `[0]` format, `[1]` number of loops,
`[2..25]` 8 × (length, divider, chaos), `[26..27]` scale mask (bit n = n semitones above root),
`[28]` root MIDI note, `[29..172]` 18 × (enabled, start, width, flags, min lo, min hi, max lo, max hi)
with flags bit 0 = count mode, bit 1 = invert, and min/max as signed 16-bit little endian.

## Building

```sh
mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk
make
```

Builds against Pico SDK 2.1.1.

`ProcessSample` must finish inside ~20 µs (about 3000 cycles at 144 MHz) on every sample, or
the Computer's knob and CV readings get mixed up. So no sample does a burst of work: after a
clock step the loops shift one per sample and the parameters are re-read one per sample; a
layout from the editor is prepared on the USB core and swapped in by pointer; pitch, smoothing,
knobs, LEDs and the editor snapshot each run on their own staggered samples. Measured in an
emulated Cortex-M (QEMU, instruction counts, hardware divider modelled) the worst sample is
about 1,500 instructions and the average about 1,000, with FM at full depth. It has not yet
been timed on hardware;
if you change the audio path, check it with a scope on a spare GPIO.

## Credits

Firmware and editor written with AI assistance (Claude). The USB/SysEx link, knob pick-up
and editor style follow the CVSeq and WaveSeq cards. MIT licence.
