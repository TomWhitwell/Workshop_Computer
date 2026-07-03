# Cathode Ray — Workshop Computer Program Card

A composite video synthesizer (PAL and NTSC builds). Generates a live black-and-white picture on any composite-input TV or monitor, driven entirely by Eurorack control voltages and audio signals. The picture reacts to what you patch in — sweep an audio waveform as an oscilloscope trace, draw freely with two CV sources as X/Y coordinates, or watch a 24-band spectrum analyser dance to Audio In 1. Plus a set of alt-boot screensaver/game modes.

The image is 1-bit (black/white) at the pixel level, rendered through a small **2-bit resistor DAC** built from **Pulse Out 1** and **Pulse Out 2** so the signal has proper composite levels (separate sync, black and white). Drawing happens in a half-resolution greyscale working buffer that is **spatially dithered** into the 1-bit picture, giving **5 apparent brightness levels** (black → white) — enough for a smooth CRT-style phosphor fade. No extra hardware is needed beyond two resistors and a phono (RCA) cable.

---

## Hardware Connection

The Pulse Out jacks output approximately **6V** for a logic HIGH and **0V** for a logic LOW. A composite video input expects roughly **1V peak** into a **75Ω** load, and crucially needs **three** distinct levels: sync (0V), black (a small pedestal above sync), and white (~1V). A single on/off pin can only make two levels — which forces black and sync to the same voltage and gives a TV poor contrast or no sync lock.

Cathode Ray solves this by summing **two** pulse outputs through weighted resistors into the RCA centre pin, making a 2-bit DAC with enough levels for a clean signal.

### Resistor summing network (two resistors)

```
Pulse Out 1 (Pu1) ──[ 1kΩ  ]──┐
                              ├──── RCA centre pin ──── TV composite in
Pulse Out 2 (Pu2) ──[ 220Ω ]──┘
Workshop Computer GND ─────────────── RCA shell    ──── TV composite GND
```

- **Pu1 via 1kΩ** provides the small "black" pedestal step above sync.
- **Pu2 via 220Ω** provides the larger step up to white.
- The **75Ω** TV input is the bottom leg of the divider.
- All grounds are common: 3.5mm sleeve → RCA shell.

The firmware drives both pins together as a 2-bit symbol per pixel, producing:

| Level | Pu1 | Pu2 | Node voltage | Meaning |
|-------|-----|-----|--------------|---------|
| Sync  | low | low | 0V           | horizontal/vertical sync tips |
| Black | high (via 1kΩ) | low | small pedestal | picture black |
| White | high | high | ~1V | picture white |

### Tuning

Exact voltages depend on your resistors and the TV. The white pin (Pu2) is **220Ω** here, chosen so thin/fast white details reach full white quickly; 330Ω–470Ω also work but give softer/greyer fine detail. If black looks too light, raise the 1kΩ slightly. If sync drops out, the black pedestal is too small (1kΩ too high) — bring it back down. All polarity/level tuning in firmware lives in a single `level_pair[]` table in `main.cpp`.

### Building the cable

A short adapter cable is the neatest approach: a male 3.5mm mono jack into Pulse Out 1, a second into Pulse Out 2, both signal wires going through their resistors to the RCA centre pin, all grounds joined to the RCA shell. Heatshrink over the resistors, or mount them on a scrap of stripboard in a small box.

### Which TVs work

Any display with a **composite video input** (usually a yellow RCA phono socket):

- CRT televisions from the 1980s–2000s (the ideal target — phosphor persistence and scanlines suit this kind of video)
- LCD/LED TVs with a yellow composite input
- Portable monitors with composite in
- Video capture devices (Elgato Cam Link, etc.) — for recording or routing onward

The signal is **PAL** (50Hz) by default. Most modern TVs auto-detect; a PAL-only set is fine.

### NTSC build (US / 60Hz displays)

For displays that only accept **NTSC** (common in the US), flash **`cathode_ray_ntsc.uf2`** instead of `cathode_ray.uf2`. It is the *same firmware* — identical features, controls and modes — retimed for NTSC (445 px/line, 262 lines, ~60Hz). The picture is cropped very slightly top and bottom to fit NTSC's fewer scanlines; everything else is identical to the PAL version. (The two are built from one source via a `TV_NTSC` compile switch, so both stay in lockstep.) PAL is the primary/tested build; the NTSC build is a courtesy for those without a multi-standard TV.

---

## Download & install

