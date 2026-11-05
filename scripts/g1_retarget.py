#!/usr/bin/env python3
"""Retarget a clean-room ZD2 (built by Stomphacks' pipeline for the MS Plus pedals) for the G1 FOUR.

It changes exactly one header field, the target bitfield at offset 12, to a value with bit 0x2 set (the G1
FOUR's OS refuses a file without it), then recomputes the CRC at offset 8. Nothing else is touched: no code,
no section, no identity. Stock G1-listed effects carry 0x7, 0x6 or 0x2; 0x2 is the default here.

    python3 -I scripts/g1_retarget.py IN.ZD2 OUT.ZD2 [--target 0x0002]

The output still has to pass research/tools/g1check.py (or your own copy) before it goes near a pedal.
"""
import argparse
import struct
import sys
import zlib


def crc(data: bytes) -> int:
    # The zoom-zt2 / decode_effect.py recipe: CRC32 over everything after the CRC field up to the 16-byte
    # trailer, inverted.
    return zlib.crc32(data[12:-16]) ^ 0xFFFFFFFF


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--target", type=lambda s: int(s, 0), default=0x0002)
    a = ap.parse_args()
    if not a.target & 0x2 or not 0 <= a.target <= 0xFFFF:
        sys.exit("target must be a 16-bit value with bit 0x2 set")

    d = bytearray(open(a.src, "rb").read())
    if d[:4] != b"ZDLF" or struct.unpack_from("<I", d, 4)[0] != 120:
        sys.exit("not a ZD2 container (ZDLF magic and header constant 120 expected)")
    stored, old = struct.unpack_from("<II", d, 8)
    if stored != crc(d):
        sys.exit(f"input CRC 0x{stored:08x} does not match its contents (0x{crc(d):08x}); refusing")

    struct.pack_into("<I", d, 12, a.target)
    struct.pack_into("<I", d, 8, crc(d))
    open(a.dst, "wb").write(d)
    changed = [i for i, (x, y) in enumerate(zip(open(a.src, "rb").read(), d)) if x != y]
    if any(not (8 <= i < 16) for i in changed):
        sys.exit("internal error: bytes outside the CRC and target fields changed")
    print(f"{a.dst}: target 0x{old:04x} -> 0x{a.target:04x}, CRC 0x{stored:08x} -> "
          f"0x{struct.unpack_from('<I', d, 8)[0]:08x}, {len(changed)} bytes changed, all in the header")
    return 0


if __name__ == "__main__":
    sys.exit(main())
