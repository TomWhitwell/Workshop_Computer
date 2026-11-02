# LoCho Vibes

Add gentle stereo wobble, worn-cassette warble, or crushed and noisy vibrato to
the Music Thing Modular Workshop Computer. LoCho Vibes takes inspiration from
the ZVEX Lo-Fi Junky, with chorus, vibrato, saturation and compression-style
colour. It sums incoming audio to mono, then creates a stereo output.

## Controls

| Control | Function |
| --- | --- |
| Main | Modulation rate |
| X | Modulation depth |
| Y | Character: lo-fi to the left, neutral at noon, compression to the right |
| Switch Up | Vibrato |
| Switch Middle | Chorus |
| Switch Down (tap) | Cycle through Triangle, Sine and Random Drift LFO shapes |

## Inputs and outputs

| Input / output | Function |
| --- | --- |
| Audio In 1 + 2 | Summed mono audio input |
| Audio Out 1 | Left audio output |
| Audio Out 2 | Right audio output |
| CV In 1 | Modulates depth (X) |
| CV In 2 | Modulates Character (Y) |
| Pulse In 1 | External LFO clock |
| CV Out 1 | Main LFO waveform |
| CV Out 2 | Inverted LFO waveform |

## Chorus and vibrato

Set the switch to Middle for chorus. The dry signal is blended with a modulated
delay, with different delay offsets and opposing modulation on the left and
right. Low depth adds restrained movement; higher settings widen the stereo
image and can become chaotic.

Switch Up selects fully wet vibrato, so the pitch movement is much more obvious.
Low depth gives a gentle tape wobble. Turn it up for warped-cassette sounds that
can become quite seasick.

## Character

Y moves through three regions. CV In 2 modulates the same control, so you can
use a slow voltage to drift between textures or faster modulation for more
rapid changes in tone.

### Left: lo-fi

Turn Y anticlockwise for tape-style saturation, reduced bit resolution,
softened transients and a dirtier, grainier sound. The signal level also drops
as you turn further left.

### Noon: neutral

Around 12 o'clock you get the cleanest modulation, with minimal saturation,
no bit reduction and no compression coloration.

### Right: compression

Turn Y clockwise to increase input drive, soft limiting, saturation, transient
compression and makeup gain. Higher settings produce a louder, denser sound,
like a heavily driven modulation pedal or compressed tape playback.

## Modulation depth

X controls the amount of pitch movement and stereo spread. CV In 1 modulates
this control, affecting chorus movement and vibrato depth. Incoming CV is
attenuated internally to keep the response useful across its range.

## LFO shapes

Tap the switch Down to cycle through the three shapes. The LEDs briefly show
which one you've selected, then return to their normal display.

| Shape | Sound | Selection LEDs |
| --- | --- | --- |
| Triangle | An even rise and fall for regular chorus movement | LEDs 1–2 |
| Sine | Smooth pitch movement and gentle tape-style wobble | LEDs 3–4 |
| Random Drift | Slow modulation mixed with random wandering for irregular pitch and stereo movement | LEDs 5–6 |

Main sets the rate and X sets the depth, including in Random Drift. That shape
adds some of the uneven motion of an old tape transport: it doesn't wander the
same way each time.

## External clock

Patch a pulse clock to Pulse In 1 to synchronise the LFO. Modulation follows the
incoming pulse timing, and changes in clock speed are smoothed. If the pulses
stop, the card returns to its internal clock.

Use this to keep chorus or vibrato in time with a sequencer, trigger pattern or
other clock source.

## CV outputs

CV Out 1 carries the main LFO, and CV Out 2 carries its inverse. Both follow the
selected waveform and clock state. Patch them to filters, panning or other
modulation inputs for movement that follows the effect. The inverted output is
useful when you want two destinations to move in opposite directions.

## LEDs

| LED | Meaning |
| --- | --- |
| LED 1 | LFO timing: one flash per cycle internally, or a flash on each incoming external clock pulse |
| LED 2 | Modulation activity and depth; brighter movement means greater depth, and remains visible at slow rates |
| LED 3 | Lo-fi amount; off at neutral, brighter as Y turns anticlockwise |
| LED 4 | Compression and drive amount; off at neutral, brighter as Y turns clockwise |
| LEDs 5 and 6 | Animation of the selected LFO shape |

The last pair shows a pendulum-like motion for Triangle: the LEDs brighten and
dim in opposite directions. For Sine, they brighten and dim together in a smooth
breathing motion. Random Drift shifts the balance between them irregularly.

LED brightness follows the parameter values and modulation as you play. Tapping
Down briefly replaces this display with the shape-selection LEDs listed above.

## Release notes

### Development status

Actively under development and testing.

## Attribution

LoCho Vibes uses the ComputerCard hardware framework by Chris Johnson. The
framework's original notice is retained in `ComputerCard.h`.

## Firmware distribution notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
copyright and license texts. Keep the accompanying `NOTICE.txt` with
each UF2 when redistributing it.
