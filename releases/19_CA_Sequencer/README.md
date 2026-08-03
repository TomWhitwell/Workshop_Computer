# Cellular Automata Sequencer

A generative gate and CV pattern generator for the [Music Thing Modular Workshop Computer](https://github.com/TomWhitwell/Workshop_Computer), inspired by the [NLC Cellular Automata](https://www.nonlinearcircuits.com/modules/p/cellular-automata) Eurorack module.

Source code and latest release: https://github.com/ainews1/ca_sequencer

## What It Does

This program card implements a **16-cell 4x4 grid** running cellular automata **rules 90 and 150**. Each cell turns on or off based on its neighbors, creating evolving, unpredictable gate patterns.

Unlike the hardware module, this version adds:
- **Quantized melody** — CV Out 1 outputs 1V/oct pitch from the CA state
- **Selectable scales** — chromatic, major, minor, minor pentatonic, dorian
- **Rule morphing** — blend between Rule 90 and Rule 150 via CV In 2
- **Clock division** — sync to external clock at variable divisions
- **Spontaneous seeding** — probability-based corner injection via CV In 1

## How to Build

### Prerequisites
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- CMake 3.13+
- ARM GCC toolchain

### Build Steps
```bash
cd ~/Workshop_Computer/releases/19_CA_Sequencer
mkdir build && cd build
cmake ..
make -j4
```

The resulting `CA_Sequencer.uf2` will be in the `build/` folder. Copy it to your Workshop Computer by holding the BOOTSEL button while connecting USB.

## Panel Layout

### Inputs
| Jack | Function |
|------|----------|
| **Pulse 1** | External clock |
| **Pulse 2** | Seed trigger (injects energy into 4 corners) |
| **CV 1** | Seed probability / chaos modulation |
| **CV 2** | Rule morph (0V = Rule 90, ~5V = Rule 150) |

### Outputs
| Jack | Function |
|------|----------|
| **CV 1** | Quantized 1V/oct melody (root = X, scale = Y) |
| **CV 2** | Left/Right grid imbalance |
| **Pulse 1** | Master gate (high when any cell is on) |
| **Pulse 2** | Accent gate (high when center 4 cells active) |
| **Audio 1** | Bitmask of cells 0-7 as voltage levels |

### Controls
| Control | Function |
|---------|----------|
| **Main Knob** | Free-run rate (Up) or clock division (Mid) |
| **X Knob** | Root note (C .. B) |
| **Y Knob** | Scale select: chromatic, major, minor, minor pentatonic, dorian |
| **Switch Up** | Free run (internal clock) |
| **Switch Mid** | External clock |
| **Switch Down** | Reset grid to single seed (momentary) |

### LEDs
Shows the top-left **3x2 window** of the 4x4 grid:
```
[0] [1]    <- row 0
[2] [3]    <- row 1
[4] [5]    <- row 2
```

## How the Algorithm Works

The grid uses a **Von Neumann neighborhood** (4 adjacent neighbors: up, down, left, right).

- **Rule 90**: new state = parity of neighbors (XOR)
- **Rule 150**: new state = center XOR parity of neighbors

The Y knob morphs between these two rules probabilistically.

## Patching Ideas

- **Clock divider**: Patch the A-160-2 (or any clock divider) into Pulse In 1
- **Extract 8 gates**: Use Audio Out 1 with a multi-window comparator or sample & hold to derive individual cell gates
- **Feedback seeding**: Patch Pulse Out 1 or 2 back into Pulse In 2 for self-modifying behavior
- **Quantize CV Out 1**: Send density CV to a quantizer for generative melodies

## License

MIT
