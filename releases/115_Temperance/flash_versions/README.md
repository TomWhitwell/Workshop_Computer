# Temperance flash versions

Both versions detect supported 2MB and 16MB Winbond cards automatically.
Each folder includes its own source, CMake build, tests, licences and UF2.

| Version | Source and build instructions | Firmware |
| --- | --- | --- |
| 0.1.1 — triggers only | [flash_trigger](flash_trigger/README.md) | [temperance0.1.1.flash.uf2](flash_trigger/UF2/temperance0.1.1.flash.uf2) |
| 0.2.0 — switchable triggers and gates | [flash](flash/README.md) | [temperance0.2.0.flash.uf2](flash/UF2/temperance0.2.0.flash.uf2) |

The user reported functional hardware tests passing on both sizes for 0.1.1
on 2026-11-11 and for 0.2.0 on 2026-11-15. Detailed reports accompany each
source tree. These reports do not include direct interrupt timing measurements.

The separate upstream RAM versions remain in [../UF2/](../UF2/), with their
source in [../src/](../src/). All four firmware downloads are curated by the
card's [info.yaml](../info.yaml).

The flash derivatives use GPL-3.0 overall; each tree includes attribution and
upstream licence notices. The original RAM release retains its MIT licence.
