# Temperance 0.2.0: gate mode

Released 2026-11-13, after beta testing on hardware. It adds one thing to Temperance 0.1.0: **Pulse In 1 and 2 can record as gates as well as triggers.** Everything else works as before.

## Flashing

1. Put the Workshop Computer into USB flashing mode, the same way you would to flash any card.
2. Copy `temperance.0.2.0.uf2` onto the `RPI-RP2` drive that appears.
3. The card restarts. One slow chase down the LEDs shows it has loaded.

## Two modes for the Pulse ins

Pulse In 1 records Loop A and Pulse In 2 records Loop B. Both jacks use the same mode.

| | Trigger (the default) | Gate |
| --- | --- | --- |
| **Rising edge** | Starts recording, or stops it if already recording | Starts recording |
| **Falling edge** | Nothing | Stops recording; the loop plays |
| **Loop length** | The time between two triggers | The length of the gate |
| **Two edges under 300ms apart** | Clears the loop | Makes a short loop |

In gate mode, a high gate (+5V) records and a low one (0V) stops.

## Changing mode

**Flick the switch up twice, quickly.** The second flick has to land within 400ms of the first. Then return the switch to the middle.

- Switching up only changes what the knobs do, so the flicks never start, stop or clear a recording.
- Knobs you haven't moved go straight back to working as before.
- **The mode isn't saved.** Every power-on starts in trigger mode.

## Reading the mode from the LEDs

For 1.5 seconds after you change mode, the LEDs show the new one:

```
Trigger            Gate
 ●  ●               ●  ●
 ○  ○               ●  ●
 ○  ○               ●  ●
top row only:      both columns:
a short blip       a level held high
```

After that, in gate mode, the top LED of an **empty** loop glows dimly: it's waiting for a gate. In trigger mode an empty loop's top LED is off, as before. Once a loop is recording or playing, the LEDs look the same in both modes.

## Gate mode in detail

- **A gate over a playing loop** records a new loop over it. The old one fades out.
- **Very short gates** make very short loops, down to buzzes. A gate under 512 clock ticks is too short to loop and leaves the loop empty. That's about 11ms at the fastest clock and 256ms at the slowest.
- **Gates shorter than 20ms** still work. Every record edge acts 20ms late, with the audio delayed to match, so the loop still starts and ends exactly on the gate.
- **The switch still taps** both loops, as in trigger mode. A tap takes a loop over from its gate, so that gate's fall no longer stops it.
- **A gate that rises while the switch is recording** takes the recording over, and its fall stops it.
- **Pulling the cable out of a high gate** counts as the gate going low: the recording stops and the loop plays.
- **A gate already high when you change mode** is ignored until its next rise.
- **If the buffer fills** while the gate is high, the loop plays on its own and the gate's fall does nothing.

## Things worth trying

- A gate sequencer into Pulse In 1 and 2, with different gate lengths per step, so the two loops keep changing length against each other.
- An envelope generator's end-of-rise or gate output, so a loop captures exactly one note.
- A slow square LFO, so a loop re-records every cycle and the loops never settle.

## Known limits

- The mode isn't saved; it resets to trigger at power-on.
- Both Pulse ins share one mode. One jack can't be on gates and the other on triggers.
