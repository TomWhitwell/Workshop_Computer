# Attribution

ScaryLingo firmware and documentation are Copyright 2026 Adrian Vos and are released under the MIT License.

## Included Code

- `ComputerCard.h` is ComputerCard version 0.3.0 by Chris Johnson, copied from `releases/107_scintillator/ComputerCard.h`. ComputerCard is the header-only hardware framework used by this card and is MIT licensed.
- `pico_sdk_import.cmake` is the Raspberry Pi Pico SDK import helper. Its original Raspberry Pi (Trading) Ltd. BSD-3-Clause notice is retained in that file.

## Adapted Ideas

- The Q12 fixed-point conventions and cross-modulation approach were informed by `releases/97_alloy/dsp/xmod_algorithms.h`, an MIT-licensed card whose comments credit Mutable Instruments Warps/Parasites behavior. ScaryLingo's saturated ring stage, carrier oscillator, LFO, soft pickup, and control mapping are original implementations.

## Tools

- Firmware and documentation were developed with Codex assistance and hardware tested by Adrian Vos.
