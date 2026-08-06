# Grains

A granular synthesizer and live-processing effect for the Workshop System Computer. 

## Controls


**Switch Actions**
- **Down (Flick):** Cycle forward a page.
- **Down (Short):** Cycle back a page.
- **Up (Flick):** Toggle Freeze.
- **Down (Hold 2s):** Live mode: Load buffer/ sample. Freeze mode: Save Buffer.


**Page 1: Grain Control**
- Main: Position
- X: Density
- Y: Size

**Page 2: Grain Tone**
- Main: Envelope Shape (Hamm, Decay, Square, Inverse, Gauss)
- X: Left: Jitter, Middle: Chords, Right: Random Reverse
- Y: Pitch

**Page 3: Mix & Spread**
- Main: Wet / Dry Mix
- X: Feedback (Live Mode) / Diffusion (Freeze Mode)
- Y: Stereo Spread

**Page 4: Reverb**
- Main: Reverb Mix
- X: Room Size
- Y: Damping

## IO

- **Audio In 1 (L/Mono):** Audio input for granular recording.
- **Audio In 2 (R):** CV input for Grain Density.
- **CV 1 In (Pitch):** 1V/Octave tracking for granular pitch.
- **CV 2 In (Position):** Granular playhead offset.
- **Pulse 1 In (Trigger):** External grain spawn trigger.
- **Pulse 2 In (Freeze):** Toggles granular buffer recording.

- **Audio Out 1/2:** Main stereo output (Mixed or Wet).
- **CV 1 Out (Envelope):** Looping Ramp 0-5V synced to buffer length.
- **CV 2 Out (Playhead):** Random CV per grain (0-5V).
- **Pulse 1 Out (Grain Gate):** Fires a gate synced to grain spawning.
- **Pulse 2 Out (Freeze State):** HIGH when the buffer is frozen.

## Sample Management

The module features **16 internal slots** for audio storage. Samples are streamed directly from flash memory with zero latency.

- **Slots 1-4 (Persistent Recordings):** These slots hold audio captured directly on the module using the Freeze/Record function. They survive power cycles.
- **Slots 5-16 (User Samples):** These slots are reserved for custom audio files loaded via the `web/grains_sample_manager.html`.

Also available under:
https://vincentmaurer.de/grains/grains_manager.html

### The Save Menu
To save a buffer into a slot:
1. Ensure you're in freeze mode (LED 6 is ON).
2. Hold the momentary switch **DOWN** for 2 seconds.
3. **LED 5** will be on to indicate the Save Menu is active.
4. Use the **Main Knob** to scroll through slots 1-4.
5. LEDs 1-4 display the slot number in **4-bit binary**.
6. Let go of the switch to save the buffer and exit.

### The Load Menu
To load a sample into the engine:
1. Ensure you're not in freeze mode (LED 6 is OFF).
2. Hold the momentary switch **DOWN** for 2 seconds.
3. **LED 6** will be on to indicate the Load Menu is active.
4. Use the **Main Knob** to scroll through slots 1-16.
5. LEDs 1-4 display the slot number in **4-bit binary**.
6. Let go of the switch to load the sample and exit.

## Tape Mode

Tape Mode is a sample player engine that transforms the Grains module into a sample player with manual scrubbin

### Activation
To enter Tape Mode, hold the momentary switch **DOWN** during the power-up sequence (while the LEDs are chasing). LED 5 will remain ON to indicate the mode is active.

### Controls
- **Main Knob:** Manual position scrubbing and playhead offset.
- **X Knob:** Playback Speed (Center is 1.0x).
- **Y Knob:** Pitch control (Center is 1.0x).
- **Switch UP:** Momentary pause.
- **Switch DOWN (Hold):** Open the Slot Selection menu. Use the **Main Knob** to select a slot (1-16).

### IO
- **CV 1 In (Pitch):** 1V/Octave tracking for playback pitch.
- **CV 2 In (Position):** Playhead position offset / scrubbing.
- **Pulse 1 In:** Play/Pause Gate (HIGH = Play, LOW = Pause).
- **Pulse 2 In:** Resets the playhead to the beginning of the sample.
- **Audio Out 1/2:** Main stereo output.
- **CV 1 Out:** Playhead Position Ramp (0-5V).
- **CV 2 Out:** Movement/Speed CV (Bipolar).
- **Pulse 1 Out:** Fires a trigger at the **End of Cycle (EOC)**.
- **Pulse 2 Out:** Fires a trigger at the **Midpoint** of the sample.

### Visuals
- **LED 5:** Indicates Tape Mode is active.
- **LEDs 0, 1, 3, 2:** Circular progress bar showing the current playhead position.
- **Slot Menu:** Displays the current slot in 4-bit binary (LEDs 1-4) while the switch is held down.

Created for the Music Thing Modular Workshop System by Vincent Maurer (https://github.com/vincent-maurer/) with assistance from Google Gemini.

Thank you to everyone on the Workshop System Discord server and especially to Tom Whitwell for the module and Chris Johnson (https://github.com/chrisgjohnson) for the ComputerCard library and the great support.
