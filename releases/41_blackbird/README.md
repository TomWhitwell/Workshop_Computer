# Blackbird v1.2

A program card for the Music Thing Modular Workshop Computer that speaks the same language as [monome crow](https://monome.org/docs/crow/). It runs crow's Lua scripts, and anything that talks to crow over USB talks to blackbird the same way:

- Live code from [druid](https://monome.org/docs/crow/druid/) or [web-druid](https://dessertplanet.github.io/web-druid/) on a computer.
- Connect a [monome norns](https://monome.org/docs/norns/) over USB and run norns scripts that use crow (try [awake](https://llllllll.co/t/awake/21022), [buoys](https://llllllll.co/t/buoys-v1-2-0/37639), [loom](https://llllllll.co/t/loom/21091) ... many also let you use a monome grid).
- Use [Max/MSP or Max for Live](https://monome.org/docs/crow/max-m4l/) with monome's crow devices, or your own Max patches.
- Write your own scripts in [Lua 5.4](https://www.lua.org/manual/5.4/) and upload them with druid's `u` command. They start straight away and are saved to flash on the **physical card itself**, so you can keep several blackbird cards with different scripts and swap them.

Crow's [documentation](https://monome.org/docs/crow/) and examples apply as they are. Blackbird adds the Computer's knobs, switch and pulse jacks in the `bb` namespace: see [Basics](https://computer.musicthing.co.uk/programs/41-blackbird/?panel=basics) for the jack mapping and how to get started.

## What's new in 1.2

- **MIDI over USB**: send and receive MIDI from Lua (the same functions as monome iii). Connected to a computer, blackbird is a USB MIDI device; plug a class-compliant controller into the Computer and it's a USB MIDI host. MIDI clock sync and MPE too. See the [MIDI API](https://computer.musicthing.co.uk/programs/41-blackbird/?panel=midi).
- **ii (I2C)**: wire the Computer's UART pads to an ii bus and blackbird talks to Just Friends, crow, Teletype expanders and more, with crow's `ii` syntax, and answers as crow 4. See [ii](https://computer.musicthing.co.uk/programs/41-blackbird/?panel=ii).
- **Outputs**: audio-rate ASL (`lfo`, `oscillate`, nb_drumcrow's voices) stays in tune, clocks and metros keep time while a script is busy, slopes can be very long, and the `over`, `under` and `rebound` shapes are in.
- **Scripts**: uploads start straight away with no reset, `^^k` always recovers (even from a script that used up all the memory), each new script starts with all the memory, `metro[8]` works, and metros honor `count`.
- **Single-file UF2s with a built-in script**, for sharing patches (see [UF2 script injection](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/UF2_SCRIPT_INJECTION.md)).

If you use monome's crow devices in Max for Live: blackbird now shows up as a MIDI device as well as a serial port, so you may need to pick it in the device's menu again once.

## Documentation

- [Crow documentation](https://monome.org/docs/crow/) and [script reference](https://monome.org/docs/crow/reference/)
- [Bowery](https://github.com/monome/bowery) crow scripts, and [bbbowery](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/bbbowery), versions adapted to the Computer's knobs, switch and pulse jacks
- [Lua 5.4 manual](https://www.lua.org/manual/5.4/)
- Blackbird's own docs: [Basics](https://computer.musicthing.co.uk/programs/41-blackbird/?panel=basics), [First.lua](https://computer.musicthing.co.uk/programs/41-blackbird/?panel=first-lua) (the default script), [MIDI](https://computer.musicthing.co.uk/programs/41-blackbird/?panel=midi), [ii](https://computer.musicthing.co.uk/programs/41-blackbird/?panel=ii)
- More detail:
  - [Knobs & switch](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/KNOB_SWITCH_API.md)
  - [Knobs in ASL](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/KNOBS_WITH_ASL.md)
  - [Pulse inputs](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/PULSEIN_DETECTION.md)
  - [Pulse outputs](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/PULSEOUT_ACTIONS.md)
  - [Sharing scripts as a single UF2](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/UF2_SCRIPT_INJECTION.md)
  - [Integer-only Lua, for heavy DSP-style scripts](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/41_blackbird/docs/INT_MATH.md)

## Talking to blackbird from your own software

Blackbird uses crow's USB serial protocol, so any serial host works (tested with druid, norns, Max/MSP and pyserial):

- A line starting with `^^` is a command (`^^k` stops the script, `^^v` prints the version, ...); anything else is run as Lua.
- A newline ends a line; send multi-line code between triple backticks (```` ``` ````).
- `print()` output and replies come back over the same port.

Wait for blackbird's welcome message before sending anything after power-up.

## Credits & Thank yous

Written by Dune Desormeaux / [@dessertplanet](https://dessertplanet.co), 2025

Special thanks to:
- **Tom Whitwell** for the Workshop System
- **Chris Johnson** for the ComputerCard framework
- **Brian Crabtree** and **Trent Gill** (monome & Whimsical Raps) for the original crow and the open source crow firmware
- **Zack Scholl** (infinitedigits) for encouragement and proving Lua can work on RP2040 with his midi-to-cv project
- **Ben Regnier** (Q*Ben) for extensive testing, feedback, and hard work on porting bowery scripts over to bbbowery

## License

GPLv3 or later - see [LICENSE](LICENSE.txt) file for details.
