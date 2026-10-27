# First.lua: unique melody box

First.lua is crow's default script, and blackbird runs it whenever no script has been uploaded (or after `^^c` clears the saved one). It's a two-voice melody generator, and each card plays its own: the patterns are generated from the RP2040's unique ID, so no two Computers play the same tune.

## Patching

- **CV In 1: clock.** Each rising edge (crossing about 1V) moves both voices on a step. Any clock or gate works: a pulse output, an LFO, another sequencer.
- **CV In 2: range.** The further from 0V (in either direction), the wider the melodies range and the more varied they get. 0V and above picks a major pentatonic scale, below 0V the major scale. Leave it empty for simple, narrow tunes.
- **CV Out 1 and CV Out 2: pitch** (1V/octave) for voice 1 and voice 2. Voice 2 has a short slew.
- **Audio Out 1 and Audio Out 2: envelopes** for voice 1 and voice 2, attack-decay shapes. Patch them to a VCA or filter for each voice.

A typical patch: CV Out 1 to an oscillator's pitch, Audio Out 1 opening its VCA, and the same for voice 2 with CV Out 2 and Audio Out 2.

## How it plays

Each voice runs two step sequences, each of its own length (6 to 24 steps): one decides on which clocks the voice's envelope fires, the other on which clocks its pitch changes. With four sequences of different lengths, the voices drift against each other and the whole pattern takes a long time to repeat.

- Not every clock fires an envelope or changes a pitch, so the rhythms have rests, and a pitch can change under a sustaining envelope.
- The envelopes respond to the rhythm: the longer voice 1 has rested, the longer its next decay, and the longer voice 2 has rested, the longer its next attack.

The knobs, switch, audio inputs and pulse jacks aren't used: First.lua is crow's script, and crow has none of them. Upload your own script to use them (see the Basics tab).
