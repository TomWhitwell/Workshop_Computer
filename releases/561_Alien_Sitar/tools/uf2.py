#!/usr/bin/env python3
"""Write and independently verify an RP2040 flash UF2 from SDK-linked binary."""
from pathlib import Path
import struct, sys, binascii


def verify_boot2(binary):
    # SDK boot_stage2/pad_checksum uses bit-reversed CRC32 with seed0xffffffff.
    reverse = lambda value, width: int(f"{value:0{width}b}"[::-1], 2)
    crc = reverse(
        (binascii.crc32(bytes(reverse(b, 8) for b in binary[:252])) ^ 0xFFFFFFFF)
        & 0xFFFFFFFF,
        32,
    )
    assert (
        struct.unpack("<I", binary[252:256])[0] == crc
    ), "invalid RP2040 boot2 checksum"


def convert(binary):
    blocks = (len(binary) + 255) // 256
    assert 256 <= len(binary) <= 2 * 1024 * 1024
    out = bytearray()
    for i in range(blocks):
        data = binary[i * 256 : (i + 1) * 256].ljust(256, b"\0")
        out += (
            struct.pack(
                "<8I",
                0x0A324655,
                0x9E5D5157,
                0x2000,
                0x10000000 + i * 256,
                256,
                i,
                blocks,
                0xE48BFF56,
            )
            + data
            + bytes(220)
            + struct.pack("<I", 0x0AB16F30)
        )
    verify(bytes(out), binary)
    return out


def verify(uf2, binary):
    assert len(uf2) % 512 == 0
    count = len(uf2) // 512
    data = bytearray()
    for i in range(count):
        b = uf2[i * 512 : (i + 1) * 512]
        h = struct.unpack("<8I", b[:32])
        assert h == (
            0x0A324655,
            0x9E5D5157,
            0x2000,
            0x10000000 + 256 * i,
            256,
            i,
            count,
            0xE48BFF56,
        )
        assert struct.unpack("<I", b[-4:])[0] == 0x0AB16F30
        data += b[32:288]
    assert data[: len(binary)] == binary
    assert not any(data[len(binary) :])


if __name__ == "__main__":
    source = Path(sys.argv[1]).read_bytes()
    verify_boot2(source)
    Path(sys.argv[2]).write_bytes(convert(source))
    print(f"RP2040 UF2 validated, family 0xe48bff56, {len(source)} binary bytes")
