# Outputs and routing

The card turns USB MIDI into three kinds of output at once: two monophonic CV/gate pairs and a four-voice polyphonic synth on the audio jacks. You can use any combination; many patches use CV to drive external oscillators while the onboard audio fills in chords or a bass line.

## CV and gates (Voice A and B)

| Output | Voice | Default MIDI channel | Notes |
|--------|-------|----------------------|-------|
| CV Out 1 | A | 1 | Pitch, 1 V/oct |
| Pulse Out 1 | A | 1 | Gate, high while a note is held |
| CV Out 2 | B | 2 | Pitch |
| Pulse Out 2 | B | 2 | Gate |

**Last-note priority** applies per voice. If you hold C and then E on channel 1, CV Out 1 tracks E until you release E, then returns to C if C is still held.

**Pitch bend** on a channel bends CV for that voice. Factory bend range is **±2 semitones** (learn slot 3). Increase the range in SETUP or the web editor if you want wider sweeps.

**Calibration:** MIDI note 60 (middle C) is **0 V** on CV, using the Workshop Computer Simple MIDI calibration stored in EEPROM. If CV pitch does not match your oscillators, calibrate on the Computer framework first; this card reads that table.

## Audio engine

Audio Out 1 and 2 carry a **four-voice polyphonic** mix. Any note on any MIDI channel can allocate one of four voices (subject to your maps). When all four are busy, stealing follows the usual polyphonic rules inside the engine.

The timbre is not fixed: it comes from the **voice matrix** (121 patches). One MIDI CC value selects the patch for the whole engine. See [Voice matrix](voice-matrix.md).

Some variations (especially chorus) send slightly different material to left and right. Most patches are mono on both outputs.

Audio is **full level** line output. Plan headroom in your mixer.

## MIDI channels in practice

Out of the box:

- Play **channel 1** for Voice A CV/gate and for notes into the poly engine.
- Play **channel 2** if you want a second CV/gate pair.
- Use **channel 10** for the fixed drum kit (does not use CV).

You can remap Voice A and B channels in SETUP (slots 1 and 2) or in the web editor Settings panel. The audio engine still receives note data broadly; channel assignment for CV is independent of which notes trigger poly voices unless you configure it that way through your controller’s transmit channels.

## Pitch bend vs modulation

Pitch bend messages affect CV pitch on the matching voice. Mod wheel and other CCs only affect parameters you have **learned** to a slot (voice patch, ADSR, cutoff, PWM). There is no default mod wheel mapping.

## Typical patches

**External mono synth on CV, card handles chords**  
Voice A → oscillator CV/gate. Play bass lines on ch 1. Layer chords on the same channel for the onboard poly engine, or use ch 2 for a second CV line while ch 1 notes still stack on audio.

**Two-voice modular duet**  
Ch 1 → Voice A, ch 2 → Voice B. Sequence or play two parts into separate modules. Audio Out can stay silent in the mix if you turn down the fader, or add a pad from the matrix.

**All-in-the-box**  
Ignore CV. Run only Audio Out to a mixer. Four notes of polyphony with CC 24 choosing the patch.

## What is not routed

CV inputs, pulse inputs, and audio inputs are not used by this program. There is no envelope follower or external audio pass-through.
