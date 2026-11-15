# Credits

Blocks is MIT-licensed (see `LICENSE`). It is built on, or adapted from, the work below. Vendored files keep
their original licence headers.

| Used | By | Licence | How Blocks uses it |
|---|---|---|---|
| [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer) | Chris Johnson | MIT | vendored `firmware/vendor/ComputerCard.h`: jacks, knobs, LEDs, 48 kHz audio loop, calibrated CV |
| [usb_midi_host](https://github.com/rppicomidi/usb_midi_host) | rppicomidi | MIT | vendored `firmware/vendor/usb_midi_host*`: USB MIDI host driver for the Launchpad |
| [TinyUSB](https://github.com/hathach/tinyusb) | Ha Thach (tinyusb.org) | MIT | USB host and device stack, via the Pico SDK; `firmware/tusb_config.h` follows its template |
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | Raspberry Pi Ltd | BSD-3-Clause | build dependency: RP2040 runtime, multicore, flash, `pico_sdk_import.cmake` |
| drumdrum (Workshop Computer card 33) | Moses Hoyt | MIT | the USB-host-on-core-1 structure Blocks' `usb_core1.cpp` was forked from |
| Simple MIDI (Workshop Computer card 00) | Tom Whitwell, Chris Johnson | MIT | USB device descriptors (`firmware/vendor/usb_descriptors.c`), output mapping, start-up delay, copy_to_ram build |
| ComputerCard `web_interface` example | Music Thing Workshop Computer repo | MIT | the WebMIDI + SysEx pattern behind `firmware/src/editor.cpp` and `web/index.html` |
| [mido](https://github.com/mido/mido) | mido contributors | MIT | Python MIDI library, used only by the throwaway `prototype/lp_proto.py` |

Also used, not shipped: Arm GNU Toolchain, CMake, Ninja, picotool (Raspberry Pi, BSD-3-Clause) and Node.js
(codec test). Launchpad protocol facts come from Novation's *Launchpad Mini MK3 Programmer's Reference Manual*.
Built for the [Music Thing Modular Workshop System](https://www.musicthing.co.uk/workshopsystem/).

## Copyright notices

Blocks is Copyright (c) 2026 rafski, MIT (see `LICENSE`). The MIT and BSD licences require these notices to stay with copies.

- ComputerCard: Copyright (c) 2024-2026 Chris Johnson, MIT.
- drumdrum (card 33): Copyright (c) Moses Hoyt, MIT.
- Simple MIDI (card 00): Copyright (c) Tom Whitwell and Chris Johnson, MIT.
- usb_midi_host: Copyright (c) rppicomidi, MIT (its source files keep their own headers).
- TinyUSB: Copyright (c) Ha Thach (tinyusb.org), MIT.

### Raspberry Pi Pico SDK (included in the firmware binary)

Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
