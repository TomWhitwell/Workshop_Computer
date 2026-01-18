![YinYang oscillator example](example.png)

# 69 - trace

trace is a collection of oscillograph oscillators with two channel outputs, designed to work with X/Y mode on oscilloscope to make oscillograph.

Use with all kinds of effects and make cool oscilloscope music with cohesive visuals.

## Overview

There are currently three banks of oscillator built with different models. A-1,B-3,C-3, a total of 7 oscillators.

- Bank A - math defiend oscillator (the Yin/Yang symbol above is an example)
- Bank B - 3d polygon oscillator, project 3d polygon to 2d plane
- Bank C - wavetable oscillator, use two single cycle waveform to trace vector graphic

## Development  
Functional but still work in progress. More custom shapes are welcome! 

Find the detail, raise issue or make contribution on the complete repo [Trace_workshop_Computer](https://github.com/indiepaleale/Trace-Workshop-Computer)

\*\* I'm fairly new to C++, so any suggestion or comment to better code are welcome too!

## Controls and I/O

| Control | Function |
|---------|----------|
| `Main Knob` | Pitch (exponential), summed with `CV In 1` |
| `X Knob`   | Growth, depth of phase, summed with `Audio In 1`<br> **ALT:** attenuate `Audio In 1` when switch is `up` |
| `Y Knob`   | Depends on selected oscillator, summed with `Audio In 2`<br> **ALT:** attenuate `Audio In 2` when switch is `up` |
| `Toggle witch` | **UP**: Enable **ALT**<br>**MID**: Normal mode.<br>**DOWN**: Move to next oscillator|
| `LED` | Left collum indicate oscillator bank, right collum indicate index (top to bottom)|

| Jack I/O | Function |
|----------------|----------|
| `Audio In 1`    | Modulate ***Growth***|
| `Audio In 2`    | Modulate ***Y Knob***|
| `Audio Out 1`| Audio out horizontal channel - L|
| `Audio Out 2`| Audio out vertical channel - R |
| `CV In 1`    | Adds to the ***Pitch***<br>(not calibrated to 1V/OCT **help on this maybe?) |
| `Pulse In 1`| Rising edge cycles through osc within the same bank|
| `Pulse In 2`| |
| `Pulse Out 1`| Fires when switch `down` toggled |

## Future Plan
- Calibrate pitch input to 1v/OCT
- Add more oscillator variation
- Web interface to upload new shape from mesh/wavetable
- Use binary code for LED for maximum 8 banks of 8 oscillators

## Credit
Big thanks to @Jerobeam Fenderson for inspiration. Find amazing oscilloscope art on his website [oscilloscope music](https://oscilloscopemusic.com)

Use an analog oscilloscope for the best visual or download this cross platform [oscilloscope](https://oscilloscopemusic.com/software/oscilloscope/) with your interface.