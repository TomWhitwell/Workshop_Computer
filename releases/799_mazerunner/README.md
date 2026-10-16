# Maze Runner

Generates a maze, then solves it **twice** with two independently chosen algorithms to drive two voices at once. Path A (CV/Pulse Out 1) and Path B (CV/Pulse Out 2) each turn their solved route into music:

- Every turn in the route fires a gate.
- The length of each straight run sets how many clock steps pass before the next turn.
- Each turn moves the pitch by some number of scale degrees, quantized to a scale.
- Reaching the exit reverses that path and plays it back (ping-pong), looping forever.

The two paths reach their ends on their own schedules, and Path B can also run at its own clock multiply/divide. Loops of different lengths phase against each other over time.

A maze is generated at power-on and whenever you regenerate (see below). Firmware is `src/build/mazerunner.uf2`.

## Controls

The switch selects a **bank**. While it sits in a position, Main / X / Y control that bank's parameters.

| Knob | Switch Up | Switch Middle (default) | Switch Down |
|---|---|---|---|
| Main | Generation algorithm: Prim / Kruskal / Wilson / DFS | Tempo, 40-400 BPM (internal clock) | Maze size *(pending)* |
| X | Path A solve algorithm (5) | Quantizer scale (10), shared by both paths | Braid amount 0-100% *(pending)* |
| Y | Path B solve algorithm (5) | Maze-to-CV mapping mode (3), shared | Path B clock multiply/divide (21), live |

**Regenerate:** quickly press and release the switch Down (under about 300 ms), or send a trigger to CV In 2. This builds a new maze, re-solves both paths and restarts playback. Holding Down longer to adjust its bank does *not* regenerate.

**Pending** settings preview live (LEDs, web app) but only take effect on the next regenerate. Everything else takes effect immediately.

**Knob pickup:** each parameter locks to its current value when you enter a bank, and the knob does nothing until you turn it to that value. This is normal. Sweep the knob once and it catches.

## Jacks

| Jack | Function |
|---|---|
| Pulse In 1 | Clock. When nothing is patched, the internal clock (tempo knob) runs. Patching any cable takes over, whether or not it carries pulses. |
| Pulse In 2 | Reset. Restarts both paths from their start; does not rebuild the maze. |
| CV In 1 | Live addition to the quantizer scale, in any bank. |
| CV In 2 | Regenerate + reset trigger (fires above ~1.2 V). Not a modulation input. |
| Audio In 1 | Live addition to Path B's clock multiply/divide, in any bank. |
| Audio In 2 | Live addition to the maze-to-CV mapping mode, in any bank. |
| Pulse Out 1 | Path A gate: fires on each turn. |
| Pulse Out 2 | Path B gate: fires on each turn. |
| CV Out 1 | Path A pitch (calibrated). |
| CV Out 2 | Path B pitch (calibrated). Independent of Path A, not a harmony. |
| Audio Out 1 | Sum of Path A + Path B pitch (uncalibrated). |
| Audio Out 2 | Absolute difference of Path A and Path B pitch (uncalibrated, never negative). |

Audio Out 1/2 each have their own slew: Off (default) / Fast (~30 ms) / Medium (~200 ms) / Slow (~1.4 s). Slew is set in the web app only.

**LEDs**

| LED | Meaning |
|---|---|
| 0 | Blinks on every main clock tick |
| 1 | Path A direction (lit = forward) |
| 2 | Held bank: off = Middle, half = Up, full = Down |
| 3 | Headline value for the held bank (generation algorithm / scale / braid) |
| 4 | Flashes when Path A turns |
| 5 | Flashes when Path B turns |

All six flash together briefly when a regenerate fires.

## Parameters in detail

**Generation algorithms:** Prim, Kruskal, Wilson, DFS backtracker.

**Braid:** at 0% the maze has exactly one route, so both paths find the same route and play in unison. Higher braid opens dead ends into loops, giving multiple routes, which is what lets the paths diverge. Braid is also what makes the loop-based melodies.

**Solve algorithms (per path):**
- **Direct:** shortest path.
- **Wall-follower:** right-hand rule; explores dead ends, so usually much longer.
- **Tremaux's:** marks corridors as it goes; correct even with loops.
- **Pledge:** walks toward the exit and wall-follows when blocked; escapes loops that trap wall-following.
- **Random walk:** chaotic, with a light pull toward the exit.

If any solver fails to reach the exit within its step budget, that path silently falls back to Direct.

**Mapping mode (both paths):**
- **Direct:** each turn moves exactly 1 scale degree.
- **Run-length-as-leap:** a turn moves by the length of the run just finished, capped at 7 degrees.
- **Cross-path leap:** as above, but sized from the *other* path's most recently finished run.

**Scales:** Major, Natural minor, Dorian, Mixolydian, Harmonic minor, Major pentatonic, Minor pentatonic, Blues, Whole tone, Chromatic. The root is fixed at C3. Changes apply immediately, mid-sequence.

**Path B clock ratio:** ×16 ×13 ×11 ×8 ×7 ×6 ×5 ×4 ×3 ×2 ×1 ÷2 ÷3 ÷4 ÷5 ÷6 ÷7 ÷8 ÷11 ÷13 ÷16 (default ×1). Changing the ratio mid-play can cause one slightly off-timed step; a reset on Pulse In 2 re-syncs it. With an external clock, tempo changes reach Path B's multiplied steps after up to one beat.

**Internal clock:** tempo changes take effect from the next beat.

## Web app

`web/index.html` is a live visualizer and settings page. It needs Chrome or Edge (Web MIDI), and a USB-C data cable to the card. Open it, pick the card and click Connect.

It shows the maze with both paths (Path A solid, Path B dashed) and a live cursor for each, plus a settings panel.

- **Generator, Solver A/B, Maze size and Braid** show *current* (what's playing now) and *pending* (what the next regenerate will apply, highlighted yellow when different).
- **Tempo, Mapping mode and Path B ratio** show live values.
- **Scale** is a dropdown. You can also set it with the knob or CV In 1, and the most recent change wins.
- **Sum slew / Diff slew** dropdowns set Audio Out 1/2 slew.

## Notes

- Nothing is saved to flash: scale, slew and all other settings reset on power-up (scale to Major, slew to Off).
- Maze sizes go up to 16 × 16.
- Knob response curves and the CV In 2 trigger threshold are first guesses and may want tuning by ear.

## Building

Source is in `src/` (Pico SDK, ComputerCard v0.3.0 vendored as `src/ComputerCard.h`; USB-MIDI via TinyUSB). Build with the standard Pico SDK CMake flow using `src/CMakeLists.txt`. `src/sysex_protocol.h` documents the USB message format used by the web app.
