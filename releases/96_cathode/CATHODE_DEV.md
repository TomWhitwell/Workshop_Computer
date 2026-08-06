# Cathode Ray — developer handbook

Internal notes for building, modifying and releasing the Cathode Ray firmware. The user-
facing docs are in `README.md`; this file is the "how it works / how to work on it" reference.

Everything lives in a single **`main.cpp`** (plus `composite.pio`, `CMakeLists.txt`, and a
self-contained `ComputerCard.h`). It's a 1-bit composite-video synth for the Music Thing
Workshop Computer (RP2040).

---

## 1. Architecture (two cores)

| Core | Role |
|------|------|
| **Core 0** | ComputerCard `ProcessSample()` ISR @ 48 kHz. Reads all Eurorack I/O, runs knob-pickup, publishes the volatile `shared` struct, pushes CV into the etch ring and audio into the audio ring, drives LEDs and (in alt mode) the CV outputs. Do **no** heavy work here. |
| **Core 1** | Dedicated video loop. Owns PIO0/SM0 + a DMA channel. Each frame: `update_framebuffer()` draws into the **grey buffer** → `expand_grey_to_fb()` dithers it into the 1-bit **framebuffer** → `build_frame_words()` packs sync+blank+active into the DMA word stream → swap buffers at vblank. All drawing/DSP that isn't per-sample happens here. |

The two cores talk **only** through the `volatile SharedState shared` struct. Core 0 writes
inputs; Core 1 reads them and (for alt-mode CV out) writes back a few fields that Core 0 reads.

### Output — the 2-bit resistor DAC
GPIO8 (Pulse Out 1) via **1 kΩ** + GPIO9 (Pulse Out 2) via **220 Ω**, summed into the RCA
centre pin; grounds common. Pulse Out 2 is consumed by video and is not a normal output.
The PIO shifts 2 bits/pixel to those pins. `level_pair[]` (near the top of main.cpp) is the
**one place** to tune polarity/levels:
- `SYNC = 0b11` → both jacks LOW → 0 V (sync tip)
- `BLACK = 0b10` → Pu1 only, via 1 kΩ → a small pedestal **above** sync (must stay > sync or
  the TV loses lock)
- `WHITE = 0b00` → both HIGH → brightest

Only 3 of the 4 levels are used → effectively a crisp 1-bit black/white picture.

---

## 2. Video geometry & the PAL / NTSC split

- **Framebuffer** 360×256, 1 bpp (`FB_WIDTH/FB_HEIGHT/FB_STRIDE/FB_SIZE`).
- **Grey buffer** 180×128, 5 levels (`GREY_SCALE=2`, `GREY_LEVELS=5`, `GREY_W/GREY_H`). All
  drawing is done here; `GREY_H` is **128 for both PAL and NTSC** — never changes.
- `expand_grey_to_fb()`: 2×2 spatial dither (5 levels, 4 orientations cycling every 2 frames
  to average the pattern) + **level-aware right white-dilation** (`dilate_amount[]`, capped
  per-frame by the global `dilate_cap`; `text_mode` disables it for crisp menus).

### PAL vs NTSC — one `#ifdef TV_NTSC` block
All timing divergence lives in a single block near the top of main.cpp. Everything else
(framebuffer, grey buffer, every mode) is identical, so **PAL edits flow to NTSC for free**.

| | PAL (default) | NTSC (`-DTV_NTSC`) |
|---|---|---|
| Line | 448 px (fp12/hs33/bp40/av363), 64.0 µs | 445 px (fp10/hs33/bp32/av370), 63.57 µs |
| Frame | 312 lines, 50 Hz | 262 lines, ~60 Hz |
| Active | 256 rows (all) | 240 rows = a centred crop of the 256-row FB (`TV_ACTIVE_ROW0=8`) |

- Both use the **same 7.000 MHz pixel clock** (clkdiv 144/7, sys 144 MHz) — no PLL change.
- Frame-structure macros are format-neutral (`TV_VSYNC_LINES`, `TV_BLANK_TOP/BOT`,
  `TV_ACTIVE_LINES`, `TV_ACTIVE_ROW0`, `TV_TOTAL_LINES`) so `build_frame_words()` is shared.
