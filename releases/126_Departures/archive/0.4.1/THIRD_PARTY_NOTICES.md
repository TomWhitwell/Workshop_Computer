# Attribution and dependency inventory

Applies to the stable Departures 0.4.1 source and firmware. Original notices
for previous versions are preserved with their archived bundles.

The card application is original, AI-assisted work by Adrian Vos, licensed under
MIT. [LICENSE](LICENSE) covers that application, not the independently licensed
dependencies listed below. [NOTICE.txt](NOTICE.txt) consolidates the license texts
that accompany source and firmware redistribution.

| Component | Use in Departures | Attribution and license |
| --- | --- | --- |
| ComputerCard 0.4.0 | Unmodified hardware driver in the firmware | Chris Johnson, copyright 2024–2026; MIT. Header and full license in `vendor/ComputerCard/`. |
| Equanimity timing helpers | Adapted ARM cycle model and field-offset helper in `test/bench.py` and `test/bench_layout.cpp`; not in normal firmware | Matt Allison, copyright 2026; MIT. Full license in `licenses/EQUANIMITY_LICENSE.txt`. |
| Pico SDK 2.3.0 | External firmware build dependency; SDK import helper included | Raspberry Pi and contributors; BSD-3-Clause core license in `licenses/PICO_SDK_LICENSE.txt`. Individual SDK components retain their notices. |
| Pico SDK printf | SDK build component derived from printf | Marco Paland, copyright 2014; MIT. Full license in `licenses/PICO_PRINTF_LICENSE.txt`. |
| ARM GNU Toolchain / GCC runtime / Newlib | Compiler and linked runtime/math support | Respective contributors; full collected runtime notices and GCC runtime exception in `licenses/TOOLCHAIN_NOTICES.txt`. |
| Unicorn / Capstone | Optional external host dependencies for the timing estimate; neither is bundled or linked into the UF2 | Respective projects and their licenses; install separately if running the ARM benchmark. |

The inventory names the versions used for this release where known. The collected
toolchain notices preserve existing upstream/runtime notices; the runtime exception
does not change the application's MIT license.

## Musical inspiration and development references

- **AMB-1 by Charles Vestal:** inspiration for the ambient ensemble concept,
  independent loops, breathing layers and slowly evolving harmony. No AMB-1
  source, firmware, presets, samples, fonts, artwork or user interface is included.
  Its MPL-2.0 license therefore is not presented as the license of Departures.
  [Source project](https://github.com/charlesvestal/fm1-amb).
- **Brian Eno's generative music:** the musical background to unequal loops and
  an evolving ambient composition, acknowledged through the AMB-1 inspiration.
  No recordings or musical samples are included.
- **Equanimity by Matt Allison:** a local reference for the ambient-card build,
  fixed-point synthesis approach and 192MHz hardware setup, in addition to the
  explicitly adapted host timing helpers above.
  [Source card](https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/112_Equanimity).
- **Chris Johnson's ComputerCard examples and the existing repository cards:**
  build conventions, RAM execution, hardware I/O and timing instrumentation.
  [Framework](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard).
- **Tom Whitwell, Music Thing Modular and Workshop Computer contributors:**
  hardware platform, repository, documentation and catalogue metadata protocol.

ComputerCard was copied unchanged from the local `122_You_Spin_Me_Round` vendor
directory; its header matches the local Equanimity copy. Sine and pitch tables
are generated mathematically at boot. All sound is synthesized; no third-party
sample library is distributed. The echo and reverb are independently written
fixed-point delay structures, not copied AMB-1 or Dattorro source.

## Redistribution

Keep `NOTICE.txt`, `LICENSE` and the `licenses/` and `vendor/` notices with source
distributions. Keep `uf2/NOTICE.txt` beside the UF2 in firmware distributions.
No external service, sample download or proprietary sound bank is required.
