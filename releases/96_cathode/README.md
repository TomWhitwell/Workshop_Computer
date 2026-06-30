# Cathode Ray — Workshop Computer Program Card

A PAL composite video synthesizer. Generates a live black-and-white picture on any composite-input TV or monitor, driven entirely by Eurorack control voltages and audio signals. The picture reacts to what you patch in — sweep an audio waveform across the screen as an oscilloscope trace, or draw freely with two CV sources as X/Y coordinates.

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

The signal is **PAL** (50Hz). Displays locked to NTSC-only will not show it; most modern TVs auto-detect.

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

Both pulse outputs are consumed by the video DAC and are not available as normal pulse outputs while this firmware runs.

---

## Controls

### Main Knob — Mode + Speed

The main knob is a continuous control:

| Position | Mode | Behaviour |
|----------|------|-----------|
| **Lowest ~25% (far CCW)** | Etch-a-sketch | CV In 1/2 draw X/Y. Within this quarter the knob sets the **fade rate** (extreme CCW ≈ 0.15 s to black; top of the quarter ≈ 2 s). |
| **Upper ~75%** | Oscilloscope | Audio In 1 sets the trace height; the sweep speed scales from **slow (~3 s/sweep)** just above the etch zone to **fast (~0.1 s)** at full CW. |

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
| **UP** | Phosphor fade. In scope mode the fade is **locked to the sweep** — a column reaches black just as the sweep returns to it, at any speed. In etch mode the fade rate is set by the main knob (~0.15–2 s). |
| **MIDDLE** | Static — pixels persist. In scope mode each column is cleared just before redrawing (single clean trace); in etch mode drawings accumulate. |
| **DOWN** (momentary) | **Trigger source** (default: cycle the performance effects). Hold to run its assigned behaviour; while held, twist Main/X/Y to open the config menu. |

### Trigger sources & behaviours

There are **three independent trigger sources** — Switch-DOWN, Pulse In 1, Pulse In 2 — each assigned one behaviour in the config menu. They run simultaneously, so you can have three different performance effects on the go at once. Defaults: **DOWN = CYCLE FX**, **PU1 = CYCLE FX**, **PU2 = INVERT**.

| Behaviour | While triggered |
|-----------|-----------------|
| INVERT | Whole image inverted |
| CLS | Clear the screen (on each trigger) |
| CYCLE FX | Advance to the next performance effect on each new trigger; run it while held |
| RANDOM FX | Pick a random effect on each trigger |
| CV FX | Audio In 2 level selects which effect |
| STROBE / FADE / FADEWHITE / SNOW / SWAP / CORRUPT | Run that one fixed effect while held |

The six performance effects: **Strobe** (rapid flash), **Fade** (freeze + fade to black), **Fade-white** (freeze + bloom to white), **Swap** (etch: transpose X/Y · scope: reverse sweep), **Snow** (random static), **Corrupt** (rows shift/tear + noise).

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
| LED 3 | Lit in phosphor fade mode (Switch UP) |
| LED 4 | Lit while Switch DOWN is held (effect / menu) |
| LED 5 | Unused |

---

## Signal Flow

```
Audio In 1 ─────────────────────────► trace height (scope mode)
CV In 1 ──┐
          ├─ + Knob X/Y base ────────► X/Y cursor (etch mode, 48kHz sampled)
CV In 2 ──┘

Main Knob ──────────────────────────► etch (far CCW) | scope speed slow→fast (CW)
Switch ─────────────────────────────► UP=fade  MID=static  DOWN=performance effect (cycles)

Pulse In 1 (gate) ──────────────────► configurable trigger (default: cycle FX)
Pulse In 2 (gate) ──────────────────► configurable trigger (default: invert)

Framebuffer (360×256, 1bpp) ─► PAL word stream ─► PIO/DMA ─► Pu1+Pu2 ─► [resistor DAC] ─► TV
```

---

## Getting Started