Both builds live in this program-card folder in the repository:

- **`cathode_ray.uf2`** — **PAL, 50 Hz (the core/primary build — use this unless you specifically need NTSC).**
- **`cathode_ray_ntsc.uf2`** — NTSC, ~60 Hz (the alternative build for US / 60 Hz-only displays).

Repository (both `.uf2` files, source and this README):
**https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/96_cathode**

To flash: hold the Workshop Computer's boot button while connecting USB (it mounts as a `RPI-RP2` drive), then drag the chosen `.uf2` onto it. The module reboots into the firmware. To switch standards later, just flash the other `.uf2`.

---

## Inputs

| Jack | Function |
|------|----------|
| Audio In 1 | Oscilloscope trace (scope mode). Deflects the trace from centre; positive voltage = up. Gain set by Knob Y. |
| CV In 1 | Etch-a-sketch X (etch mode). Scaled/offset by Knob X. Sampled at full 48 kHz. |
| CV In 2 | Etch-a-sketch Y (etch mode). Scaled/offset by Knob Y. Sampled at full 48 kHz. |
| Pulse In 1 | **Trigger source** — runs the behaviour assigned in the config menu (default: CYCLE FX). Set it to CLS for the old screen-clear. |
| Pulse In 2 | **Trigger source** — runs the behaviour assigned in the config menu (default: INVERT while held). |

---

## Outputs

| Jack | Function |
|------|----------|
| Pulse Out 1 | Composite video — DAC bit 0 (via 1kΩ). |
| Pulse Out 2 | Composite video — DAC bit 1 (via 220Ω). |
| CV Out 1 | Alt boot only — mode-dependent (Patchteroids pitch / Boing height / Lunar altitude / …). Unused in the main synth. |
| CV Out 2 | Alt boot only — mode-dependent event pulse (hit / bounce / crash / maze-exit / FourTrig trigger). Unused in the main synth. |

Both pulse outputs are consumed by the video DAC and are not available as normal pulse outputs while this firmware runs.

---

## Controls

### Main Knob — Mode

The main knob is a continuous control split into **three zones**:

| Position | Mode | Behaviour |
|----------|------|-----------|
| **Lower third (CCW)** | Etch-a-sketch | CV In 1/2 draw X/Y. Switch **MIDDLE = Knob X/Y scale the CV** (+ phosphor fade, rate from the knob); Switch **UP = Knob X/Y offset the drawing** (no fade). |
| **Middle third** | Oscilloscope | Audio In 1 traces; sweep speed scales **slow (~3 s) → fast (~0.1 s)** across the zone. Knob Y = gain, Knob X = baseline. Switch **MIDDLE = phosphor fade/persistence**; Switch **UP = clean static trace**. |
| **Upper third (CW)** | Spectrum analyser | Audio In 1 through a 24-band filter bank, bass left → treble right, bars fall/decay (decay speed set by the knob within the zone). Knob Y = gain. Switch **MIDDLE = radial pulsing blob** (Knob X rotates it; leaves a grey echo trail); Switch **UP = LED-segment bargraph**. A SWAP trigger mirrors bass↔treble. |

Mode changes take effect immediately and do not clear the screen.

### Knob X / Knob Y — multi-function

Knob X and Y change job with the mode and switch. All changes use **pickup hysteresis**: when a knob's job changes, that parameter holds its stored value until you physically move the knob past a small threshold — so nothing jumps. Each job keeps its own value.

| Mode | Switch | Knob X | Knob Y |
|------|--------|--------|--------|
| **Etch** | UP | CV In 1 **scale** (0–4×) | CV In 2 **scale** (0–4×) |
| **Etch** | MIDDLE | X **offset** (position) | Y **offset** (inverted) |
| **Scope** | UP / MIDDLE | centre-line (0V) **vertical position** | Audio In 1 **gain** (0–2×) |

Etch position = offset + CV × scale. CV is sampled at the full 48 kHz and every sample is drawn (line-interpolated), so fast gestures draw smooth continuous curves. Scale can boost a small CV (or audio) input as well as attenuate it. (DOWN is reserved for performance effects — see below — so knobs hold their current job while it is held.)

### Switch — Background / Performance

