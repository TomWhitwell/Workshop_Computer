# ZX + OneBit — for the Music Thing Workshop Computer

A program card for the **Music Thing Modular Workshop System Computer** that carries
**two different instruments in one firmware**, chosen at power-on:

- **ZX** — a cycle-accurate **ZX Spectrum 128K** (Z80 + ULA + banked memory + AY sound
  chip) that turns the module's jacks, knobs and switch into a two-way bridge between
  the modular and the machine: play games/demos, load snapshots and `.ay`/`.pt3`
  chip-music,
  drive the keyboard from CV/gates or a laptop, read patched CV back as data inside a
  Spectrum program, and take beeper/border/AY out as CV and audio.
- **OneBit** — a 1-bit "beeper synth" that ports classic ZX-Spectrum beeper-music
  engines. A playable modular voice with pitch CV, gate, duophony and drums.

They're thematically adjacent but **used quite differently**, so their controls and
jack mappings are documented separately below.

Runs on a real Workshop Computer. Built on the RP2040.

## Choosing a mode at boot

| At power-on… | Boots |
|--------------|-------|
| Normal (switch not held) | **ZX Spectrum** |
| **Hold the momentary switch Down** through startup (~½ sec) | **OneBit** |

(ZX runs at 200 MHz, OneBit at 144 MHz; the boot dispatcher sets the clock for
whichever you choose.)

## Panel overlays

Printable panel overlays for both modes are in [`panels/`](panels/) — they label
every knob and jack for the mode. The panels below are the quick reference.

| ZX Spectrum mode | OneBit mode |
|:---:|:---:|
| ![ZX Spectrum overlay](panels/ZX%20Spectrum%20mode%20overlay.png) | ![OneBit overlay](panels/OneBit%20mode%20overlay.png) |

---

# Mode 1 — ZX Spectrum

## What it does

- **Cycle-accurate Z80** at the true emulated rate — 3.5469 MHz (128K) or 3.5 MHz
  (real 48K timing when a 48K program is loaded).
- **128K Spectrum**: 128 KB banked RAM, 32 KB ROM, `0x7FFD` paging, the ULA
  (`0xFE` beeper/border), and the **AY-3-8912** (3 tones + noise + 8 envelope shapes).
  48K programs run too.
- **Loads** `.z80` (v1/v2/v3, 48K & 128K, RLE), `.sna` (48K/128K) snapshots, and
  `.ay` / `.pt3` (Vortex Tracker) chip-music. AY tunes run their own Z80 player on
  the emulated CPU; PT3 modules run against an embedded PT3 player — both via a
  self-contained IM2 harness. Everything decodes **in the browser** and streams to
  the card, so any size fits.
- **AY-3-8912** uses the **measured** real-chip volume curve, so tunes play at correct
  pitch, timing, envelopes and a faithful tone/noise balance.
- A **baked-in default** program boots at power-on (built from
  `snapshots/bakedasm.z80`); with none present it boots the 128K menu.
- **Reverb** (Freeverb-style) on the beeper+AY mix, wet/dry on Knob X.

## Panel (ZX)

| Control | Function |
|---------|----------|
| **Knob Main** | Emulation **speed** — centre **deadzone** = exactly 100% / real time (speed shifts pitch, so the detent holds true speed). Applies to snapshots and `.ay`. |
| **Knob X** | **Reverb** wet/dry (on Audio Out 1) |
| **Knob Y** | Readable by the Z80 at **port `0x5F`** (`IN 95` in BASIC) |
| **Switch Up** | **Pause** (also the moment to use the Web UI) |
| **Switch Middle** | **Run** |
| **Switch Down** | A **mappable momentary keypress** |

## Inputs (ZX)

