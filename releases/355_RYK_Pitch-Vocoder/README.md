# RYK 355 Pitch to Voltage / 350 Vocoder

This Program Card contains two related processors:

- **355 Pitch to Voltage** tracks a monophonic sound and produces pitch CV, envelope CV, gates, triggers, and a matching oscillator.
- **350 Vocoder** is a 14-band vocoder with an optional internal four-note chord synthesizer.

## Choosing a model

Press **Reset** while holding the switch **down**. Keep holding until you hear the model number, then release. Repeat the process to change between 355 and 350.

## 355 Pitch to Voltage

Connect a clean, monophonic sound to **Audio In 1**. The tracker works best when only one clear note is present at a time. Adjust the source so the top LED peaks at the attack of a note.

### Controls

- **Main - Pitch offset:** Centre is unshifted; fully left is one octave down and fully right is one octave up.
- **X - Trigger threshold:** Low settings suit smooth sounds such as flute or trumpet. Higher settings suit sources with harder attacks, such as guitar or piano.
- **Y - Slew:** Smooths both the tracked 1V/oct CV and oscillator output.
- **Switch down - Next waveform:** Cycles the tracked oscillator through sine, square, saw, super saw 1, and super saw 2.

### Connections

- **Audio In 1:** Monophonic sound to track.
- **Audio Out 1:** Raw tracked oscillator.
- **Audio Out 2:** Tracked oscillator shaped by the input envelope.
- **CV Out 1:** Tracked 1V/oct pitch CV.
- **CV Out 2:** Envelope follower CV.
- **Pulse Out 1:** Gate while the signal is above the threshold set by X.
- **Pulse Out 2:** Short trigger when the signal crosses the threshold.

Accurate guitar tracking rewards clean playing: mute unused strings and avoid overlapping notes.

## 350 Vocoder

Connect a voice or other modulator to **Audio In 1**. Connect a harmonically rich carrier to **Audio In 2**, or leave it unplugged to use the internal four-note chord synthesizer.

### Controls

- **Main - Output mix:** Balances the original modulator and the processed carrier.
- **X - Carrier pitch:** Sets the pitch of the internal carrier synthesizer.
- **Y - Carrier chord:** Selects major and minor inversions, diminished and suspended chords, seventh chords, sixth chords, or a pentatonic chord.
- **Switch up - Freeze carrier:** Freezes the current carrier state.
- **Switch down - Next waveform:** Cycles the internal carrier waveform. The manual lists saw, sync saw, and sin-stack options.

### Connections

- **Audio In 1:** Modulator input, usually a voice or rhythmic sound.
- **Audio In 2:** External carrier input; unplug it to use the internal chord synthesizer.
- **Audio Out 1:** Mix of the modulator and vocoded carrier.
- **Audio Out 2:** Vocoded carrier only.
- **CV In 1:** 1V/oct pitch control for the internal carrier.
- **CV In 2:** CV control of chord selection.
- **CV Out 1:** Envelope follower from the modulator.
- **Pulse In 1:** Freezes the carrier.

## Links

- [Buy the RYK Program Card set from Thonk](https://www.thonk.co.uk/shop/ryk-pgm-cards-mixed/)
- [RYK Program Cards user manual (PDF)](https://www.thonk.co.uk/wp-content/uploads/2026/01/MTM-Music-Thing-Modular-cards-Manual_01_FINAL-THONK_V2.pdf)