| Position | Behaviour |
|----------|-----------|
| **MIDDLE** | Phosphor fade / persistence. In scope mode the fade is **locked to the sweep** (a column blackens just as the sweep returns); in etch mode the fade rate is set by the main knob (~0.15–2 s) and Knob X/Y **scale** the CV; in spectrum mode = the radial blob. |
| **UP** | Static / clean. In scope mode the column is cleared before each redraw (single clean trace); in etch mode drawings accumulate and Knob X/Y **offset** the drawing; in spectrum mode = the LED bargraph. |
| **DOWN** (momentary) | **Trigger source** (default: cycle the performance effects). Hold to run its assigned behaviour; while held, twist Main/X/Y to open the config menu. |

### Trigger sources & behaviours

There are **three independent trigger sources** — Switch-DOWN, Pulse In 1, Pulse In 2 — each assigned one behaviour in the config menu. They run simultaneously, so you can have three different performance effects on the go at once. Defaults: **DOWN = CYCLE FX**, **PU1 = CYCLE FX**, **PU2 = INVERT**.

| Behaviour | While triggered |
|-----------|-----------------|
| INVERT | Whole image inverted |
| CLS | Clear the screen (on each trigger) |
| CYCLE FX | Advance to the next performance effect on each new trigger; run it while held |
| RANDOM FX | Pick a random effect on each trigger |
| STROBE / FADE / FADEWHITE / SNOW / SWAP / CORRUPT / ROLL | Run that one fixed effect while held |

The seven performance effects: **Strobe** (rapid flash), **Fade** (freeze + fade to black), **Fade-white** (freeze + bloom to white), **Swap** (etch: transpose X/Y · scope: reverse sweep), **Snow** (random static), **Corrupt** (rows shift/tear + noise), **Roll** (vertical screen-roll scroll).

### Config menu

While holding **Switch DOWN**, **twist the Main knob, Knob X, or Knob Y** to open the config menu (the effect stops and a text menu appears). **Main knob** selects the DOWN behaviour, **Knob X** the Pulse In 1 behaviour, **Knob Y** the Pulse In 2 behaviour. Release DOWN to exit; settings are kept (until power-off). A knob only takes control of its setting once you move it a little — so the knob you twist to open the menu won't jump.

---

## LEDs

```
| LED 0   LED 1 |
| LED 2   LED 3 |
| LED 4   LED 5 |
```

| LED | Function |
|-----|----------|
| LED 0 | Lit in oscilloscope mode |
| LED 1 | Lit in etch-a-sketch mode |
| LED 2 | Lit while the config menu is open |
| LED 3 | Lit in phosphor fade / persistence mode (Switch MIDDLE) |
| LED 4 | Lit while Switch DOWN is held (effect / menu) |
| LED 5 | Lit in spectrum-analyser mode |

---

## Signal Flow

```
Audio In 1 ─────────────────────────► trace height (scope mode)
CV In 1 ──┐
          ├─ + Knob X/Y base ────────► X/Y cursor (etch mode, 48kHz sampled)
CV In 2 ──┘

Main Knob ──────────────────────────► etch (CCW) | scope (mid) | spectrum (CW)
Switch ─────────────────────────────► UP=fade  MID=static  DOWN=performance effect (cycles)

Pulse In 1 (gate) ──────────────────► configurable trigger (default: cycle FX)
Pulse In 2 (gate) ──────────────────► configurable trigger (default: invert)

Framebuffer (360×256, 1bpp) ─► PAL word stream ─► PIO/DMA ─► Pu1+Pu2 ─► [resistor DAC] ─► TV
```

---

## Getting Started

1. Wire the resistor DAC: Pu1 via 1kΩ and Pu2 via 220Ω, both into the RCA centre pin; all grounds to the RCA shell.
2. Turn the TV on and select the composite input. You should see a stable black screen immediately. If the picture rolls or there is no sync, recheck the resistors and grounds.
3. Set the Switch to **UP** (static/clean).
4. Turn the Main Knob to the **middle range** (oscilloscope) and patch any audio into **Audio In 1** — you'll see the waveform sweep across. Knob position sets sweep speed; Knob Y sets the trace gain.
5. Try Switch **MIDDLE** (phosphor fade) for a glowing persistence trail that dissolves to black.
6. Turn the Main Knob fully **CCW** (etch-a-sketch). Patch LFOs/CV into **CV In 1** and **CV In 2** — two slightly-detuned sine LFOs draw evolving Lissajous figures. Switch **MIDDLE** = Knob X/Y scale the CV; Switch **UP** = Knob X/Y reposition (offset).
7. Hold **Switch DOWN** for the performance effect; tap it repeatedly to cycle effects. While holding DOWN, twist Knob X or Y to open the **config menu**.
8. Gate **Pulse In 1** / **Pulse In 2** to fire their configured effects (set them in the config menu; e.g. PU1 = CLS to clear).

