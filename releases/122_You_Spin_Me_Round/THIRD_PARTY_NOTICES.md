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

## Complete notices for source and firmware distribution

Include this file, LICENSE and licenses/TOOLCHAIN_NOTICES.txt with firmware
distributions. NOTICE.txt beside each UF2 consolidates these materials.
Use license_tools/package_uf2.py from the releases directory to make a ZIP
containing the UF2 and its notices. Existing UF2 SDK/toolchain versions
have not been reconstructed; do not treat CMake defaults as build records.

### ComputerCard — Chris Johnson (MIT)

Source: https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard

This notice covers the included ComputerCard hardware framework and
adaptations of it. Older headers retain their version and author credit.

MIT License

Copyright (c) 2024-6 Chris Johnson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### Raspberry Pi Pico SDK (BSD-3-Clause)

Source: https://github.com/raspberrypi/pico-sdk

Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### Pico SDK printf component (MIT)

The MIT License (MIT)

Copyright (c) 2014 Marco Paland

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
