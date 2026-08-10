### Global Overview & Signal Routing

Bends is a 6-stage stereo multi-effects processor, live stutter looper, and signal degradation engine designed for the Workshop Computer.

The module makes digital imperfection its core concept. Inspired by 90s circuit-bent rack units, Bends exposes the artifacts, memory corruption, and data bus shorting of early digital processors. It functions both as a traditional stereo multi-effects unit and as a self-contained generative sound source.

Bends spans a wide range of sound processing:
* **Clean stereo utility**: BBD chorus, pitch-glide delays, state-variable filters, and spring or hall reverbs.
* **Glitch textures**: Granular freeze pendulums, clock-synced breakbeat slicing, and shimmer pitch-shifting.
* **Digital corruption**: Wavefolder fuzz, bitwise BBD XOR data bus shorting, lossy MP3 compression, network packet dropouts, and CD sputter.

#### Switch Controls & Navigation
- **Tap DOWN (<350ms)**: Step forward through parameter pages (`1. Chorus` -> `2. Loss` -> `3. Delay` -> `4. Glitcher` -> `5. Filter` -> `6. Reverb`).
- **Hold DOWN (>=350ms)**: Enter **Global Mode** to adjust global bend, stereo width, and routing presets.
- **Hold DOWN (3s)**: Save current parameters and routing setup to persistent flash memory (600ms strobe confirmation).
- **Flick UP**: Toggle live buffer **Freeze Mode**.

#### Global Controls (Global Mode)
- **Main (Bend)**: Sweeps background noise, vinyl crackle, bitcrush fuzz, and packet dropouts across all 6 effect stages simultaneously.
- **X (Width & Mode)**: Extended Mono Mode (CCW limit <2%), Stereo Width (0% to 100%), or Dual Mono Mode (CW limit >95%).
- **Y (Routing & Reset)**: Selects 1 of 4 signal routing chains (`0: Series FX`, `1: Space Wash`, `2: Granular Cloud`, `3: Filtered Dub`). Turn fully CW (>96%) to Clean Reset.

#### Mode LED Indicators
- **LEDs 0 to 3**: Active signal routing chain (0 to 3).
- **LED 4**: Dual Mono Mode active.
- **LED 5**: Extended Mono Mode active.

#### Inputs
- **Audio 1 & 2**: Stereo audio inputs. When no cable is inserted in Audio 2, Audio 1 is normalled to both channels to maintain stereo processing.
- **CV 1**: Bipolar CV input modulating primary page parameters or tuned 1V/Oct pitch in Karplus-Strong string and filter oscillator modes.
- **CV 2**: Bipolar CV input modulating secondary page parameters, digital corruption, and stutter loop capture.
- **Clock Sync (`Pulse 1`)**: Multi-standard pulse clock sync (1, 2, 4, and 24 PPQN DIN Sync) for delay subdivisions, loop stepping, and pitch CV sequence advances.
- **Freeze Gate (`Pulse 2`)**: High gate input (>+1.2V) locking delay and stutter buffers into live freeze.

#### Outputs
- **Out 1 & 2**: Processed stereo audio outputs with DC-blocking filters.
- **Pitch CV (`CV Out 1`)**: Turing Machine 1V/Oct quantized semitone CV sequence output driven by internal shift register.
- **Random CV (`CV Out 2`)**: Stepped random Sample & Hold CV output updated per clock step or loop reset, designed for self-modulation.
- **Loop Trig (`Pulse Out 1`)**: +5V 2ms trigger pulse output emitted on granular loop resets or clock steps.
- **Texture (`Pulse Out 2`)**: Lo-fi PWM audio stream output of microsound bleeps, vinyl crackle, and sub-harmonic loop ticks.

#### Reset Procedures
- **Preset Clean Reset**: Turn Knob Y fully CW (>96%) in Global Mode to restore all 6 parameter pages to clean factory defaults.
- **Factory Hardware Reset**: Power on system while holding Z Switch DOWN (1s LED flash) to restore original factory settings from flash memory.