## Alt boot — screensaver / performance modes

Hold **Switch DOWN while powering on** to start in **alt-boot** mode: a set of screensaver / performance-tool hybrids that also keep a CRT from burning in.

- **Switch UP** shows the **selector** — the current mode name, a position counter, and its input/output help. Turn the **Main knob** to scroll through the modes.
- **Switch MIDDLE or DOWN** runs the shown mode.

Boot straight into MID/DOWN (without visiting the selector) and you get **COMET**, the default.

| # | Mode | What it is |
|---|------|-----------|
| 1 | **COMET** | A round comet with a phosphor tail, bouncing around. Main / CV1 = speed, Knob Y / CV2 = tail length. |
| 2 | **PATCHTEROIDS** | Patch-controlled Asteroids (see below). |
| 3 | **BOING** | Amiga-style rotating checkered ball (see below). |
| 4 | **STARFIELD** | Fly through a 3D starfield. Main = speed; Knob X / CV1 = horizontal turn, Knob Y / CV2 = vertical turn. |
| 5 | **RADAR** | A radar scope (see below). |
| 6 | **LUNAR** | Lunar Lander (see below). |
| 7 | **3DMAZE** | First-person wireframe maze — find the exit (see below). |
| 8 | **FOURTRIG** | Four trigger inputs stamp decaying "things" into four screen quadrants (see below). |

Each mode shows its own control map on the selector page, so you don't need to memorise them. Highlights of the interactive ones:

### Patchteroids

A tiny Asteroids game, and a simple melodic sequencer. A ship cruises forward and wraps around; **Main knob (+ CV In 1) steers**, **PU1 / Switch DOWN fires**. Shooting a big comet splits it in two; the count grows the longer you survive. **CV Out 1 = pitch** (rises one semitone per hit, arpeggiates down on a crash while the HITS score shows); **CV Out 2 = gate** on each hit. Patch CV Out 1 → osc v/oct and CV Out 2 → envelope to play it as a sequence.

### Boing

A checkered ball spins and bounces under gravity. **Knob X / CV In 1 = spin + kick impulse**, **Main / CV In 2 = bounce efficiency** (centre = bounces forever, CW gains height, CCW decays to rest), **Knob Y = horizontal speed**, **PU1 / Switch DOWN = kick** (launch strength set by the spin control). **CV Out 1 = ball height**, **CV Out 2 = a trigger on each floor bounce** — with the ball decaying it makes a shortening bounce-ball rhythm (Peaks-style). 

### Radar

A radar scope with a sweeping hand and a rotating turet. **Main knob aims** the turret; **PU1 / Switch DOWN fires** a ballistic missile (hold = launch power → range); **PU2 IN places** fading target blocks on the sweep line, with **CV In 1 setting their radius** from centre. **CV Out 2 pulses on a hit.**

### Lunar

Classic Lunar Lander. **Main / CV In 1 rotate** the craft; **PU1 / Switch DOWN thrust** (against constant gravity). Land gently and upright on the flat pad (which narrows each stage) while avoiding drifting UFOs; land → next stage, crash → back to stage 1. Fuel is limited (empty = no thrust). **CV Out 1 = altitude**, **CV Out 2 = a pulse on land/crash**.

### 3D Maze

A chunky first-person **wireframe maze** (ZX81 3D Monster Maze style). **Main knob turns** (smoothly), **PU1 / Switch DOWN walks** forward; movement is rail-locked to corridor centres. Somewhere in the maze a **glowing white EXIT panel** sits on a wall — **find it**: reaching it flashes the screen and generates a fresh maze. Turn **Knob X up (or patch CV In 1)** for a hands-free **auto-run** that drives itself through the maze, turning only where it must. **Hold Pulse In 2** to invert the screen (white-on-black) — a momentary performative accent. **CV Out 2 pulses when you reach the exit.**

### FourTrig

A trigger-driven **visual "drum machine"**. All **four inputs are triggers** — **Audio In 1, Audio In 2, Pulse In 1 and Pulse In 2** — and each owns one screen quadrant (Audio 1 = top-left, Audio 2 = top-right, PU1 = bottom-left, PU2 = bottom-right). On a trigger, that quadrant gets a "thing" **stamped** in near its centre, which then **decays through the five greys to black**. Audio inputs fire on a rising transient/gate (crossing roughly +0.4 V), so kicks, claps and gates all work; pulses fire on their edge.

