# Simple MIDI technical notes

This document contains development and implementation information for Simple
MIDI. For installation, normal operation, troubleshooting, and calibration, see
[README.md](README.md).

## Calibration support

Simple MIDI contains reusable code for storing and applying Workshop Computer
CV-output calibration.

### `CV.h`

`CV.h` contains Chris Johnson's delta-sigma output code. It obtains approximately
19-bit precision from the RP2040's 11-bit PWM output.

### `DACChannel.h`

`DACChannel.h` applies the stored calibration data to create the calibration
constants used by the precision CV outputs.

### `calibration.h`

`calibration.h` manages reading and writing calibration data in EEPROM, including
error checking.

The calibration memory map reserves ten calibration points for each of the two
channels. Simple MIDI currently uses three points per channel: -2 V, 0 V, and
+2 V.

The current manual process could be automated further to improve precision and
ease of use.

### `Click.h`

`Click.h` is a small library for distinguishing short and long switch actions at
startup.

## EEPROM calibration memory map

Calibration data occupies page 0 at address `0x50`.

| Offset | Bytes | Contents |
| ---: | ---: | --- |
| 0 | 2 | Magic number `2001`; its presence indicates that EEPROM has been initialized |
| 2 | 1 | Format version, `0`–`255` |
| 3 | 1 | Padding |
| 4 | 1 | Channel 0 entry count, `0`–`9` |
| 5 | 40 | Ten four-byte Channel 0 entries |
| 45 | 1 | Channel 1 entry count, `0`–`9` |
| 46 | 40 | Ten four-byte Channel 1 entries |
| 86 | 2 | CRC over the preceding data |
| 88 | — | End |

Each four-byte calibration entry contains:

- A four-bit voltage value
- Four reserved bits
- A 24-bit output setting

## Dependencies

### ResponsiveAnalogRead

ResponsiveAnalogRead by Damien Clarke:

https://github.com/dxinteractive/ResponsiveAnalogRead

### Adafruit TinyUSB

Adafruit TinyUSB Arduino:

https://github.com/adafruit/Adafruit_TinyUSB_Arduino

### Arduino-Pico

Earle F. Philhower's Arduino core for the Raspberry Pi Pico:

https://github.com/earlephilhower/arduino-pico

## Arduino compilation settings

| Setting | Value |
| --- | --- |
| Board | Generic RP2040 |
| Flash size | 16 MB (no filesystem) |
| CPU speed | 133 MHz |
| Boot Stage 2 | W25Q16JV QSPI/4 for newer 2 MB cards |
| USB stack | Adafruit TinyUSB |

For builds targeting Windows stability, the original v0.6.0 notes specify
increasing the buffer sizes in `tusb_config_rp2040.h` from 64 to 512 before
building.

## Release history

### v0.6.0

- Updated the Earle F. Philhower Arduino-Pico core from 3.9.2 to 5.3.0. This also
  updated TinyUSB and incorporated multiple
  [TinyUSB fixes](https://github.com/hathach/tinyusb/pull/2492).
- Increased USB buffer sizes for Windows stability.
- Changed `SWITCH_DOWN` from `500` to `340`; the former threshold was missed on
  some boards and prevented calibration mode from starting.
- Changed `CableName` from `1` to `0` for Windows stability.
- Added `TinyUSBDevice.task();` to all loops to keep USB processing alive.
- Initialized pitch-CV outputs to 0 V before receiving MIDI notes, preventing a
  jump after calibration finishes.
- Corrected outgoing MIDI messages from knobs and CV inputs to match the
  [published Simple MIDI documentation](https://www.musicthing.co.uk/Computer_Program_Cards/#00-simple-midi).
- Reduced unnecessary outgoing MIDI traffic by sending messages only when MIDI
  values change, rather than whenever underlying analogue readings change.
- Disabled MIDI Thru so incoming MIDI notes are not echoed back from the
  Workshop System.

### v0.5.0

- Fixed an issue introduced in v0.3.0 that prevented calibration mode from
  starting without a serial connection.

### v0.3.0

- First release containing the calibration system.