1. Wire the resistor DAC: Pu1 via 1kΩ and Pu2 via 220Ω, both into the RCA centre pin; all grounds to the RCA shell.
2. Turn the TV on and select the composite input. You should see a stable black screen immediately. If the picture rolls or there is no sync, recheck the resistors and grounds.
3. Set the Switch to **MIDDLE** (static).
4. Turn the Main Knob to the **upper range** (oscilloscope) and patch any audio into **Audio In 1** — you'll see the waveform sweep across. Knob position sets sweep speed; Knob Y sets the trace gain.
5. Try Switch **UP** (phosphor fade) for a glowing persistence trail that dissolves to black.
6. Turn the Main Knob fully **CCW** (etch-a-sketch). Patch LFOs/CV into **CV In 1** and **CV In 2** — two slightly-detuned sine LFOs draw evolving Lissajous figures. Switch **UP** = Knob X/Y scale the CV; Switch **MIDDLE** = Knob X/Y reposition (offset).
7. Hold **Switch DOWN** for the performance effect; tap it repeatedly to cycle effects. While holding DOWN, twist Knob X or Y to open the **config menu**.
8. Gate **Pulse In 1** / **Pulse In 2** to fire their configured effects (set them in the config menu; e.g. PU1 = CLS to clear).

**Alt boot — screensaver:** hold Switch DOWN while powering on to start in screensaver mode — a bouncing block with phosphor trails, to protect a CRT from burn-in. (More screensaver patterns planned.)

---

## Patch Ideas

**Lissajous figures** — Etch mode, two sine LFOs at slightly different rates into CV In 1/2. Because CV is sampled at 48 kHz, the figures draw as continuous curves, not dotted points.

**Oscilloscope with persistence** — Scope mode + Switch UP. The live sweep leaves a trail that dissolves evenly to black.

**Performance effects** — Tap Switch DOWN to cycle through strobe / freeze-fade / freeze-bloom / swap-reverse / snow / corrupt; hold to run the current one. Great for punctuating a patch live.

**Strobed inversion** — A square LFO or clock into Pulse In 2 flips the image at the LFO rate.

---

## Technical Notes

- **Video format:** PAL composite, progressive (non-interlaced), 360×256 pixels at 50 frames per second.
- **Pixel clock:** 144 MHz ÷ (144/7) = **7.000 MHz** exactly. Each pixel is ~142.857 ns. Line period 64.000 µs (PAL spec 64.00 µs). Frame 312 lines = 50 Hz.
- **2-bit DAC output:** Each pixel is sent as a 2-bit symbol to GPIO 8 (Pu1) and GPIO 9 (Pu2) via PIO `out pins, 2`, summed externally into 3 composite levels (sync / black / white).
- **Core allocation:** Core 1 is dedicated to video (PIO + DMA) for rock-solid sync; Core 0 runs all Eurorack I/O through ComputerCard at 48 kHz and pushes CV samples to Core 1 via a ring buffer.
- **Greyscale:** half-resolution grey buffer (180×128, `GREY_SCALE`-configurable), **5 brightness levels** per cell, expanded each frame into the 1-bit framebuffer via a 2×2 spatial dither whose 4 orientations cycle every 2 frames (averages out the fixed pattern). Scan-out reads the 1-bit framebuffer unchanged. Dilation is **level-aware** — brighter levels are held on longer so the brightness ramp survives the DAC's slow rise (a lone dim pixel isn't flattened to white).
- **Phosphor fade:** grey cells decrement toward true black. In scope mode the fade is locked to the sweep (a column blackens just as the sweep returns); in etch mode the main knob sets the rate (~0.15–2 s).
- **White dilation (analog workaround):** a lone white pixel can't slew to full white through the resistor DAC in one ~143 ns pixel, so it reads grey. After expansion each white pixel is dilated `WHITE_DILATE` pixels to the right, guaranteeing white features are wide enough to render at full brightness. Etch dots are also drawn ≥2 cells wide for the same reason. This trades a little horizontal sharpness for white fidelity — the practical compromise of 1-bit composite.
- **RAM usage:** ~44% of the RP2040's 256 KB: the two double-buffered PAL word streams (~70 KB), the grey buffer (~23 KB) and the framebuffer (~11 KB).
- **Pixels are taller than wide** (portrait) given 360 columns over the ~52µs active line and 256 rows; greyscale cells are 2×2 of these.
