# USB MIDI Host — documentation

These notes are for operating the card: patching, playing, learning MIDI maps, and using the web editor. They assume you have a Music Thing Modular Workshop System Computer with this program card installed.

If you are modifying firmware, see [developer/](developer/) instead.

## Start here

1. [Getting started](getting-started.md) — flash the card, plug in a keyboard, confirm CV, gate, and audio.
2. [FAQ](faq.md) — common problems and quick answers.

## Features

| Topic | What it covers |
|-------|----------------|
| [Outputs and routing](features/outputs.md) | CV Out 1/2, gates, audio mix, MIDI channels, pitch bend |
| [Voice matrix](features/voice-matrix.md) | The 121 built-in synth patches and how to select them |
| [Envelope and tweaks](features/envelope-and-tweaks.md) | ADSR, cutoff, PWM, and the X/Y knobs |
| [SETUP mode](features/setup-mode.md) | MIDI learn on the hardware panel |
| [Web editor](features/web-editor.md) | Browser setup, relay, Live tab, saving to flash |
| [Drum kit](features/drums.md) | Channel 10 pad mapping |
| [LEDs and status](features/leds-and-status.md) | What the six LEDs mean in PLAY and SETUP |
| [USB host and device](features/usb-host-and-device.md) | Keyboard on the Computer vs laptop on USB-C |

## Firmware version

Current release: **0.10.0**. The web editor and SysEx protocol match this version. If you flash an older UF2, some editor fields may not line up.

## Web editor

Open `web/index.html` in Chrome or Edge on a computer connected to the Workshop System by USB-C. The in-page tutorial covers the same ground as these docs from the editor’s point of view.
