# USB MIDI Host

USB MIDI → CV/Gate/Audio for the Music Thing Modular Workshop System Computer, with SETUP + MIDI learn, a 121-patch voice matrix, stroke LED glyphs, and a browser editor.

## Documentation

**[docs/](docs/README.md)** — getting started, feature guides, and FAQ for operating the card.

| | |
|---|---|
| New to the card | [Getting started](docs/getting-started.md) |
| Patch reference | [Voice matrix](docs/features/voice-matrix.md) |
| Panel learn | [SETUP mode](docs/features/setup-mode.md) |
| Browser tool | [Web editor](docs/features/web-editor.md) |
| Problems | [FAQ](docs/faq.md) |

Firmware developers: [docs/developer/](docs/developer/README.md).

## What it does (summary)

| Output | Source |
|--------|--------|
| CV Out 1 + Pulse Out 1 | Voice A (default MIDI ch 1), last-note priority |
| CV Out 2 + Pulse Out 2 | Voice B (default MIDI ch 2) |
| Audio Out 1 / 2 | 4-voice poly — **121 voice matrix** |

- **CV pitch:** MIDI note 60 (C4) = 0 V (Simple MIDI EEPROM cal)
- **Audio engine (CC 24):** 4-voice poly — 121 patches via the voice matrix (CC 0–120)
- **ADSR (slots 7–10):** amp envelope; factory **Attack → X**, **Release → Y**
- **Cutoff (slot 11)** and **PWM (slot 12):** tone controls (see [docs](docs/README.md))
- **Drum pads (MIDI ch 10, notes 36–43):** velocity-sensitive kit on the audio mix

Host **SETUP** learns CC/knob maps on the panel; **device mode** exposes the same maps to `web/index.html`.

## Quick Start

After flashing the card, power everything off, plug in a USB keyboard (ideally one that has CC dials on it (twisty knobs) like the M-VAVE SMK25) into the USB-C port, and switch everything on, starting with your USB keyboard.

Next, we need to set up the voice select, and ADSR controls. Hold the Z switch down for a second or two and you'll see the 6 LEDs flicker, and the bottom two start to alternate. You're now in setup mode.

**Voice**

Twist the main knob until you see the LED pattern look like this:

```
⚫⚫
🔴⚫
FF

F = Flashing
```

Now, twist the CC knob that you want to use to control the voice on your keyboard. You should see all the LEDs flicker. That's just assigned it.

**Attack**

Next, twist the main knob until you see this:

```
🔴🔴
🔴⚫
FF

F = Flashing
```

Now, twist the CC knob that you want to use to control the attack on your keyboard. You should see all the LEDs flicker. That's just assigned it.

**Decay**

Next, twist the main knob until you see this:

```
⚫⚫
⚫🔴
FF

F = Flashing
```

Now, twist the CC knob that you want to use to control the decay on your keyboard. You should see all the LEDs flicker. That's just assigned it.

**Sustain**

Next, twist the main knob until you see this:

```
🔴⚫
⚫🔴
FF

F = Flashing
```

Now, twist the CC knob that you want to use to control the sustain on your keyboard. You should see all the LEDs flicker. That's just assigned it.

**Release**

Next, twist the main knob until you see this:

```
⚫🔴
⚫🔴
FF

F = Flashing
```

Now, twist the CC knob that you want to use to control the release on your keyboard. You should see all the LEDs flicker. That's just assigned it.

**Save Settings**

Flick the Z switch up, wait 2 seconds, and flip it back to the middle. The settings have been saved to the unit, and you're good to go. To make sure there's no weird preset / flash values hanging around, you should turn each knob you've assigned up and down a bit to get some midi flowing, then turn:

- Voice to 0
- Attack to 0
- Decay to 0
- Sustain to 127
- Release to 0

... and now you should get some sound if you mash some keys. Have fun!

## Build

Build inside the [Dev Container](../../.devcontainer/README.md) (recommended):

```bash
cd releases/92_usb_midi_host_synth
make build
```

From the repo root:

```bash
make releases/92_usb_midi_host_synth
```

Flash `UF2/92_usb_midi_host_synth.uf2`.

SysEx protocol: [`sysex_spec.json`](sysex_spec.json). Implementation notes: [`docs/developer/CONTROL_FLOW.md`](docs/developer/CONTROL_FLOW.md).

Firmware **0.10.0**.
