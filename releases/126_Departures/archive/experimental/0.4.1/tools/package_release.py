#!/usr/bin/env python3
"""Validate and package a local build without changing the firmware bytes."""
# Copyright (c) 2026 Adrian Vos. SPDX-License-Identifier: MIT
import hashlib
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.4.1-experimental"


def main():
    build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build"
    image = (build / "Departures.uf2").read_bytes()
    binary = (build / "Departures.bin").read_bytes()
    if not image or len(image) % 512:
        raise ValueError("UF2 must contain complete 512-byte blocks")
    count = len(image) // 512
    payload = {}
    for i in range(count):
        block = image[i * 512:(i + 1) * 512]
        m0, m1, flags, address, size, number, total, family = struct.unpack_from("<8I", block)
        end_magic, = struct.unpack_from("<I", block, 508)
        if (m0, m1, end_magic) != (0x0A324655, 0x9E5D5157, 0x0AB16F30):
            raise ValueError(f"Block {i}: invalid UF2 magic")
        if flags != 0x2000 or family != 0xE48BFF56:
            raise ValueError(f"Block {i}: expected an RP2040 flash block")
        if size != 256 or number != i or total != count:
            raise ValueError(f"Block {i}: invalid payload size or numbering")
        if address != 0x10000000 + i * 256 or address in payload:
            raise ValueError(f"Block {i}: unexpected flash address")
        if address + size > 0x10200000:
            raise ValueError("Image exceeds the default 2MB card layout")
        payload[address] = block[32:32 + size]
    if b"".join(payload[a] for a in sorted(payload))[:len(binary)] != binary:
        raise ValueError("UF2 payload does not match Departures.bin")

    output = ROOT / "uf2"
    output.mkdir(exist_ok=True)
    name = f"Departures_{VERSION}.uf2"
    digest = hashlib.sha256(image).hexdigest()
    (output / name).write_bytes(image)
    # Keep checksums for previous listening builds retained beside this image.
    (output / "SHA256SUMS.txt").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n"
        for p in sorted(output.glob("*.uf2"))))
    (output / "NOTICE.txt").write_bytes((ROOT / "NOTICE.txt").read_bytes())
    print(f"Packaged {name}: {count} validated blocks; SHA-256 {digest}")


if __name__ == "__main__":
    main()
