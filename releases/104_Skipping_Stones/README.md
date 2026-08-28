# Skipping Stones

Skipping Stones is a controlled-random patch conductor for the Music Thing
Workshop Computer. It is inspired by the ideas in Mutable Instruments Marbles:
related random gates and voltages, deja-vu loop memory, bias/spread shaping,
clocked uncertainty, and repeatable musical chance.

It is designed for the Workshop System as a complete performance setup. Patch
CV Out 1 to a SineSquare oscillator pitch input, Pulse Out 1 to a Slope trigger,
CV Out 2 to a filter, slope time, or VCA/ring-mod control, and Pulse Out 2 to a
second Slope or accent destination. The audio outputs provide closed and open
hi-hats so the card still makes a useful rhythm bed before any analogue patching.

## Controls

| Control | Function |
| --- | --- |
| Main | Internal rate and trigger density |
| X | Deja Vu: from new randomness to locked loop memory |
| Y | Spread and timing jitter |
| Switch Up | Coupled mode: more regular relationship between the two pulse streams |
| Switch Middle | Sparse mode: more gaps and independent pulse decisions |
| Switch Down | Write mode: aggressively replaces steps in the loop while held |
| CV In 1 | Adds to density/rate, good with Four Voltages |
| CV In 2 | Adds to spread/jitter |
| Audio In 1 | Bias input for tilting the random voltage distribution |
| Pulse In 1 | External clock |
| Pulse In 2 | Reset to the start of the captured loop |

## Outputs

| Output | Function |
| --- | --- |
| CV Out 1 | Calibrated quantized pitch CV |
| CV Out 2 | Smoothed related modulation CV |
| Pulse Out 1 | Main gate stream |
| Pulse Out 2 | Accent/fill gate stream |
| Audio Out 1 | Closed hi-hat, triggered by the main gate stream |
| Audio Out 2 | Open hi-hat, triggered by the accent/fill gate stream |

## Notes

The firmware runs the RP2040 at 192 MHz and is copied to RAM by the linker to
avoid flash-cache timing jitter in the audio interrupt.

This is not a direct source port of Marbles. It uses the same broad instrument
ideas, adapted to the Workshop Computer's two CV outputs, two pulse outputs,
audio outputs, and three-knob/switch panel.

## Credits

Inspired by Mutable Instruments Marbles by Emilie Gillet. Mutable Instruments'
STM32 firmware is MIT licensed; this card is independently adapted for the
Workshop Computer and avoids using the original module name for the derivative
work.
