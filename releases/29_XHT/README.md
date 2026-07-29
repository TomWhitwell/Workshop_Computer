# XHT Card

This folder now contains the main and MIDI firmware builds, plus rollback-safe fallback UF2s, for the Music Thing Modular Workshop Computer.

- `uf2/XHTCard.uf2` — current main build
- `uf2/XHTCard_fallback.uf2` — last accepted rollback-safe build
- `uf2/XHTCardMidi.uf2` — current tested MIDI build
- `uf2/XHTCardMidi_fallback.uf2` — last accepted MIDI rollback-safe build

If you just want the current recommended build, flash `uf2/XHTCard.uf2`.

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
- [README_fallback.md](/Users/adrianvos/coding/GitHub/XHT_card/README_fallback.md) — rollback-safe build
- [README_midi.md](/Users/adrianvos/coding/GitHub/XHT_card/README_midi.md) — tested MIDI build
- [README_midi_fallback.md](/Users/adrianvos/coding/GitHub/XHT_card/README_midi_fallback.md) — MIDI rollback-safe build

## Notes

- The sound is a synthesised deep-note-style gesture, not a bundled sample.
- The project runs at `192 MHz`.
- The build uses the current authoritative `ComputerCard.h` from the Workshop Computer tree.
- The synthesis idea was inspired by Tod E. Kurt's GPL-3.0 `derpnote2`, so this project remains GPL-3.0.
- In the MIDI build, `P1` only gates MIDI out when `P1` is physically patched. Unpatched `P1` leaves MIDI out always open.
- In the MIDI build, note-on/note-off and sustain pedal (`CC64`) now control whether the sound stays open when playing from a keyboard.
- In current active builds, `CV Out 1` mirrors note position unless `CV1` is patched, when it becomes pitch; `CV Out 2` always mirrors note position.
- In current active builds, analog `CV2` is scaled so 0-5V controller/mod-wheel sources can cover the full note-position travel.
- Pulse Out 1 mirrors note gate behavior and Pulse Out 2 mirrors the `P2` input pulse.

## Attribution

This card uses the ComputerCard hardware framework by Chris Johnson. Its USB
MIDI host support includes the MIT-licensed rppicomidi files, copyright 2023
rppicomidi; their copyright and licence notices are retained in the source.
