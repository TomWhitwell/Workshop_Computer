# Simple MIDI

Simple MIDI turns the Music Thing Modular Workshop Computer into a USB MIDI
interface. It sends controls and CV from the Workshop System to a computer, and
turns incoming MIDI notes, gates, and continuous-controller messages into CV and
gate signals.

It is also the card used to calibrate the Workshop Computer's two precision CV
outputs. Other cards, including Turing Machine and Reverb+, can use the saved
calibration data so that their pitch outputs play in tune.

Simple MIDI was written by Tom Whitwell in Herne Hill, London, between October
2024 and October 2025.

## What it does

Simple MIDI receives two channels of MIDI from a connected computer:

- Note pitch
- Note on/off gates
- Continuous Controller 42

It sends eight controls from the Workshop System back to the computer:

- Main, X, and Y knobs
- The Z switch
- Both Audio/CV inputs
- Both precision CV inputs

The Workshop System appears on the connected computer as **Workshop System
MIDI** or **Music Thing Workshop System MIDI**.

[Watch the 90-second Simple MIDI introduction on
Instagram](https://www.instagram.com/reel/DBjZivNN67D/).

## Connecting to a computer

1. Connect the Workshop Computer's front-panel USB-C port to a computer running
   a MIDI sequencer or DAW.
2. Cycle the Workshop System's power. Power cycling, rather than only pressing
   Reset, ensures that the Workshop Computer detects the USB host.
3. Insert the Simple MIDI card with its gold connector facing down.
4. Press the small Reset/Load button beside the Program Card slot.
5. The LEDs should blink briefly and then stop.
6. Select **Workshop System MIDI** or **Music Thing Workshop System MIDI** in
   your DAW or MIDI software.

If the LEDs continue blinking, the USB connection has not been established:

- Turn the Workshop System off and on again.
- Check that the USB cable supports data. Some USB-C cables provide power only.
- Try another USB port or cable.

The Workshop Computer does not need to connect to a USB-C socket on the host; a
USB-A-to-USB-C data cable is also suitable.

## MIDI controls

### Workshop System to computer

| Workshop Computer control | MIDI message |
| --- | --- |
| Main knob | CC 34 |
| X knob | CC 35 |
| Y knob | CC 36 |
| Z switch | CC 37 |
| Audio/CV In 1 | CC 38 |
| Audio/CV In 2 | CC 39 |
| CV In 1 | CC 40 |
| CV In 2 | CC 41 |

### Computer to Workshop System

| Workshop Computer output | MIDI input |
| --- | --- |
| Audio/CV Out 1 | Channel 1, CC 42 |
| Audio/CV Out 2 | Channel 2, CC 42 |
| CV Out 1 | Channel 1 note pitch |
| CV Out 2 | Channel 2 note pitch |
| Pulse Out 1 | Channel 1 note on/off |
| Pulse Out 2 | Channel 2 note on/off |

## Calibrating the Workshop Computer

For full details on how to calibrate the Workshop Computer and Workshop System oscillators, see the [complete Workshop System calibration
guide](https://www.musicthing.co.uk/Workshop_System_Calibration/).

## Using the calibration data 

For more details check the [Technical notes](TECHNICAL_NOTES.md)
