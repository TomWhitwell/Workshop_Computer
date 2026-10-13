# Drum kit

A fixed eight-piece kit plays on **MIDI channel 10**, General MIDI style note numbers **36–43**. It mixes into **Audio Out** alongside the poly synth. It does not trigger CV or gates.

Drums are always available. You do not enable them in SETUP. They ignore the voice matrix CC.

## Note map

| Note | Name | Behaviour |
|------|------|-----------|
| 36 | Kick | Low sine drop, velocity sensitive |
| 37 | Snare | Tone + noise, velocity sensitive |
| 38 | Closed hi-hat | Short noise burst |
| 39 | Open hi-hat | Longer noise; **chokes** when 38 plays or on note-off |
| 40 | Low tom | Lower tom pitch |
| 41 | High tom | Higher tom pitch |
| 42 | Crash | Noise splash |
| 43 | Ride | Metallic ping + noise |

Velocity scales loudness. Sending note-off is good practice for open hat (39) and long samples; closed hat and kick decay quickly on their own.

## Using with the poly synth

Channel 10 is separate from Voice A/B (default channels 1 and 2). A single keyboard can transmit on multiple channels if you configure splits or layers on the controller.

The drum voices share the audio mix with up to four poly synth notes. Heavy simultaneous playing is still four poly notes **plus** overlapping drum hits; extremely dense MIDI may steal drum voices internally, but normal pad playing is fine.

## No learn slots

Drum mapping is fixed in firmware. You cannot remap note 36 to a different sound without code changes.

## Typical setup

- **Finger drumming:** set keyboard or pad controller to channel 10, notes 36–43.
- **Sequencer:** one track on ch 10 for drums, other tracks on ch 1–2 for CV and poly audio.
- **Web editor:** virtual keyboard defaults to channel 1; use a relayed pad controller or change channel in your DAW for drum tests.

If you hear drums but no synth (or the reverse), check transmit channel on each part.
