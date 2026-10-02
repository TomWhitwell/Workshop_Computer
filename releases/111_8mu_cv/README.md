# 8mu CV

*A Music Thing [8mu](https://www.musicthing.co.uk/8mu.html) as four voltages, two pulses, and a motion controller.*

A playable instrument as much as a showcase for the Music Thing Modular
Workshop System Computer. Every control the 8mu has — eight faders, four top
buttons, and the accelerometer — drives the Computer's outputs: four bipolar
voltages and two pulses. Turn the LFOs loose on the voltages and latch in the
accelerometer, and tilting and turning the controller steers pitches, sweeps
and rhythms you didn't quite ask for. That is where the music tends to come
from: it is wonderfully easy to set something in motion and then be surprised
by it. It is also a plain way to test an 8mu channel by channel, and it is
meant to be used **with** the 8mu — unplug it and the outputs hold their last
values.

| 8mu control | Jack | What it does |
|---|---|---|
| **Fader 1** | Audio Out 1 | Voltage, centre = 0V (or LFO rate) |
| **Fader 2** | Audio Out 2 | Voltage, centre = 0V (or LFO rate) |
| **Fader 3** | CV Out 1 | Voltage, centre = 0V (or LFO rate) |
| **Fader 4** | CV Out 2 | Voltage, centre = 0V (or LFO rate) |
| **Fader 5** | Pulse Out 1 | Rate, 0.1Hz to 20Hz |
| **Fader 6** | Pulse Out 2 | Rate, 0.1Hz to 20Hz |
| **Fader 7** | Pulse Out 1 | Width, centre is a 50% square |
| **Fader 8** | Pulse Out 2 | Width, centre is a 50% square |
| **Button 1** (C2) | Audio Out 1 | Arm/disarm its triangle LFO |
| **Button 2** (C3) | Audio Out 2 | Arm/disarm its triangle LFO |
| **Button 3** (C4) | CV Out 1 | Arm/disarm its triangle LFO |
| **Button 4** (C5) | CV Out 2 | Arm/disarm its triangle LFO |
| **Accelerometer** | Audio Out 1–2, CV Out 1–2 | Gesture control (see below) |

The Computer's own knobs are deliberately **not** a fallback: the 8mu is the
source of the eight parameters. The three panel knobs each have a dedicated
job instead — Main sets LFO depth, X sets gesture sensitivity, and Y smooths
the gestures. The switch picks the mode and latches the accelerometer.

The audio outputs are DC-coupled on this hardware, so they carry a steady
voltage just as well as sound does. That is what makes four voltage outputs
possible from two audio jacks and two CV jacks, rather than the usual two.
None of them are calibrated, which does not matter here: this card is a source
of control voltages, not a pitch reference.

## Modes

The Computer's switch picks what the card does:

| Switch | Mode | Behaviour |
|---|---|---|
| **Middle** | Basic | Faders 1-4 are steady voltages, 5-8 pulse rate and width |
| **Up** | LFO | Armed outputs oscillate; un-armed outputs stay steady voltages |
| **Down** (momentary) | — | Held ~half a second, it latches the accelerometer on or off |

Switching back to Middle stops any oscillation, and each output glides to its
captured centre rather than jumping.

## The accelerometer

**Hold the switch Down for about half a second** to latch the accelerometer on
or off (all six LEDs blink to confirm). It is off by default. When it is on,
the four gesture axes take over the four voltage outputs *and* their LFO rates:

| Axis | Output | Feel |
|---|---|---|
| **Pitch** → V1 | tilt, back lifted positive | bipolar, rests at 0V / mid rate |
| **Roll** → V2 | tilt, right lifted positive | bipolar, rests at 0V / mid rate |
| **Yaw** → V3 | spin, clockwise positive | bipolar (a rate of turn), rests at 0V / mid rate |
| **Flip** → V4 | which way up | unipolar: flat = 0V / slowest, upside-down = max / fastest |

In Basic the axis sets a steady voltage. In LFO mode an armed output's axis
sets its **rate** instead, replacing the fader. The **X knob** scales how far
the gestures respond (exponential, ~0.25x–4x), and the **Y knob** smooths them
so a shaky hand does not judder the output. The latch survives the 8mu being
unplugged.

## The voltages

Each of the four voltage outputs follows its fader from about -5V at the
bottom, through 0V in the middle, to about +5V at the top. The voltage is
smoothed very slightly, so the steps in the 8mu's 7-bit faders do not click
into whatever the voltage is controlling.

Use them for offsets, for holding a filter or oscillator at a position, or as
a manual voltage source you can dial in by hand.

## LFOs

Any of the four voltage outputs can be a **triangle LFO** instead of a steady
voltage. A tap on one of the four top buttons **arms** that output; it
oscillates whenever the switch is Up. You can arm in Middle and flip Up to
hear it.

Arming captures the voltage the output is already sitting at as the centre to
oscillate around, and while it is oscillating the fader that set the level
instead sets the **speed** (0.1Hz to 20Hz, exponential, the same feel as the
pulse rates). So the move is: dial in a voltage, tap the button, flip Up, and
it comes alive around that point while your finger takes over the rate.

A fader (or gesture axis) whose output is **not** armed is still a level even
in LFO mode, so the card stays useful with one LFO running or none.

When the accelerometer is latched on, the gesture axis takes the fader's place
as the rate control while an output is armed — tilt the 8mu to speed the LFO up
and down. See "The accelerometer" above.

**Main** sets how far the triangle swings: fully anticlockwise is almost no
swing (a bare trace), fully clockwise is the whole distance to the nearer
supply rail. Because the swing is measured to the rail, it scales itself down
as the centre gets close to one - an LFO can never clip, whatever the centre
and depth are. Main only affects armed outputs.

Disarming an output - by tapping its button again, or by dropping the switch
back to Middle - glides it to its centre. The fader does not grab the level
immediately, because it is sitting at a rate position; it stays locked until
you move it to meet the captured value, so nothing jumps.

**Holding any one of the four buttons for a second and a half disarms all four
at once** - the quick way to stop everything.

Arming survives the 8mu being unplugged; the mode is the switch's, so the card
always has a way back to steady voltages.

## The pulses

Pulse Out 1 and Pulse Out 2 are independent pulse streams. Fader 5 and fader 6
set their rates between 0.1Hz and 20Hz, with an exponential response so that
equal movements of the fader multiply the rate by the same amount. That matches
the way speed is heard: the bottom of the fader is a slow blink, the top is a
fast pulse.

Fader 7 and fader 8 set their **width** - how much of each cycle the output
stays high. The middle of the fader is a 50% square, the way up widens it and
the way down narrows it:

| Fader 7 / 8 | Duty | What it is good for |
|---|---|---|
| Bottom (~2%) | very narrow | a sharp trigger or clock edge |
| Middle (50%) | square | the classic LFO / gate shape |
| Top (~98%) | very wide | a gate that holds a note or envelope open nearly all the time |

The width never quite reaches zero or full, so there is always a pulse being
produced and never a dead or stuck output. Like the voltages, the width is
smoothed slightly, so sweeping the fader does not stretch a single pulse as it
passes.

Width comes from the 8mu alone - there is no other source for the eight
parameters, so with no controller attached both widths hold wherever they
were last left.

Patch them into clocks, triggers, gates, or anything that wants a rhythmic
on/off. At the bottom they are slow enough to use as an LFO; widen the pulses
for trigger duties and narrow-shape them into gate streams.

## Without a controller

Unplug the 8mu and the outputs simply hold their last values - nothing new can
be set, but nothing jumps either. The switch still selects the mode, so an
armed output can still be stopped from the panel. Plug it back in and the
faders pick up where they were.

## Panel LEDs

| LED | Meaning |
|---|---|
| 0 | Lit while an 8mu is connected |
| 1 | Brightness follows the audio-out voltage on fader 1 |
| 2 | Follows Pulse Out 1 |
| 3 | Follows Pulse Out 2 |
| 4 | Lit in LFO mode (switch up) |
| 5 | Brightness follows the CV-out voltage on fader 3 |

All six blink briefly when the accelerometer latch is toggled.

## Requirements

The 8mu is a USB device, so the Computer has to act as a USB host: this needs
a **Rev 1.1 or later** board, and nothing else may be plugged into the
Computer's front USB socket. The card is a pure MIDI listener - it never
writes to the 8mu's LEDs or settings, so the controller behaves exactly as it
does anywhere else.

## Building

```bash
cmake -B build -G Ninja
cmake --build build
```

Drag `build/8mu_cv.uf2` onto the Pico in bootloader mode, or use the binary
committed at `UF2/8mu_cv.uf2`.

## Credits

- **Chris Johnson** — the ComputerCard library and `EightMU.h`, which carries
  the USB MIDI host driver and the TinyUSB host configuration.
- **rppicomidi** — the `usb_midi_host` driver, MIT, vendored inside
  `EightMU.h`.
- **Tom Whitwell / Music Thing Modular** — the Workshop System and the 8mu.

## License

MIT. See `LICENSE`, which records the vendored components and their terms.
