# Third-Party Notices

## ComputerCard

`ComputerCard.h` is copied from the
[Music Thing Modular Workshop Computer repository](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard).

Copyright (c) 2024-2026 Chris Johnson. Distributed under the MIT License; see
[`ComputerCard.h`](ComputerCard.h) for its complete licence notice.

The local header adds `CVIn1Millivolts()`, using the framework's existing CV1
EEPROM coefficients and conversion. This accessor is a Workshop Blueberry
extension, not an unmodified upstream 0.4.0 API. The original licence notice
is retained in full in the header. Other differences are whitespace cleanup.

## Raspberry Pi Pico SDK

`pico_sdk_import.cmake` is the Pico SDK import helper, distributed through the
Workshop Computer examples. The firmware is built with the
[Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk), using its
RP2040 hardware drivers and runtime support. The SDK's principal licence is
BSD-3-Clause, copyright Raspberry Pi (Trading) Ltd.; its complete notice is
included in `licenses/PICO_SDK_LICENSE.txt`. Individual SDK components retain
their upstream notices. The SDK itself is an external build dependency, not
vendored into this card folder.

## Blueberry Inspiration

This card is a new Workshop implementation inspired by the interaction model
of [Blueberry](https://github.com/plinkysynth/buddies_public/tree/main/sw/src/blueberry):
quantised keyboard control, recorded gestures, clocked playback, and persistent
button-like transposition. No Blueberry source, panel artwork, or hardware
files are copied into this card.

The Buddies project's software is MIT licensed. Its visual-design and hardware
licences are separate and do not apply to this software-only implementation.
See the [upstream licence declaration](https://github.com/plinkysynth/buddies_public/blob/main/LICENSE.md):
software MIT, graphic design CC BY-SA 4.0, hardware CERN-OHL-P v2.

## Workshop-Specific Code

Blueberry Smoothie is the release name for Workshop Blueberry 0.3.0, by Adrian
Vos. The card-specific recorder, pentatonic quantiser, transposition, control
mapping, switch debounce/startup guard, gate/clock capture, and native tests
are new MIT-licensed code, developed with AI assistance. No original Blueberry
DSP or touch-pad code is used. Development history and earlier versions are
available in [Workshop Buddies](https://github.com/soveda/workshop-buddies/tree/main/cards/blueberry).
