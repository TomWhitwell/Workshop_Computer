# Sources and attribution

Original project code, documentation and synthetic preview audio: Adrian Vos (soveda), 2026, MIT; see LICENSE.

## ComputerCard 0.4.0

ComputerCard by Chris Johnson, copyright 2024–2026, MIT.
The unmodified header is in vendor/ComputerCard/ComputerCard.h and the upstream
license file is preserved beside it. The header also includes the full MIT notice.
The CMake setup follows ComputerCard's example build conventions; the initial
hardware scaffold followed its passthrough example and README (Chris Johnson).
The integer phase-accumulator/lookup-table approach follows Chris Johnson's
sine_wave_lookup example; the sine data is generated from the mathematical function.
The rotary DSP and controls are original code, not a port of commercial Leslie DSP.
pico_sdk_import.cmake is copied from the same upstream directory; the Raspberry Pi
copyright and BSD-3-Clause notice are preserved in vendor/PicoSDK/LICENSE.TXT.

Source: https://github.com/TomWhitwell/Workshop_Computer/tree/7e2b0416093b397b5512de3f111472bd91084c6e/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard

## Workshop Computer documentation

The development directive is an unmodified reference copy from the Music Thing
Modular Workshop Computer repository, revision 7e2b0416093b397b5512de3f111472bd91084c6e.
Its source does not include a separate author or license notice; this project's
MIT license does not relicense that upstream document. Consult upstream for reuse.
Platform and metadata guidance is credited to the Workshop Computer maintainers,
including Tom Whitwell and Chris Johnson.

Source: https://github.com/TomWhitwell/Workshop_Computer/blob/7e2b0416093b397b5512de3f111472bd91084c6e/Demonstrations%2BHelloWorlds/AI/WORKSHOP_COMPUTER_AI_DIRECTIVE.md
Metadata: https://github.com/TomWhitwell/Workshop_Computer/blob/7e2b0416093b397b5512de3f111472bd91084c6e/documentation/info.yaml.md

## Pico SDK

Builds use an externally installed Raspberry Pi Pico SDK. Its components retain
their own licenses. Preserve applicable SDK and dependency notices when distributing
firmware. The SDK itself is not vendored in this repository.

## Release documentation style

The README organization and metadata presentation follow Adrian Vos's Bib
(card 818) and Dub Warning (card 999) in Workshop_Computer. Descriptions and
control instructions here are written specifically for You spin me round.
Reference files: releases/818_Bibesque/{README.md,info.yaml} and
releases/999_Dub_Warning/{README.md,info.yaml}.

Card 122 references the Leslie 122 speaker; source:
https://hammondorganco.com/products/leslie-speakers/122-147-981-147a
No Leslie firmware, manuals, graphics or measured cabinet data are included.
