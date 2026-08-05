#!/usr/bin/env python3
"""mksamples.py — bake samples/*.raw into a single samples.h.

Usage:
    python tools/mksamples.py <sample_dir> <output.h>

Expects 8-bit SIGNED mono raw at 48kHz, named per mode with an optional variant
number for round robins:

    horses_1.raw .. horses_4.raw      (four: one per hoof)
    geese_1.raw  .. geese_8.raw       (up to eight round robins)
    frogs_N.raw  rain_N.raw  meteors_N.raw  cicadas_N.raw

A bare `horses.raw` (no number) is accepted and used for all four variants, so
a single recording per mode still works.

VARIANTS ARE NOT DECORATION. The firmware asks for a specific variant and in
Horses that variant IS THE HOOF: 1=left hind, 2=left fore, 3=right hind,
4=right fore (Horses uses only the first four slots). Hind hooves on a real animal strike lower and heavier than fores,
so record or order them accordingly — that front/back difference is most of what
makes a gait sound like an animal rather than a drum machine. In the other modes
the variants are simply different individuals (birds, species, drip sizes,
distances, insects) and the firmware picks between them at random, never
repeating the same one twice running.

Emits, for each mode, a flat concatenated array plus per-variant offsets:
    const signed char horses_pcm[];
    const uint32_t    horses_pcm_len;             // total
    const uint32_t    horses_pcm_off[4];          // start of each variant
    const uint32_t    horses_pcm_size[4];         // length of each variant

Converting a wav with ffmpeg:
    ffmpeg -i clop_lh.wav -ac 1 -ar 48000 -f s8 samples/horses_1.raw

Flash budget: 2MB total, ~107KB used by code, so roughly 41 seconds of 48kHz
8-bit mono fits. A full eight-variant library of one-shots comes to well under
half of that. These are one-shots; keep them short.
"""
import os
import sys

MODES = ["horses", "geese", "frogs", "rain", "meteors", "cicadas"]
NUM_VARIANTS = 8          # must match kNumVariants in voices.h
HOOF_VARIANTS = 4         # must match kHoofVariants: Horses = one per hoof


def load_variants(sample_dir, mode):
    """Return NUM_VARIANTS byte strings for a mode, padding by reuse.

    Padding reuses the SAME object rather than a copy, so emit() can spot the
    repeat and point both slots at one copy in flash.
    """
    # Horses' variants are the four hooves, not alternates; it never asks for
    # a slot above the fourth.
    limit = HOOF_VARIANTS if mode == "horses" else NUM_VARIANTS

    found = []
    for v in range(1, limit + 1):
        path = os.path.join(sample_dir, f"{mode}_{v}.raw")
        if os.path.isfile(path):
            with open(path, "rb") as f:
                found.append(f.read())
        else:
            found.append(None)
    found += [None] * (NUM_VARIANTS - limit)

    # Fall back to an unnumbered file for any variant with no recording.
    plain_path = os.path.join(sample_dir, f"{mode}.raw")
    plain = None
    if os.path.isfile(plain_path):
        with open(plain_path, "rb") as f:
            plain = f.read()

    have = [d for d in found if d is not None]
    if not have and plain is None:
        return None

    out = []
    for i, d in enumerate(found):
        if d is not None:
            out.append(d)
        elif plain is not None:
            out.append(plain)
        else:
            # Cycle whatever real variants exist rather than emitting silence.
            out.append(have[i % len(have)])
    return out


def emit(name, variants):
    """Concatenate the distinct recordings once, and point repeated variants at
    the copy already in flash. Horses only ever uses four slots (its variants
    are the hooves) and a mode with five recordings pads to eight, so storing
    the padding again would waste a quarter of a megabyte for nothing."""
    blob = bytearray()
    seen = {}                       # id of the bytes object -> (offset, size)
    offsets, sizes = [], []
    for v in variants:
        key = id(v)
        if key not in seen:
            seen[key] = (len(blob), len(v))
            blob += v
        off, size = seen[key]
        offsets.append(off)
        sizes.append(size)
    blob = bytes(blob)

    # Explicitly uint32_t: on the arm-none-eabi ABI uint32_t is `unsigned long`,
    # so arrays declared `unsigned` produce pointers that will not convert.
    lines = [f"const uint32_t {name}_pcm_len = {len(blob)};",
             f"const uint32_t {name}_pcm_off[{NUM_VARIANTS}] = {{"
             + ", ".join(str(o) for o in offsets) + "};",
             f"const uint32_t {name}_pcm_size[{NUM_VARIANTS}] = {{"
             + ", ".join(str(s) for s in sizes) + "};",
             f"const signed char {name}_pcm[] = {{"]
    for i in range(0, len(blob), 16):
        chunk = blob[i:i + 16]
        # Raw bytes are unsigned; reinterpret as signed two's complement.
        lines.append("    " + "".join(
            f"{b - 256 if b > 127 else b}," for b in chunk))
    lines.append("};")
    return lines, len(blob)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    sample_dir, out_path = sys.argv[1], sys.argv[2]

    out = ["// Auto-generated by tools/mksamples.py. Do not edit by hand.",
           "#pragma once", "#include <stdint.h>", ""]
    total = 0
    for mode in MODES:
        variants = load_variants(sample_dir, mode)
        if variants is None:
            variants = [b"\x00"] * NUM_VARIANTS
            print(f"  {mode}: missing, emitting silent stubs")
        else:
            sizes = "/".join(str(len(v)) for v in variants)
            print(f"  {mode}: {sizes} bytes")
        lines, n = emit(mode, variants)
        total += n
        out += lines
        out.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {out_path}: {total} bytes of PCM "
          f"({total / 48000.0:.2f}s at 48kHz)")


if __name__ == "__main__":
    main()