Every jack + the switch is **mapped in the Web UI** to one of: a **key** (held while
the input is active), a **Kempston** joystick direction, a **Z80-readable port** (the
input's live 0–255 value), or **None**. A jack only acts when something is patched in.

| Jack | Default | Mappable to |
|------|---------|-------------|
| **Pulse In 1** | ENTER | key / Kempston / port / none |
| **Pulse In 2** | SPACE | key / Kempston / port / none |
| **CV In 1** | Q (comparator) | key / Kempston / port / none |
| **CV In 2** | A | key / Kempston / port / none |
| **Audio In 1** | O | key / Kempston / port / none |
| **Audio In 2** | P | key / Kempston / port / none |
| **Switch Down** | ENTER | key / Kempston / port / none |

**Input ports** (read with `IN A,(port)` when a source is mapped to *Port*):
Knob Y `0x5F`, CV In 1 `0x6F`, CV In 2 `0x7F`, Audio In 1 `0x8F`, Audio In 2 `0x9F`,
Pulse In 1 `0xAF`, Pulse In 2 `0xBF`, Switch `0xCF`.

## Write your own — a Spectrum program in the CV world

The real point of ZX mode is to **write a ZX Spectrum program that interfaces with the
modular** — read patched CV/gates (as keypresses, joystick, or values on the input
ports), do whatever you like in Z80, and drive the beeper / border / AY / memory-probe
CV back out. Write it in BASIC or assembly, assemble/save a `.z80` (or `.sna`), and
either upload it over the Web UI or bake it in as the default.

The baked-in default demo is exactly this — its source is
[`BakedASM.asm`](BakedASM.asm), a small Z80 program that:
- scans the **keyboard** (so it responds to whatever CV/gates you've mapped to keys),
- reads **Port 95** (`IN A,(95)` = Knob Y) and uses it to modulate the sound, and
- bangs the **beeper** (`OUT (254),A`) → Pulse Out 1.

That's the whole loop — modular in → Spectrum logic → audio/CV out — in ~40 lines.
Start from it. (`snapshots/bakedasm.z80` is the assembled version that gets baked in.)

## Outputs (ZX)

| Jack | Function |
|------|----------|
| **Pulse Out 1** | **Beeper** |
| **Pulse Out 2** | MIC / tape line |
| **CV Out 1** | **Border** voltage |
| **CV Out 2** | **Memory probe** — a chosen RAM byte (0–255) as 0–5 V (address set in the Web UI; default 16384 = screen memory) |
| **Audio Out 1** | **Reverb** (beeper + AY mix), wet/dry on Knob X |
| **Audio Out 2** | **AY-3-8912** sound |
| **LEDs** | Left: mode (128K / 48K / AY). Right: heartbeat / correct-speed / beeper |

## Web UI (ZX)

USB-MIDI / WebMIDI SysEx — **Chrome or Edge**, no install.

> **Open the web app: <https://uglifruit.github.io/WorkshopZX/interface.html>**
> (or the local [`interface.html`](interface.html) copy). Chrome/Edge, plug the card
> into USB, click **Connect**. Pause the card (switch Up) while uploading.

- **Upload** `.z80` / `.sna` / `.ay` / `.pt3`.
- **Remap** inputs on a clickable QWERTY keyboard, with **Kempston**, **→ Port** and
  **✕ None** targets.
- **Keyboard passthrough** — type into the Spectrum. **Shift** = CAPS SHIFT,
  **Alt** = SYMBOL SHIFT, **Shift+0** or **Backspace** = DELETE.
- Set the **CV Out 2 memory-probe** address.

---

# Mode 2 — OneBit

A 1-bit beeper synth: a single fast-bitbanged output makes the tone, with
pseudo-polyphony faked by interleaving/XOR-ing squares — the classic ZX beeper trick.
Seven selectable engines (faithful ports of well-known Spectrum beeper routines) plus
drums. Boot it by holding the switch **Down** at power-on.

## Panel (OneBit)

| Control | Function |
|---------|----------|
| **Knob Main** | **Engine × decay** — seven engine bands (Beep → PlipPlop → Tritone → Qchan → Phaser → Savage → Music Box); *within* each band the knob sweeps note-decay short → long |
| **Knob X** | **Root pitch** (~C1–C6), summed with CV In 1 |
| **Knob Y** | *Switch Up:* voice-2 interval (far CCW = solo, then unison / m3 / M3 / P5 / dom7 / octave). *Switch Middle:* a per-engine **timbre** control (Phaser detune / Savage skew depth) |
| **Switch Up** | Duophonic — voice 2 = CV In 1 root + Knob Y interval |
| **Switch Middle** | Duophonic — voice 2 = **CV In 2** (its own 1V/oct); Knob Y = timbre |
| **Switch Down** | Momentary tap **cycles the drum kit** (Click → Tritone → PCM → Synth) |

## Inputs (OneBit)

| Jack | Function |
|------|----------|
| **CV In 1** | **Voice 1 pitch**, 1V/oct (+ Knob X) |
| **CV In 2** | **Voice 2 pitch**, 1V/oct (in Switch Middle) |
| **Audio In 1** | Duty / pulse-width mod; **also latches the drum select** at each Pulse In 2 edge (low = kick … high = snare) |
| **Audio In 2** | Duty / timbre mod |
| **Pulse In 1** | **Note gate** — rising edge triggers the envelope, **held sustains**, falling releases |
| **Pulse In 2** | **Drum trigger** (drum chosen by Audio In 1) |

## Outputs (OneBit)

| Jack | Function |
|------|----------|
| **Pulse Out 1** | 1-bit **tone** (the true bitbanged beeper output) |
| **Pulse Out 2** | 1-bit **drum** lane |
| **Audio Out 1** | Tone density (PCM-style monitor of the tone) |
| **Audio Out 2** | Drum density |
| **LEDs** | Left (0/2/4) = **engine number in binary** (0–6). Right = note-decay glow (1) / drum kit (3) / drum activity (5) |

*(OneBit uses no Web UI. See its own repo/README for the full details and engine
history.)*

---

# Under the hood (ZX)

`ProcessSample()` runs at 48 kHz — far too slow to *be* the 3.5 MHz Z80 clock — so
work is split across the RP2040's two cores (the repo's `second_core` pattern):

| Core | Job |
|------|-----|
| **Core 1** | Free-runs the Z80 + ULA + AY, paced to emulated Spectrum time; also services USB. |
| **Core 0** | 48 kHz I/O: latches beeper/AY/border to the outputs, samples the inputs, runs the mapping engine + reverb, presents keyboard/joystick/port state to the Z80. |

They meet through a small lock-free `CrossCore` struct (single-writer per field). An
instruction-stepped Z80 core keeps it real-time from flash. Emulator setup runs **on
core 1**, never in the ComputerCard constructor (which would wedge the chip).

## Building

Raspberry Pi Pico SDK (2.2.0):

```sh
cmake -B build -G Ninja
cmake --build build
```

Produces `build/zx.uf2`. Hold BOOTSEL on the Computer's RP2040 and drop it on the
mounted drive. ROM headers and the baked-snapshot header are generated from
`roms/*.rom` and `snapshots/bakedasm.z80` at build time (Python + `tools/bin2h.py`).

**Bake your own default snapshot:**
```sh
python tools/bin2h.py path/to/game.z80 snapshot_z80 snapshot_data.h
```
Copyrighted game/`.ay` files are **not** committed; only the freely-usable `bakedasm`
demo is.

---

## Credits

This card stands on a lot of other people's work. Thank you, all of you.

**Hardware & framework**
- **Music Thing Modular Workshop System Computer** and the **ComputerCard** library —
  Tom Whitwell / Music Thing Modular; ComputerCard by **Chris Johnson**. The whole
  card is built on this. (MIT-licensed, header-only.)
- **Raspberry Pi Pico SDK** / RP2040 — Raspberry Pi Ltd.

**ZX Spectrum emulation**
- **Z80 CPU core** — [`superzazu/z80`](https://github.com/superzazu/z80) by
  **Nicolas Allemand** (MIT). Instruction-stepped, passes zexdoc/zexall. Vendored in
  `vendor/sz80/` with its licence; the only modification is a `port16` field so the
  I/O callback receives the full 16-bit port (marked `MOD(ZX)` in-file).
- **Sinclair ZX Spectrum ROMs** — © **Amstrad plc**, redistributed with Amstrad's
  kind permission (see `reference/ROMS.md`). Unmodified images.
- Format & hardware references: **World of Spectrum** and the **Sinclair Wiki**
  (`.z80`/`.sna`/`.ay` formats, `0x7FFD` paging, ULA port decode, AY, Kempston);
  Sean Young, *Z80 Undocumented Documented*; Chris Smith, *The ZX Spectrum ULA*;
  the **zexall** test suite; **Project AY** for the `.ay` format. **Spectaculator 8**
  was used as the reference emulator for testing.
- **AY-3-8912** volume curve from the measured real-chip response (as used by MAME /
  AY emulators).
- **PT3 playback** uses the **Pro Tracker 3 / Vortex Tracker II** player routine
  (embedded); PT3 format by **Sergey Bulba** and the Vortex Tracker authors.
- The reverb is a fixed-point take on **Jezar at Dreampoint**'s *Freeverb* topology.

**OneBit beeper engines** (all via **Beepola** by Chris Cowley):
- **Joffa Smith** — PlipPlop / Special FX · **Shiru** — Tritone / Qchan / Phaser /
  Huby · **Jason C Brooke** — Savage · **Mark Alexander** — Music Box ·
  **Saa Puica** — Music Studio · and above all **utz** for the 1-bit routine
  tutorials and community.

**This card**
- ZX + OneBit for the Workshop Computer — **Andy Jenkinson** (**uglifruit**), 2026,
  with **Claude Code** (Anthropic).

## Licence

The card's own source is Andy's. Vendored components keep their own licences
(`vendor/sz80/LICENSE`, `ComputerCard.h`). The Sinclair ROMs are Amstrad's, used by
permission. No copyrighted game or music files are included.
