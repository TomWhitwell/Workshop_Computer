# Cathode Ray — control cheat-sheet

Quick reference for every mode. **DOWN** = the performance-trigger / config-menu path in all
normal modes (hold + twist Main/X/Y to open the menu). Jacks: **Pu Out 1/2** are the video
DAC; **CV Out 1/2** are only used in alt-boot modes.

## Normal boot — Main knob picks the mode (three zones)

| Main knob | Mode | Knob X | Knob Y | Switch UP | Switch MIDDLE |
|-----------|------|--------|--------|-----------|---------------|
| Lower (CCW) | **Etch-a-sketch** | offset (UP) / CV1 scale (MID) | offset (UP) / CV2 scale (MID) | X/Y **offset**, drawings accumulate | X/Y **scale** CV (+ fade, rate from knob) |
| Middle | **Oscilloscope** | trace baseline | trace gain | **static** clean trace | **fade** / persistence |
| Upper (CW) | **Spectrum analyser** | rotate blob (MID only) | gain | **LED bargraph** | **radial** pulsing blob (grey echo) |

- **Etch:** CV In 1 → X, CV In 2 → Y (sampled 48 kHz, drawn as continuous lines).
- **Scope:** Audio In 1 → trace; Main knob within the zone sets sweep speed (~3 s … 0.1 s).
- **Spectrum:** Audio In 1 → 24-band analyser, bass left → treble right; Main within the zone
  sets decay speed; a **SWAP** trigger mirrors bass↔treble.

LEDs: 0 = scope, 1 = etch, 5 = spectrum, 2 = config menu open, 3 = fade/persistence (MIDDLE),
4 = Switch DOWN held.

## Performance triggers (any normal mode)
Three independent sources, each assigned a behaviour in the config menu:
**Switch DOWN**, **Pulse In 1**, **Pulse In 2**. Behaviours: INVERT · CLS · CYCLE FX ·
RANDOM FX · STROBE · FADE · FADEWHITE · SNOW · SWAP · CORRUPT · ROLL.
Defaults: DOWN = CYCLE FX, PU1 = CYCLE FX, PU2 = INVERT.
Config menu: hold DOWN + twist Main (→ DOWN's behaviour) / X (→ PU1) / Y (→ PU2).

## Alt boot — hold Switch DOWN at power-on
Switch **UP** = selector (Main scrolls the modes, on-screen help). Switch **MID/DOWN** = play.
Default mode = COMET. Each mode's on-screen help lines:

| Mode | Controls (in / out) |
|------|---------------------|
| **COMET** | MAIN/CV1 : speed · Y/CV2 : tail length |
| **PATCHTEROIDS** | MAIN/CV1 : steer · PU1/DOWN : fire · OUT1 : pitch · OUT2 : gate |
| **BOING** | X/CV1 : spin + kick impulse · MAIN/CV2 : bounce · Y : h-speed · PU1/DOWN : kick · OUT1 : height · OUT2 : bounce |
| **STARFIELD** | MAIN : speed · X/CV1 : turn horizontal · Y/CV2 : turn vertical |
| **RADAR** | MAIN : aim · PU1 : fire (hold = power) · PU2/CV1 : place enemy (CV1 = radius) · OUT2 : hit |
| **LUNAR** | MAIN/CV1 : rotate · PU1/DOWN : thrust · OUT1 : altitude · OUT2 : crash |
| **3DMAZE** | MAIN : turn · PU1 : forward · X/CV1 : auto-run · PU2 : invert · OUT2 : reached exit |
| **FOURTRIG** | IN : Audio 1 / Audio 2 / PU1 / PU2 (4 triggers) · X : bank · Y/CV2 : set · MAIN/CV1 : chaos · DOWN : held glitch · OUT2 : trig |

Notes:
- **PATCHTEROIDS** — shoot comets (they split); pitch climbs per hit, arpeggiates down on a
  crash. Patch OUT1→osc v/oct, OUT2→envelope to play it as a sequence.
- **BOING** — Main centre = bounces forever, CW gains height, CCW decays to rest. OUT2 makes
  a Peaks-style shortening bounce rhythm.
- **RADAR** — hold to charge missile range; PU2 stamps fading target blocks on the sweep.
- **LUNAR** — land gently & upright on the pad (narrows each stage), avoid UFOs; limited fuel.
- **3DMAZE** — first-person wireframe maze; **find the white EXIT panel** on a wall (reaching
  it flashes and generates a new maze). Turn Knob X up (or patch CV1) for hands-free auto-run;
  hold PU2 to invert as an accent.
- **FOURTRIG** — a trigger-driven visual "drum machine". **Audio In 1, Audio In 2, Pulse In 1
  and Pulse In 2** are all trigger inputs; each stamps a "thing" (thick 2×2 strokes) into its
  own screen quadrant (TL/TR/BL/BR), which then decays through the five greys to black. **Knob X**
  picks the bank — icons first, words last: SHAPES / MUSIC-HITS / SYMBOLS / WORDS / EMPHASIS;
  **Knob Y (+CV In 2)** picks the set of four within the bank; **Main (+CV In 1)** is CHAOS — at
  zero, tidy placement near screen centre at a modest size; turning it up adds position jitter,
  quadrant swapping, and grows/varies the size, and past ~50% a rising chance each hit also fires
  a random VFX from the full performance set (strobe / invert / fade-black / fade-white / snow /
  corrupt / roll / flip-180°). **Hold Switch DOWN** for a momentary held VFX (a random one of that
  set). CV Out 2 pulses on every trigger. (Audio triggers fire on a transient/gate crossing ~+0.4 V.)

All alt-boot modes also serve as CRT-friendly screensavers.