- **`FRAME_WORDS` is format-exact** and drives the DMA transfer count — it MUST equal the real
  words/frame or the refresh rate/sync is wrong. Buffers are sized to `FRAME_WORDS_MAX` (PAL).
- VSYNC is a simplified broad-pulse block (no equalising pulses). Fine for both; a fussy NTSC
  set might want the line count/width nudged — all inside the `#ifdef`.

### The analog white-fidelity gotcha (important)
A lone ~143 ns white pixel can't slew to full white through the resistor DAC → it reads grey.
`WHITE_DILATE` holds each white pixel high for several px to the right so features reach true
white (measured: ~5 px needed). This is why single-pixel data (e.g. teletext) can't work in
firmware — it needs a hardware bandwidth fix.

---

## 3. Building

Toolchain (Bash / Git-Bash):
```
export CMAKE=/c/Users/andyu/.pico-sdk/cmake/v3.31.5/bin/cmake.exe
export PICO_SDK_PATH=/c/Users/andyu/.pico-sdk/sdk/2.2.0
export PICO_TOOLCHAIN_PATH=/c/Users/andyu/.pico-sdk/toolchain/14_2_Rel1
export NINJA=/c/Users/andyu/.pico-sdk/ninja/v1.12.1/ninja.exe
export PATH="$PICO_TOOLCHAIN_PATH/bin:$(dirname $CMAKE):$(dirname $NINJA):$PATH"
cd releases/64_cathode/build
# after any CMakeLists change, reconfigure first:
$CMAKE -G Ninja -S .. -B .
# definitive build (rm forces a real compile so the size report is accurate):
rm -f CMakeFiles/cathode_ray.dir/main.cpp.obj cathode_ray.elf cathode_ray.uf2
$CMAKE --build . --target cathode_ray            # PAL  → cathode_ray.uf2
$CMAKE --build . --target cathode_ray_ntsc       # NTSC → cathode_ray_ntsc.uf2
cp cathode_ray.uf2 ../ ; cp cathode_ray_ntsc.uf2 ../
```
Both targets are built from the same `main.cpp`; NTSC just adds `-DTV_NTSC` (see CMakeLists).
Typical footprint: FLASH ~4 %, RAM ~77 % of the RP2040. RAM is the constraint to watch — see
§6.

---

## 4. Mode systems

### Normal boot — Main knob split in thirds
`main_mode(knob)` → ETCH (< `ETCH_THRESH` 1365) / SCOPE (< `SPECTRUM_THRESH` 2730) / SPECTRUM.

**Switch UP↔MIDDLE are swapped in normal modes** (DOWN unchanged). This is done with a
normal-mode-only swapped view (`nsw` in update_framebuffer, `nswp` in the Core-0 pickup),
NOT at the source — so alt-boot's physical-UP selector and the games are unaffected. If you
add a normal-mode switch check, use `nsw`/`nswp`, not raw `sw`.

- **ETCH**: CV1/CV2 draw X/Y. MID = Knob X/Y scale the CV (+ fade); UP = X/Y offset.
- **SCOPE**: Audio In 1 waveform. MID = phosphor fade; UP = clean static. Knob Y = gain,
  Knob X = baseline (via the pickup system).
- **SPECTRUM**: 24-band integer **Goertzel** bank (`spec_coeff[]` Q13, log ~80 Hz–8 kHz) run
  once/frame over the last 512 audio samples. `spec_tilt[]` tames the bass. Bars decay (fall
  speed from the knob position within the zone). MID = radial pulsing blob (Knob X rotates it,
  grey echo trail); UP = LED-segment bargraph. A SWAP trigger reverses the bins. Spectrum
  reads `shared.knob_x/knob_y` **directly** (its own gentle gain + X-rotate), bypassing pickup.

### Performance triggers + config menu (Switch DOWN)
Three independent sources — Switch-DOWN, Pulse In 1, Pulse In 2 — each with its own
`FxState`, each assigned a behaviour in the on-screen config menu. `apply_behaviour()` /
`run_fx()`. Behaviours: INVERT, CLS, CYCLE FX, RANDOM FX, then fixed STROBE/FADE/FADEWHITE/
SNOW/SWAP/CORRUPT/ROLL. Hold DOWN + twist Main/X/Y to open the menu. `effect_invert` is the
single whole-frame invert path.

