# Developer documentation

Technical reference for firmware structure, voice matrix specification, and SysEx protocol. Operators should use the guides in the parent [docs/](../README.md) folder instead.

| Document | Contents |
|----------|----------|
| [CONTROL_FLOW.md](CONTROL_FLOW.md) | Dual-core architecture, MIDI routing, audio ISR, config flash |
| [VOICE_MATRIX.md](VOICE_MATRIX.md) | Full 11×11 patch grid, column 10 behaviour, legacy engine map |
| [DSP.md](DSP.md) | Fixed-point ranges, 48 kHz hot path, filters, intentional aliasing |

SysEx byte layout: [`../sysex_spec.json`](../sysex_spec.json) at the release root.

Build instructions: [`../README.md`](../README.md).