**Knob X selects the bank** of things — icon/picture banks come first, word banks last:

- **SHAPES** — vector shapes: circle/square/triangle/star, psychic-test-card symbols, arrows, weather icons, card suits.
- **MUSIC / HITS** — abstract sound glyphs (note, lightning bolt, explosion, echo rings, discs, targets).
- **SYMBOLS** — more figurative icons: faces (happy/flat/sad/shock), check/cross/plus/target, house/eye/crescent-moon/star, arrows, and more.
- **WORDS** — drum/shout words (HAT · CLAP · KICK · SNARE, and four more sets).
- **EMPHASIS** — punchy text stabs (YEAH! · NOPE · !!! · BANG …).

**Knob Y (+ CV In 2)** picks which **set of four** within the bank (five sets, one per quadrant slot). **Main knob (+ CV In 1) = CHAOS**: at zero, things land tidily near the middle of the screen at a modest uniform size with no glitching. As you turn it up, placement gets **jittered and occasionally swapped between quadrants**, the icons/words **grow larger and their size varies**, and **past ~50%** each hit has a growing chance of **also firing a random VFX** on top of the stamp — the full performance set (strobe, invert, fade-to-black, fade-to-white, snow, corrupt, roll, and **flip 180°**). Shapes and words are drawn with thick (2×2) strokes. **Hold Switch DOWN for a momentary held VFX** (a random effect from that same set, as in normal boot). **CV Out 2 pulses on every trigger** so you can chain the visual hits back into your patch.

---

## Patch Ideas

**Lissajous figures** — Etch mode, two sine LFOs at slightly different rates into CV In 1/2. Because CV is sampled at 48 kHz, the figures draw as continuous curves, not dotted points.

**Oscilloscope with persistence** — Scope mode + Switch UP. The live sweep leaves a trail that dissolves evenly to black.

**Performance effects** — Tap Switch DOWN to cycle through strobe / freeze-fade / freeze-bloom / swap-reverse / snow / corrupt; hold to run the current one. Great for punctuating a patch live.

**Strobed inversion** — A square LFO or clock into Pulse In 2 flips the image at the LFO rate.

---

## Technical Notes

- **Video format:** composite, progressive (non-interlaced), 360×256 framebuffer. **PAL** build = 448 px/line, 312 lines, 50 Hz (`cathode_ray.uf2`). **NTSC** build = 445 px/line, 262 lines, ~60 Hz, scanning a centred 240-row crop of the same framebuffer (`cathode_ray_ntsc.uf2`). Both share one source; only an `#ifdef TV_NTSC` timing block differs, so all drawing/features are identical.
- **Pixel clock:** 144 MHz ÷ (144/7) = **7.000 MHz** exactly (both formats). Each pixel is ~142.857 ns. PAL line 64.000 µs; NTSC line 63.571 µs (target 63.556).
- **2-bit DAC output:** Each pixel is sent as a 2-bit symbol to GPIO 8 (Pu1) and GPIO 9 (Pu2) via PIO `out pins, 2`, summed externally into 3 composite levels (sync / black / white).
- **Core allocation:** Core 1 is dedicated to video (PIO + DMA) for rock-solid sync; Core 0 runs all Eurorack I/O through ComputerCard at 48 kHz and pushes CV samples to Core 1 via a ring buffer.
- **Greyscale:** half-resolution grey buffer (180×128, `GREY_SCALE`-configurable), **5 brightness levels** per cell, expanded each frame into the 1-bit framebuffer via a 2×2 spatial dither whose 4 orientations cycle every 2 frames (averages out the fixed pattern). Scan-out reads the 1-bit framebuffer unchanged. Dilation is **level-aware** — brighter levels are held on longer so the brightness ramp survives the DAC's slow rise (a lone dim pixel isn't flattened to white).
- **Phosphor fade:** grey cells decrement toward true black. In scope mode the fade is locked to the sweep (a column blackens just as the sweep returns); in etch mode the main knob sets the rate (~0.15–2 s).
- **White dilation (analog workaround):** a lone white pixel can't slew to full white through the resistor DAC in one ~143 ns pixel, so it reads grey. After expansion each white pixel is dilated `WHITE_DILATE` pixels to the right, guaranteeing white features are wide enough to render at full brightness. Etch dots are also drawn ≥2 cells wide for the same reason. This trades a little horizontal sharpness for white fidelity — the practical compromise of 1-bit composite.
- **RAM usage:** ~77% of the RP2040's 256 KB: the two double-buffered word streams (~70 KB), the grey buffer (~23 KB), the framebuffer (~11 KB), plus the alt-boot modes' state (the Boing sphere lookup, maze grid + face list, etc). FLASH ~4%.
- **Pixels are taller than wide** (portrait) given 360 columns over the ~52µs active line and 256 rows; greyscale cells are 2×2 of these.

