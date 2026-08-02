#!/usr/bin/env python3
"""Build a single UF2 containing the ws-doom firmware plus the WHX-compressed
shareware WAD at its flash address, so the card is flashed in one drag-and-drop.

Usage:
  make_uf2.py <firmware.bin> <doom1.whx> <out.uf2> [--wad-addr 0x10048000]
"""

import argparse
import struct
import sys

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
RP2040_FAMILY = 0xE48BFF56
FLASH_BASE = 0x10000000
PAYLOAD = 256


def segments_to_blocks(segments):
    total = sum((len(data) + PAYLOAD - 1) // PAYLOAD for _, data in segments)
    blocks = []
    block_no = 0
    for addr, data in segments:
        for off in range(0, len(data), PAYLOAD):
            chunk = data[off:off + PAYLOAD]
            hdr = struct.pack(
                "<IIIIIIII",
                UF2_MAGIC0, UF2_MAGIC1, UF2_FLAG_FAMILY_ID,
                addr + off, PAYLOAD, block_no, total, RP2040_FAMILY,
            )
            blocks.append(hdr + chunk.ljust(476, b"\0") + struct.pack("<I", UF2_MAGIC_END))
            block_no += 1
    return b"".join(blocks)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("firmware_bin")
    p.add_argument("whx")
    p.add_argument("out")
    p.add_argument("--wad-addr", type=lambda x: int(x, 0), default=0x10048000)
    args = p.parse_args()

    with open(args.firmware_bin, "rb") as f:
        fw = f.read()
    with open(args.whx, "rb") as f:
        wad = f.read()

    if len(fw) > args.wad_addr - FLASH_BASE:
        sys.exit(f"firmware ({len(fw)} bytes) overlaps WAD address 0x{args.wad_addr:08x}")
    if args.wad_addr + len(wad) > FLASH_BASE + 2 * 1024 * 1024:
        sys.exit(f"WAD ({len(wad)} bytes) exceeds 2MB flash")

    out = segments_to_blocks([(FLASH_BASE, fw), (args.wad_addr, wad)])
    with open(args.out, "wb") as f:
        f.write(out)
    print(f"{args.out}: {len(fw)} bytes firmware + {len(wad)} bytes WAD "
          f"({len(out) // 512} UF2 blocks)")


if __name__ == "__main__":
    main()
