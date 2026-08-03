# XHT Card Main

Flash: [XHTCard.uf2](/Users/adrianvos/coding/GitHub/XHT_card/uf2/XHTCard.uf2)

This is the current recommended version.

What it does:

- creates a stereo deep-note-style chord swarm
- `Main` scrubs through the note
- pressing reset launches a one-shot from the start toward the current destination
- `CV2` can move the destination across the full note travel, including 0-5V controller/mod-wheel sources
- `P2` clocks the note through stepped positions and overrides the one-shot
- `CV Out 1` mirrors note position unless `CV1` is patched; then it outputs pitch
- `CV Out 2` always mirrors the current note-position state

Short control summary:

- `Main` — position / destination through the note
- `X` — delay
- `Y` — reverb
- switch up — octave up
- switch middle — normal octave
- momentary switch down — reset / one-shot from start to destination
- `CV1` in — pitch transpose
- `CV2` in — position offset and one-shot destination contribution, scaled for full travel from 0-5V sources
- `P1` in — gate the sound when patched, sustain when unpatched
- `P2` in — external clock for stepped note position

Outputs:

- `Audio Out 1/2` — stereo audio with delay and reverb
- `CV Out 1` — current note-position state, or pitch when `CV1` is patched
- `CV Out 2` — current note-position state as CV
- `Pulse Out 1` — note gate state; with `P1` patched it mirrors `P1`, unpatched it stays high while the note is audible
- `Pulse Out 2` — direct mirror of `P2` input

Use this version if you want the tested, accepted build.
