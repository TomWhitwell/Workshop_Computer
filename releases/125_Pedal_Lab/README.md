# Pedal Lab

Stereo effects for Workshop Computer, using public Spin FV-1 algorithms.

Choose effects in the [editor](https://computer.musicthing.co.uk/programs/125-pedal-lab/web/), edit their code, listen, then build and install a UF2. Everything runs in your browser. The first preview or custom build downloads the compiler; later builds reuse it.

The supplied firmware starts with six Keith Barr reverbs.

| Control | Function |
|---|---|
| Main, X, Y | The selected effect’s three parameters |
| Tap Z down | Next effect |
| Hold Z down + Main | Change the effect clock |
| CV 1, CV 2 | Modulate Main and X |
| Z up + CV 2 | Modulate the clock |
| Pulse 1 | Raise Y by half its range |

Audio 1 feeds both channels when Audio 2 is unplugged. LEDs show the selected effect; LED 5 also indicates clock limiting. Heavy effects run at a lower clock, which changes their sound.

Use Chrome or Edge for USB installation, or download the UF2 and copy it to RPI-RP2.

Author credits and individual reuse terms are retained with each algorithm. See [notices](THIRD_PARTY_NOTICES.md).
