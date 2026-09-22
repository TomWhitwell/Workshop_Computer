# Asterisk

A six-track generative drum synthesizer for the Music Thing Modular Workshop Computer.

Asterisk creates complete rhythmic patterns from synthesized drums, bass, and melodic percussion. Its six tracks draw from 14 synthesis algorithms, including FM, additive synthesis, wavefolding, subtractive bass, and several drum models.

Three knobs shape the sound. One flick of the switch creates a new musical idea. Despite the countless synthesis parameters running underneath, the user has no direct control over them and cannot save patches.

## Controls

| Control | Function |
|---|---|
| **Main — Filter** | Turn left for low-pass filtering, right for high-pass filtering. The center position leaves the sound unfiltered. |
| **X — Decay** | Shorten or extend the sounds, from approximately ¼× to 4× their original decay. Center is 1×. Changes apply to new hits. |
| **Y — Heat** | Add saturation, density, and grit, with level compensation. |
| **Switch down — Randomize** | Generate new sounds, rhythms, scale, root note, modulation, and echo settings. The new combination takes over on the next clock step after a short preparation period. Holding the switch down does not repeatedly randomize. |
| **Switch center — Play** | Run the sequencer. |
| **Switch up — Pause** | Stop new hits while existing tails ring out. Return to center to restart the sequence from the beginning. |

Randomization leaves the three knob settings in your hands.

## Clock and connectivity

Asterisk runs on its own or follows an external clock:

- **USB MIDI:** Connect to a computer and enable MIDI Clock output to **Asterisk Workshop** in your DAW. Supports standard 24 PPQN clock, Start, Stop, Continue, and Song Position Pointer.
- **Pulse In 1:** Analog clock input. Each pulse advances one sixteenth note—**4 pulses per quarter note**.
- **Pulse In 2:** Sequence reset.
- **CV In 1:** Filter modulation.
- **CV In 2:** Decay modulation.
- **Audio Out 1 / 2:** Stereo left / right.
- **Pulse Out 1 / 2:** Sixteenth-note clock / triggers from the kick track.
- **CV Out 1 / 2:** Fixed-decay trigger envelopes from two of the tracks.

A cable plugged into Pulse In 1 takes priority over USB MIDI. Without an external clock, Asterisk uses its internally generated tempo and swing. Audio inputs are unused in this version.

## Web editor

The web editor adds internal tempo control, per-track mute and solo, and a global delay amount control. Drag one voice onto another to swap their sounds while keeping each track’s rhythm.

[Open the web editor](https://computer.musicthing.co.uk/programs/369-asterisk/web/index.html). Requires Asterisk 1.0. Connect the Workshop Computer to your computer by USB, then open the editor in Chrome or Edge and allow MIDI and SysEx access.

## Sound demo 2

[Watch on YouTube](https://www.youtube.com/shorts/0aHeqTN17uc)

## Release

- **Creator:** Laboratory 0
- **Version:** 1.0
- **Status:** Released
- **Firmware:** [Asterisk_Workshop_v1.0.uf2](Asterisk_Workshop_v1.0.uf2)

Follow the [official Program Card installation instructions](https://www.musicthing.co.uk/workshopsystem/program-cards/install/) to write the UF2 to a Workshop Computer Program Card.

## Original VST

This hardware version is adapted from my Asterisk VST, available as a free download:

[Download the original Asterisk VST for free](https://laboratory0.gumroad.com/l/ASTERISK?layout=profile).
