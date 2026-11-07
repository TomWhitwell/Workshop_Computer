# Code attribution and license scope

Temperance 16MB flash experiment, **0.1.0-flash-exp.2**.
Attribution audit: 2026-11-04. This is a derivative of Matt Allison's
Temperance, with flash recording adapted from Dune Desormeaux's work.
It is not an original instrument implementation by Codex.

## Overall experimental licence — GNU GPL version 3

The combined experimental source and firmware are distributed under the
**GNU General Public License, version 3 (GPL-3.0)**; see [LICENSE](LICENSE).
On 2026-11-04, the maintainer reported that Dune Desormeaux, author of Goldfish
and MLRws, suggested GPL-3.0 as the overall licence. This experiment adopts
that licence for the combined derivative and its experimental integration.

Upstream copyright and licence notices remain intact. Matt Allison's original
Temperance release remains MIT-licensed and unchanged; ComputerCard's MIT
and the Pico SDK's BSD notices are retained. GPL-3.0 applies to this combined
experiment without rewriting the licences of the original upstream releases.

## Matt Allison — Temperance 0.1.0

Copyright (c) 2026 Matt Allison. Original code licensed under MIT.

Source: [original Temperance](../../README.md),
[original source](../../src), and [original license](../../LICENSE).
The original release is unchanged.

- `src/Clock.h`, `Dsp.h`, `Hook.h`, `Params.h`, and `Plate.h` retain the
  original implementations, with attribution headers added.
- `src/Temperance.h` and `Ui.h` retain the original audio engine and control
  implementation, adapted for independent flash channels and long positions.
- `src/Looper.h` retains Temperance's looper interface, controls, seam and
  crossfade design, adapted for decoded flash caches and long loops.
- `src/main.cpp` retains the original ComputerCard integration, adapted to
  schedule flash I/O and report storage faults.
- `CMakeLists.txt` is adapted from the original Temperance build definition.

The original README credits Matt's design and states that its code, tests and
documentation were written with support from Claude (Anthropic). That upstream
acknowledgement is preserved here; Matt remains credited as the original creator.

The original MIT notice is preserved verbatim in
`LICENSES/Temperance-MIT.txt`. It applies to the upstream Temperance code.
The experiment's top-level `LICENSE` contains GNU GPL version 3 for the
combined derivative; it does not replace the original release's MIT licence.

## Dune Desormeaux — Goldfish 2.0 and MLRws

Sources: [Goldfish](../../../11_goldfish/README.md),
[Goldfish flash streaming source](../../../11_goldfish/goldfish_stream.c),
[MLRws](../../../15_MLRws/README.md), and
[MLRws release metadata](../../../15_MLRws/info.yaml).

- `src/FlashHardware.inc` adapts Goldfish's raw QSPI transactions, ROM
  flash-mode helpers and page programming implementation.
- `src/FlashStore.cpp` adapts Goldfish's buffered recording, erase-ahead and
  erase-suspend/program/refill/resume approach. It adds independent recording
  sessions, a per-page decoder-state format and a different cache implementation.
- `src/FlashStore.h` provides the experimental interface to that storage layer.
- `vendor/adpcm.h` contains the IMA-ADPCM implementation copied from Goldfish.
  Goldfish explicitly credits it to MLRws. Its codec implementation is unchanged;
  an attribution header has been added.

MLRws identifies **Dune Desormeaux** as creator and **GPL-3.0** as its license.
Its complete license text is preserved verbatim as
`LICENSES/MLRws-GPL-3.0.txt`. The included codec must not be represented as
Matt's MIT-licensed code or as newly authored code.

The upstream Goldfish release in this repository has no explicit licence
file or licence field. For this derivative, the maintainer has reported
Dune's recommendation to use GPL-3.0 overall, as recorded above. Goldfish-derived
portions are included in this experiment under that overall licence; this
does not modify the separate Goldfish release or claim that its metadata
has been updated.

## Chris Johnson — ComputerCard

The build includes the unchanged `../../lib/ComputerCard.h`, credited to
Chris Johnson, version 0.4.0. Its header carries an MIT notice with copyright
(c) 2024–2026 Chris Johnson; the upstream standalone license is retained at
`../../lib/ComputerCard.LICENSE` and copied verbatim to
`LICENSES/ComputerCard-MIT.txt`.

ComputerCard supplies the hardware/audio interface, calibration and jack
normalisation probe. These are upstream library facilities, not experimental
flash code.

## Jon Dattorro — plate reverb design

The plate design is credited to Jon Dattorro's “Effect Design, Part 1”, JAES,
1997. `src/Plate.h` retains the original Temperance implementation and its
existing algorithm credit. This credits the design; it does not claim that
Dattorro authored the Temperance source file.

## Raspberry Pi — Pico SDK

The build uses the Raspberry Pi Pico SDK. `pico_sdk_import.cmake` comes from
the SDK via Temperance. `boards/temperance_16mb.h` includes the SDK's Pico board
definition and changes the flash capacity.

The SDK's BSD-3-Clause notice, copyright 2020 Raspberry Pi (Trading) Ltd., is
preserved verbatim in `LICENSES/Pico-SDK-BSD-3-Clause.txt`. SDK components
remain subject to their own upstream notices. The SDK and ARM GNU toolchain
are external build dependencies rather than vendored source in this folder.

## Experimental integration and tests

The flash page format, independent queues, cache integration, long-position
changes, tests and experimental documentation were generated with OpenAI Codex
at the user's direction on 2026-11-04. This describes the modification process;
it does not transfer credit for the upstream source to Codex or imply upstream
authors endorse this derivative.

Keep this attribution document and the bundled `LICENSES/` notices with source
and firmware copies. The UF2 program description also names the upstream
creators so that its provenance remains visible through `picotool info -a`.