---

## Changelog

### v1.2.0
- **Spectrum analyser** — the Main knob is now split into thirds: etch (lower), oscilloscope (middle), and a new **24-band audio spectrum analyser** (upper), driven by Audio In 1. Switch MIDDLE = radial pulsing blob (Knob X rotates it, leaves a grey echo trail); Switch UP = LED-segment bargraph. Bars decay (speed from the knob); a SWAP trigger mirrors bass↔treble.
- **NTSC build** — `cathode_ray_ntsc.uf2` for US / 60 Hz displays, built from the same source via a `TV_NTSC` compile switch (identical features; a small top/bottom crop). PAL remains the default `cathode_ray.uf2`.
- **Switch UP↔MIDDLE swapped** across all normal modes (DOWN unchanged): persistence/fade behaviours are now on MIDDLE, static/clean on UP.
- **Spectrum has its own Knob X/Y** (rotate & gain) with pickup hysteresis — independent of the oscilloscope's baseline/gain, and only changing when actually turned.
- **3DMAZE: find the EXIT** — the roaming monster is replaced by a glowing white **EXIT panel** on a wall; reach it to flash + generate a new maze (CV Out 2 pulses on reaching the exit).
- **NTSC aesthetic polish** — content nudged in from the top/bottom crop so nothing clips, and a slightly **shorter 3D-maze wall height** (NTSC only) so more of the receding top/bottom diagonals show. PAL is unchanged.
- **New alt-boot mode: FOURTRIG** — a trigger-driven visual drum machine. Audio In 1/2 and Pulse In 1/2 are all triggers, each stamping a decaying "thing" (thick 2×2 strokes) into its own screen quadrant. Knob X picks the bank (icons first: shapes / music-hits / symbols; then words / emphasis), Knob Y (+CV2) the set, and Main (+CV1) a CHAOS amount that jitters/swaps placement, grows and varies the size, and past ~50% adds a rising chance of a per-hit random VFX (the full set incl. flip-180°). Hold Switch DOWN for a momentary held VFX. CV Out 2 pulses on every trigger.
- Developer docs added (`CATHODE_DEV.md`, `MODES.md`).

### v1.1.0
- **Alt boot is now a selector of seven screensaver / performance modes** (was one screensaver). Switch UP scrolls with the Main knob and shows per-mode I/O help; MID/DOWN plays.
  - New modes: **STARFIELD** (fly/steer through space), **RADAR** (aim + ballistic missile, PU2 places targets), **LUNAR** (Lunar Lander with stages, UFOs and fuel), **3DMAZE** (first-person wireframe maze with a roaming monster + hands-free auto-run).
  - **COMET** reworked into a round comet with speed (Main/CV1) and tail-length (Y/CV2) control; it is now the default mode.
  - **BOING** gained CV1-out (height), a kick input (PU1/DOWN) with spin-set impulse, CV2-modulated bounce efficiency, and a Peaks-style shortening bounce-trigger on CV Out 2.
  - **Patchteroids** unchanged in play; the CV bridge is now hybrid-aware so each mode drives the CV outs appropriately.
- **CV outputs in alt boot**: CV Out 1 / CV Out 2 are now mode-dependent (pitch/height/altitude/sweep and hit/bounce/crash/caught pulses).
- Removed the **CV FX** config-menu behaviour (little-used); effect list now includes **ROLL**.
- Font polish (M/N/W legibility), a **slash** glyph, and text rendered with reduced dilation for crisp menus.
- **3DMAZE**: smooth rail-locked auto-run (no cell-hopping) and **Pulse In 2 = invert** accent.
- Panel overlay image added.

### v1.0.0
- Initial release: PAL composite video synth — oscilloscope + etch-a-sketch, 5-level greyscale via dithering, three independent configurable performance triggers with an on-screen config menu, and a single alt-boot screensaver (Patchteroids).
