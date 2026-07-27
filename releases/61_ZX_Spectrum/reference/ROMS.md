# ROM provenance

ZX (this card) embeds the original Sinclair ZX Spectrum ROM images. **Amstrad plc**
(the current copyright holder of the Sinclair ROMs) has granted permission for the
Spectrum ROMs to be freely redistributed and used in emulators, provided the
copyright is acknowledged and the ROM images are not modified. This card
acknowledges Amstrad's copyright in these ROM images.

## Files

| File in `roms/` | Bytes | MD5 | What it is |
|-----------------|-------|-----|------------|
| `spec128uk.rom` | 32768 | `85fede415f4294cc777517d7eada482e` | UK 128K ROM, both banks concatenated |
| `spec48.rom`    | 16384 | `4c42a2f075212361c3117015b107ff68` | Original 48K BASIC ROM |

The 32KB 128K image is the two 16KB banks back to back:
- bytes `0x0000–0x3FFF` = **ROM 0**, the 128K editor / menu ROM
- bytes `0x4000–0x7FFF` = **ROM 1**, the 48 BASIC ROM (as paged in by the 128K)

## How they're embedded

`tools/bin2h.py` converts a binary into a C header (`const unsigned char[]`), and
with `--slice START LEN` extracts one bank. The generated headers are:

```sh
python tools/bin2h.py roms/spec128uk.rom rom128_0 rom128_0.h --slice 0x0000 0x4000
python tools/bin2h.py roms/spec128uk.rom rom128_1 rom128_1.h --slice 0x4000 0x4000
python tools/bin2h.py roms/spec48.rom    rom48    rom48.h
```

`rom128_0.h` / `rom128_1.h` are wired into the 128K memory map; `rom48.h` is kept
for a future 48K mode.

## Source

Downloaded from the community ROM collection
[spectrumforeveryone/zx-roms](https://github.com/spectrumforeveryone/zx-roms)
(`spectrum128-plus2/128/spec128uk.rom`, `spectrum16-48/spec48.rom`). These are the
standard, unmodified images (the 48K MD5 matches the canonical FUSE/World of
Spectrum image).

Alternate sources for the same standard, unmodified dumps:
- http://www.rebelstar.co.uk/roms.htm (Andy's originally-cited source)
- https://www.planetemu.net/roms/sinclair-zx-spectrum-firmware-rom
