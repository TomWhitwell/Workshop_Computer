#!/usr/bin/env python3
"""Convert a raw RP2040 binary to UF2 format."""

import sys
import struct

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGIC2 = 0x0AB16F30
FLAG_FAMILY_ID = 0x00002000
FAMILY_RP2040 = 0xE48BFF56
PAGE_SIZE = 256
BLOCK_SIZE = 512
LOAD_ADDR = 0x10000000


def bin2uf2(bin_path, uf2_path, base_addr=LOAD_ADDR):
    with open(bin_path, "rb") as f:
        data = f.read()

    if len(data) % PAGE_SIZE:
        data += b"\x00" * (PAGE_SIZE - (len(data) % PAGE_SIZE))

    num_blocks = len(data) // PAGE_SIZE
    blocks = []
    for i in range(num_blocks):
        chunk = data[i * PAGE_SIZE : (i + 1) * PAGE_SIZE]
        block = bytearray(BLOCK_SIZE)
        struct.pack_into(
            "<IIIIIIII",
            block,
            0,
            UF2_MAGIC0,
            UF2_MAGIC1,
            FLAG_FAMILY_ID,
            base_addr + i * PAGE_SIZE,
            PAGE_SIZE,
            i,
            num_blocks,
            FAMILY_RP2040,
        )
        block[32 : 32 + PAGE_SIZE] = chunk
        struct.pack_into("<I", block, BLOCK_SIZE - 4, UF2_MAGIC2)
        blocks.append(block)

    with open(uf2_path, "wb") as f:
        f.write(b"".join(blocks))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.bin output.uf2")
        sys.exit(1)
    bin2uf2(sys.argv[1], sys.argv[2])
