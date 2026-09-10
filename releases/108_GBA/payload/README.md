# GBA payload

The GBA-side program uploaded into a cartridge-less Game Boy Advance via BIOS Multiboot.
It is a **PSG synth voice with its own editor**, dependency-free (no libgba, no libc) so it
builds with a bare `arm-none-eabi` toolchain targeting the GBA's ARM7TDMI (armv4t).

## What it does

- Paints the screen **green in assembly**, in `crt0.s`, before the C runtime exists. That is the
  boot proof: white means the image never ran, green means it ran and the C startup died, and the
  play screen means a normal boot. Three states that were previously one identical white screen.
- Acts as a **normal-mode 32-bit serial slave** (external clock from the RP2040), re-armed by
  the **serial interrupt** so the console is not deaf while it redraws.
- Owns the instrument: pitch tracking from CV, a 7-source modulation matrix with per-voice
  routing, per-channel software ADSR, portamento, ornaments, a drum engine, and all four PSG
  channels (2 square, wave, noise).
- Draws a performance readout and an eleven-page editor.
- Holds the patch, which the Workshop stores for it in flash — sixteen slots, transferred a byte
  per link word.
- Boots into a showcase patch rather than a blank one: a hard-panned square pair with a shimmer
  above it, playable from the module's switch with nothing patched. See `synth_default_patch()`.

## Files

| File            | Purpose |
|-----------------|---------|
| `main.c`        | Init and the main loop |
| `link.c/.h`     | Serial slave, serial IRQ handler, protocol decode |
| `psg.c/.h`      | PSG registers, wavetables |
| `synth.c/.h`    | Voice: pitch, modulation, envelopes, ornaments, drums, performance controls |
| `ui.c/.h`       | Performance screen + eleven-page editor |
| `gfx.c/.h`      | Mode-3 drawing and text |
| `diag.c/.h`     | Link-characterisation screens (used by `linkrate`/`bandwidth`) |
| `notes.h`       | **Generated** by `gen_notes.py`: PSG period register per MIDI note |
| `font5x7.h`     | 5×7 ASCII font |
| `crt0.s`        | GBA ROM header + multiboot entry (`_start` sits at offset `0xC0`) |
| `multiboot.ld`  | Links the image for EWRAM (`0x02000000`) |
| `build.sh`      | Compile every `.c` → link → `.mb` → logo/checksum → `../gba_payload.h` |
| `bin2h.py`      | Converts `payload.mb` to the C byte array |

## Two constraints that will bite you

**No runtime division.** We link `-nostdlib`, so libgcc is absent: dividing by a *variable*
is an undefined `__aeabi_uidiv` / `__aeabi_idivmod` at link time, not merely slow code. That
is a deliberate tripwire and it has already caught a `% rows` in the editor. Divides by
compile-time constants are fine — GCC turns those into multiply-and-shift. `synth.c` has a
shift-subtract `udiv32` for the one place that genuinely needs it.

**The IRQ stack is small.** The BIOS calls the serial handler in IRQ mode on the IRQ stack at
`0x03007FA0`. `crt0.s` therefore puts the user stack at `0x03007E00` rather than the usual
`0x03007F00`, giving the handler 416 bytes instead of 160. Keep the handler shallow.

**`0xC0` is a BRANCH, not code.** GBATEK: the BIOS overwrites bytes `0xC4` (boot mode) and `0xC5`
(slave ID) of the loaded image. Put instructions there and it corrupts them after loading, every
time — see the long note at the top of `crt0.s` for how that cost a white screen.

**No runtime division, and no implicit `memcpy` either.** Both are link errors here. A struct
assignment compiles to `memcpy`; copy byte by byte.

## Building

```sh
cd payload
./build.sh
```

This overwrites `../gba_payload.h` with the compiled payload, then rebuild the RP2040
firmware to bake it in.

### Toolchain

`build.sh` needs two things:

1. **A compiler** — either devkitARM, or any `arm-none-eabi-gcc` on `PATH`. The Raspberry
   Pi Pico SDK already ships one (`~/.pico-sdk/toolchain/*/bin/arm-none-eabi-gcc`) which
   compiles the payload fine when invoked with `-mcpu=arm7tdmi -marm -mthumb-interwork`.

2. **A gbafix** — inserts the 156-byte Nintendo logo and fixes the header complement. **The
   GBA BIOS compares the logo against its own copy and locks up if it's wrong**, so this
   step is mandatory for real hardware. `build.sh` uses real `gbafix` (devkitARM, via
   `DEVKITARM`/`DEVKITPRO` or `PATH`) if present, otherwise the **bundled, dependency-free
   [`gbafix.py`](gbafix.py)** — a faithful reimplementation of the logo + complement fix, so
   no devkitPro install is needed.

The committed [`../gba_payload.h`](../gba_payload.h) is already a real, bootable image
(valid logo, passing header complement). Re-run `build.sh` only after editing the payload.

## Serial protocol

Defined once in [`../gba_proto.h`](../gba_proto.h) and included by both this payload (C) and
the RP2040 firmware (C++), so the two sides cannot drift. See the comments there; the host
side is `../gba_link.cpp`.

32-bit words, MSB-first, mode 3, RP2040 is the clock master.
