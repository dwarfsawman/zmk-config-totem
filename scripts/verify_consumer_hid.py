"""Verify the Consumer HID descriptor in the central's built UF2 firmware."""

import struct
import sys
from pathlib import Path


def read_uf2(path):
    raw = path.read_bytes()
    if not raw or len(raw) % 512:
        raise ValueError("Invalid UF2 file size")
    blocks = {}
    for offset in range(0, len(raw), 512):
        magic0, magic1, flags, address, size, *_ = struct.unpack_from("<8I", raw, offset)
        end_magic = struct.unpack_from("<I", raw, offset + 508)[0]
        if (magic0, magic1, end_magic) != (0x0A324655, 0x9E5D5157, 0x0AB16F30):
            raise ValueError("Invalid UF2 block magic")
        if size > 476:
            raise ValueError("Invalid UF2 payload size")
        if not flags & 1:  # Skip blocks marked NOT_MAIN_FLASH.
            blocks[address] = raw[offset + 32 : offset + 32 + size]
    if not blocks:
        raise ValueError("UF2 contains no flash data")
    start = min(blocks)
    end = max(address + len(payload) for address, payload in blocks.items())
    image = bytearray(b"\xff" * (end - start))
    for address, payload in blocks.items():
        image[address - start : address - start + len(payload)] = payload
    return image


def main():
    image = read_uf2(Path(sys.argv[1]))
    prefix = "05 0c 09 01 a1 01 85 02 05 0c 15 00"
    suffix = "75 10 95 06 81 00 c0"
    limited = bytes.fromhex(f"{prefix} 26 ff 02 19 00 2a ff 02 {suffix}")
    original = bytes.fromhex(f"{prefix} 26 ff 0f 19 00 2a ff 0f {suffix}")
    if original in image:
        raise ValueError("Unpatched Consumer HID range 0x0FFF remains in firmware")
    if limited not in image:
        raise ValueError("Expected 16-bit Consumer HID range 0x02FF / count 6 not found")
    print("Verified Consumer HID: usage/logical maximum 0x02FF, 16-bit values, count 6")


if __name__ == "__main__":
    main()
