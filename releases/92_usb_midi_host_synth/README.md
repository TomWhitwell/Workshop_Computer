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
