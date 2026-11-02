# XHT Card

XHT is a deep-note-inspired stereo chord swarm for the Music Thing Modular
Workshop Computer. Turn Main to move through the sound, or press the switch Down
to start a one-shot that travels towards the position you've chosen. Add delay
with X and reverb with Y, and use CV or an external pulse clock to control the
movement.

## Choose your firmware

Flash `uf2/XHTCard.uf2` for the current recommended version. Choose the MIDI
version if you want to play from a keyboard, use a mod wheel, or send the chord
notes to another instrument:

- `uf2/XHTCard.uf2` — current main build
- `uf2/XHTCardMidi.uf2` — current tested MIDI build

## Short Control Summary

- `Main` — note position / one-shot destination
- `X` — delay
- `Y` — reverb
- switch up — octave up
- switch middle — normal octave
- momentary switch down — reset / one-shot from start to destination
- `CV1` in — pitch transpose
- `CV2` in — position/destination control
- `P1` in — gate when patched; sustained note when unpatched
- `P2` in — external stepped clock
- `Audio 1/2` — stereo output with effects
- `CV Out 1` — position, or pitch when `CV1` is patched
- `CV Out 2` — position
- `Pulse Out 1` — note gate
- `Pulse Out 2` — mirror of `P2`

MIDI build additions:

- MIDI note input transposes pitch and gates sound
- sustain pedal is `CC64`
- mod wheel / `CC1` controls note destination
- MIDI out sends the current chord notes
- MIDI clock is ignored
- LED 5 shows USB role when not clocking from `P2`: solid = USB host/controller mode, slow blink = USB device/DAW mode

## Quick Guide

- [README_main.md](/Users/adrianvos/coding/GitHub/XHT_card/README_main.md) — current recommended build
- [README_midi.md](/Users/adrianvos/coding/GitHub/XHT_card/README_midi.md) — tested MIDI build

## Notes

- The card synthesises its deep-note sound; it does not play a bundled sample.
- The project runs at `192 MHz`.
- The build uses the pinned ComputerCard 0.4.0 header included in this card folder.
- The synthesis idea was inspired by Tod E. Kurt's `derpnote2`. This card is released under GPL-3.0-or-later; see LICENSE and THIRD_PARTY_NOTICES.md.
- In the MIDI build, `P1` only gates MIDI out when `P1` is physically patched. Unpatched `P1` leaves MIDI out always open.
- In the MIDI build, MIDI notes and the sustain pedal (`CC64`) control whether the sound stays open when playing from a keyboard.
- `CV Out 1` mirrors note position unless `CV1` is patched, when it becomes pitch; `CV Out 2` always mirrors note position.
- A 0–5 V controller or mod-wheel signal patched to `CV2` can cover the full note-position range.
- Pulse Out 1 mirrors note gate behavior and Pulse Out 2 mirrors the `P2` input pulse.

## Release notes

### Previous accepted builds

These fallback UF2s are included so you can return to a previous accepted build
if needed:

- `uf2/XHTCard_fallback.uf2` — previous accepted main build
- `uf2/XHTCardMidi_fallback.uf2` — previous accepted MIDI build

Their guides are:

- [README_fallback.md](/Users/adrianvos/coding/GitHub/XHT_card/README_fallback.md) — previous accepted main build
- [README_midi_fallback.md](/Users/adrianvos/coding/GitHub/XHT_card/README_midi_fallback.md) — previous accepted MIDI build

## Attribution

This card uses the ComputerCard hardware framework by Chris Johnson. Its USB
MIDI host support includes the MIT-licensed rppicomidi files, copyright 2023
rppicomidi; their copyright and licence notices are retained in the source.

## Firmware distribution notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
copyright and license texts. Keep the accompanying `NOTICE.txt` with
each UF2 when redistributing it.

This card is GPL-3.0-or-later; see the complete text in LICENSE. When
distributing a compiled version, provide its corresponding source and
build instructions along with the applicable dependency notices.
