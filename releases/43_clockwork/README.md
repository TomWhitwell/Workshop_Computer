# Clockwork

Clockwork is a 6-channel polyrhythmic timing and modulation card for the Music Thing Modular **Workshop Computer**, inspired by ALM Pamela’s Pro Workout. 

It turns the Computer into a central clock, gate generator, LFO source, and USB MIDI-to-CV interface. It provides 6 independent outputs, each capable of generating clock divisions/multiplications, Euclidean rhythms, custom LFO shapes, and decay envelopes. It can sync to external clock pulses, USB MIDI clock, or run on its own internal BPM flywheel.

*   [Web MIDI Manager & Preset Editor](https://vincentmaurer.de/clockwork/index.html)
*   [Clockwork Tutorial Video](https://youtu.be/Pc0wPOAVZmw)
*   [Clockwork Demo Video](https://www.youtube.com/watch?v=WFxAQ-dK7CM)

---

## What does it do?
*   **6 outputs**: Independently configurable for gates, ratchets, envelopes, LFOs, random voltages, or CV delay lines.
*   **Euclidean sequencers**: Built-in Euclidean engines per channel (0 to 16 steps) with active fills and offsets.
*   **Auto-sync clock**: Automatically locks onto incoming USB MIDI clock or external pulse clock, with fallback to an internal clock.
*   **Web MIDI Editor**: A browser-based editor (`web/index.html`) to configure all channels, scales, and logic routing visually.

---

## How to control it

Clockwork uses the Workshop Computer's knobs and its 3-position toggle switch (UP, MIDDLE, and spring-loaded momentary DOWN) to navigate pages and adjust settings.

### 1. Page Navigation
The interface cycles through 7 pages: **Channel 1 to 6** and the **Global Page**.
*   **Tap DOWN**: Cycles forward through the channels (indicated by a single lit LED).
*   **Medium Tap DOWN (hold slightly and release)**: Cycles backward.
*   **Global Page**: Reached at the end of the cycle (all 6 LEDs will pulse gently).

### 2. The Knobs (3 Switch Positions)
The knobs control different parameters depending on the toggle switch position:

*   **MIDDLE Position (Rhythm)**: 
    *   **Main**: Speed Modifier (Divisions `/2048` to `/2`, straight `x1` at noon, and Multipliers `x2` to `x128`).
    *   **X**: Euclidean Steps (`0` to `16`; `0` runs a straight clock).
    *   **Y**: Context-dependent (see "Double Duty" below).
*   **UP Position (Sound Shape)**:
    *   **Main**: Waveform Shape (Gate, Ratchet, Sine, Triangle, Saw, Envelope, S&H, etc.).
    *   **X**: Output Level (bipolar `-100%` to `+100%` on outputs 1-4, phase delay on outputs 5-6).
    *   **Y**: Wave Parameter (pulse width, decay time, or math logic operator depending on active shape).
    *   *Play/Pause Shortcut*: Flicking the switch UP and releasing it back to MIDDLE quickly (<400ms) starts and stops the clock.
*   **DOWN Position (Held - Advanced Settings)**: Hold the toggle DOWN and turn the knobs:
    *   **Main**: Pattern Loop Length (Clamps loop length from `1` to `64` steps, or `0` for infinite).
    *   **X**: Trigger Probability (`0%` to `100%`, acts as a channel mute).
    *   **Y**: Context-dependent (see "Double Duty" below).

### 3. Double Duty (Euclidean Modes)
Knob Y changes function depending on whether Euclidean steps are active:

*   **Rhythm Mode (MIDDLE toggle), Knob Y**:
    *   **Euclidean ON (Steps > 0)**: Controls **Fills** (number of active trigger steps).
    *   **Euclidean OFF (Steps = 0)**: Controls **Phase Offset** (shifts the LFO wave/gate phase by `0..255`).
*   **Advanced Mode (DOWN held), Knob Y**:
    *   **Euclidean ON (Steps > 0)**: Controls **Step Offset** (shifts the Euclidean pattern rotation forward or backward).
    *   **Euclidean OFF (Steps = 0)**: Controls **Quantizer Scale** (snaps LFO or random CV outputs to musical scales).

### 4. Global Settings (All LEDs pulsing)
*   **MIDDLE position**: Main = Master BPM; X = Swing; Y = Humanize (timing jitter).
*   **UP position**: Main = External Clock PPQN; X = Random Seed; Y = Sync Source.
*   **DOWN position (Held)**: Preset Menu. Main = Select preset slot (1-6) or Reset. X = Turn left to Load, or right to Save. Release the toggle to execute.

---

## External CV Inputs (Jacks 1-4)
Jacks 1 to 4 correspond to Channels 1 to 4. When a patch cable is connected, it overrides default behavior for that channel:

1.  **CV Modulation (Default)**: The incoming CV modulates the **Wave Parameter** (Knob Y in UP mode), mixing with the physical knob value.
2.  **External S&H / Smooth**: The channel will sample the voltage on its input jack instead of using the internal random generator.
3.  **Delay Input**: The `Delay` shape records and delays the CV signal coming into its own input jack.
4.  **Math Carrier**: The `Math` shape carrier signal switches to the input jack voltage instead of the internal LFO.
5.  **External Triggers**: If a channel's speed modifier is set to a cross-channel trigger, a cable in its jack allows you to use external analog triggers (with a +3V Schmitt threshold) to clock that channel's loop directly.

---

## Special Speed Modifier Settings (Right Side of Wheel)

Rotating the Main Speed knob in MIDDLE mode past `x128` accesses these special modes:

*   **Cross-Channel Triggers (Outputs 1-6)**: Clocks this channel's loop from the gate transitions of another channel. If a cable is plugged into this channel's dedicated input jack, it uses the physical trigger instead.
*   **MIDI Mode**: Clocks the channel from incoming USB MIDI notes. It tracks the last played MIDI note (monophonic legato note-stacking) and automatically maps output shapes:
    *   `S&H` shape: Outputs calibrated **1V/Octave Pitch CV**.
    *   `Smooth` shape: Outputs **MIDI Velocity CV** (0V to 6V).
    *   `Envelope` / `Log Env` shapes: Generates decay envelopes scaled by note velocity.
    *   `Gate` shape: Generates note gates.

---

## Output Waveforms & Parameter Reference

When selecting shapes, the active shape is displayed on the card's 6 LEDs as a 3x2 matrix (LED 0 to 5):
```
Row 1: [LED 0] [LED 1]
Row 2: [LED 2] [LED 3]
Row 3: [LED 4] [LED 5]
```
Below, `●` indicates the LED is ON and `○` indicates the LED is OFF.

### 1. Analog Outputs (Jacks 1-4)
These swing bipolar (`-6V` to `+6V`) for smooth waveforms, or unipolar (`0V` to `+6V`) for gate/envelope shapes.

| Index | Shape Name | LED Matrix (3x2) | Y Knob Parameter |
|:---:|---|:---:|---|
| **0** | Gate | ○○<br>○○<br>○○ | Duty cycle / pulse width |
| **1** | Ratchet | ●○<br>○○<br>○○ | Subdivisions (2, 3, 4, 6, 8, 12, or 16 pulses) |
| **2** | Sine | ○●<br>○○<br>○○ | Phase offset (0° to 360°) |
| **3** | Triangle | ●●<br>○○<br>○○ | Skew (falling saw ↔ triangle ↔ rising saw) |
| **4** | Saw ↑ | ○○<br>●○<br>○○ | Curve skew (logarithmic ↔ linear ↔ exponential) |
| **5** | Saw ↓ | ●○<br>●○<br>○○ | Curve skew (logarithmic ↔ linear ↔ exponential) |
| **6** | Trapezoid | ○●<br>●○<br>○○ | Peak flat sustain duration |
| **7** | Hump | ●●<br>●○<br>○○ | Peak center skew |
| **8** | Envelope | ○○<br>○●<br>○○ | Linear decay duration |
| **9** | Log Env | ●○<br>○●<br>○○ | Exponential decay duration |
| **10** | S&H | ○●<br>○●<br>○○ | No effect (holds random step CV). Cal. 1V/Oct in MIDI mode |
| **11** | Smooth | ●●<br>○●<br>○○ | Smoothing/slew rate. Mapped to MIDI Velocity in MIDI mode |
| **12** | Delay | ○○<br>●●<br>○○ | CV delay feedback level |
| **13** | Math | ●○<br>●●<br>○○ | Selects math operator (Mix, Sub, Min, Max, Mult, AND, OR, XOR) |

### 2. Digital Gate Outputs (Jacks 5-6)
These swing unipolar (`0V` to `+6V`).

| Index | Shape Name | LED Matrix (3x2) | Y Knob Parameter |
|:---:|---|:---:|---|
| **0** | Gate | ○○<br>○○<br>○○ | Pulse width / duty cycle |
| **1** | Ratchet | ●○<br>○○<br>○○ | Spacing between double triggers |
| **2** | Trigger | ○●<br>○○<br>○○ | Phase delay (trigger is fixed at 10ms) |
| **3** | Burst | ●●<br>○○<br>○○ | Pulse count (1 to 8 triggers) |
| **4** | Random | ○○<br>●○<br>○○ | Max random gate width scale |
| **5** | Utility | ●○<br>●○<br>○○ | Mode select (1 PPQN, 4 PPQN, 24 PPQN, Run Gate) |

---

## Math Operators (Analog Shape 13)

When set to `Math`, you combine the active channel's carrier wave with another channel's output (selected using the Speed knob). Knob Y selects the operator:
*   **Mix/Sum (+)**: Sums both channels.
*   **Difference (-)**: Subtracts modulator from carrier.
*   **Minimum (min)**: Outputs the lowest voltage of the two.
*   **Maximum (max)**: Outputs the highest voltage of the two.
*   **Multiply (*)**: Bipolar ring modulator / VCA logic.
*   **AND (&)**: High (+6V) if both channels are positive, otherwise Low (-6V).
*   **OR (|)**: High (+6V) if either channel is positive, otherwise Low (-6V).
*   **XOR (^)**: High (+6V) if only one channel is positive, otherwise Low (-6V).

---

## Quantizer Scales (Analog channels, steps = 0)

Turn Knob Y in Advanced mode (DOWN held) to snap smooth random walks or LFOs to musical scales:
`OFF` (Raw CV), `CHRO` (Chromatic), `MAJ` (Major), `MPEN` (Major Pentatonic), `MIN` (Natural Minor), `MINP` (Minor Pentatonic), `DOR` (Dorian), `MIXO` (Mixolydian), `LYD` (Lydian), `PHRY` (Phrygian), `HMIN` (Harmonic Minor).

---

## USB MIDI CC Map

Map parameters to your DAW or MIDI controller using these MIDI CC numbers:

### Global Settings
*   **BPM**: CC 70
*   **Swing**: CC 71
*   **Humanize**: CC 72
*   **Seed**: CC 73
*   **Sync Mode**: CC 74

### Channel Settings (Outputs 1-6)
Each channel maps to 9 sequential CCs: **Speed, Steps, Fills, Shape, Parameter, Probability, Scale, Loop Length, Level**.
*   **Output 1**: CC 10 to 18
*   **Output 2**: CC 20 to 28
*   **Output 3**: CC 30 to 38
*   **Output 4**: CC 40 to 48
*   **Output 5**: CC 50 to 58
*   **Output 6**: CC 60 to 68

---

## Web MIDI Manager

Open `web/index.html` in Chrome or Edge.

### How to Connect:
1. Connect a USB-C data cable from the card's USB port to your computer.
2. Open the page. It will automatically search for and connect to `Clockwork MIDI`. (Note: close other DAW or MIDI applications if the port fails to open).

### Key Features:
*   **Real-time Telemetry & Scope**: Live oscilloscope view of the voltages running out of all 6 outputs.
*   **Euclidean Orbits**: Interactive rings showing Euclidean steps, active fills, and phase rotations.
*   **Preset Manager**: Save and load patch configurations, name your presets, and back them up directly to your computer as files.
*   **Advanced Routing**: Configure cross-channel trigger routing, quantizer scales, and mathematical logic operations using simple dropdowns.
*   **Instant Sync**: Any changes made in the web browser interface update the hardware immediately.

---

Created for the Music Thing Modular Workshop System by Vincent Maurer (https://github.com/vincent-maurer/) with assistance from Google Gemini.

Thank you to everyone on the Workshop System Discord server and especially to Tom Whitwell for the module and Chris Johnson (https://github.com/chrisgjohnson) for the ComputerCard library and the great support.