### Alt boot — selector of screensaver/performance hybrids
Hold Switch DOWN at power-on (`shared.alt_mode` latched after the ADC settles). Switch UP =
selector (Main scrolls, per-mode help from `ALT_HELP[][5]`); MID/DOWN = play.

**To add an alt-boot mode:** add a name to `ALT_NAMES[]`, a row to `ALT_HELP[][5]`, a `case`
in the dispatch `switch` in update_framebuffer's alt branch, and a `screensaver_*()` function.
That's it.

CV bridge (Core 0, only while PLAYING so the selector isn't CV-polluted):
`shared.alt_cv1` → CV Out 1 (mode-dependent), `shared.ast_gate_seq` → a CV Out 2 event pulse,
`shared.ast_fire` = PU1 latch. `PATCHTEROIDS` (index `ALT_HYBRID_PATCH`) has a special bridge
(CV1 folds into the steer knob, CVOut1 = pitch); other modes read Main/CV raw.

Modes: COMET, PATCHTEROIDS, BOING, STARFIELD, RADAR, LUNAR, 3DMAZE. See `MODES.md` for the
full per-mode control map. 3DMAZE is the heaviest (per-column z-buffer vector raycaster) —
watch video stability if you touch it.

---

## 5. Release / PR workflow (two-branch dance)

- **add-64-cathode** = dev branch (`releases/64_cathode/`). Work here; keep it shippable.
- **add-96-cathode** = the upstream PR branch (`releases/96_cathode/`; 96 because 64 is taken
  upstream). Each folder only exists on its own branch.
- Feature work can go on a branch off add-64-cathode (e.g. `spectrum`) then merge back.

To ship an update upstream:
1. Land the change on add-64-cathode (merge the feature branch).
2. `git checkout add-96-cathode` → **`git reset --hard origin/main`** (fetch first). This is
   critical: base the PR branch on CURRENT upstream, or the PR shows add/add conflicts.
3. Copy `main.cpp` / `README.md` / `composite.pio` / `CMakeLists.txt` verbatim from the 64
   folder into the 96 folder; copy `info.yaml` then **restore its `Repository:` line** to
   `https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/96_cathode`.
4. **Rebuild the uf2 inside the 96 folder** (the build path is baked into the uf2, so it must
   be built there, not copied from 64).
5. Commit, `git push fork add-96-cathode --force-with-lease`.
6. `gh pr create --repo TomWhitwell/Workshop_Computer --head uglifruit:add-96-cathode`.
   A merged PR stays merged — each new version needs a **new** PR.

The panel overlay `CATHODE - WORKSHOP SYSTEM OVERLAY.jpg` sits in the folder (the gallery
picks it up by naming convention; it isn't referenced in info.yaml).

---

## 6. Gotchas / things to keep in mind

- **RAM ~77 %**. Every alt-boot mode's state is `static` and so allocated simultaneously
  (only one runs at a time). Big new modes with large arrays will push it; if it gets tight,
  the fix is to `union` the per-mode state (only one is live). FLASH is a non-issue (~4 %).
- **`GREY_H` must stay 128** for both formats — the NTSC scheme depends on the shared
  framebuffer. Don't shrink `FB_HEIGHT`; crop at scan-out instead.
- **`FRAME_WORDS` must be format-exact** (it's the DMA count). If you change line/frame
  timing, recheck it.
- Normal-mode switch checks must use the swapped `nsw`/`nswp`, not raw `sw` — or UP/MID will
  be inconsistent with the rest.
- `dilate_cap` and `text_mode` are per-frame globals; reset at the top of update_framebuffer.
  If a screen looks too bloomed or too dim, that's the lever.
- Black must sit above sync (`level_pair`), or the TV won't lock — a lesson already paid for.

---

## 7. Parked work — teletext

Branch `cathode-teletext-vbi` has a complete, byte-verified World System Teletext encoder
(+ `tools/tti2h.py` to convert `.tti` pages). It does **not** decode on a TV — root cause is
analog bandwidth (a 1 px / 144 ns data pulse can't reach full white through the DAC+cable),
not the encoding. It needs a hardware fix (better cable / lower source R / active 75 Ω buffer)
and a VBI capture stick (stk1160/em28xx + zvbi on Linux) to debug further. Not a firmware
problem; parked.
