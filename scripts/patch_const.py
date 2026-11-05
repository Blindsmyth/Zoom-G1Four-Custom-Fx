#!/usr/bin/env python3
"""Patch the descriptor data in the .const section of the (grafted) stock Chorus ELF, with guards.

  patch_const.py ELF.code [--name NAME] [--tile TILE.bin] [--defaults DEPTH RATE TONE MIX | --knobs N:MAX:DEF x4]

 --name      effect name, <= 11 chars, in the 12-byte name field at .const+0x38 (expects "Chorus")
 --tile      the 92-byte 23x30 on-screen picture at .const+0x400 (expects the stock Chorus picture)
 --defaults  default knob values (UI units) stored at entry+0x10 of each knob entry:
             Depth @+0x70 (stock 40, max 100)   Rate @+0xa8 (stock 24, max 49)
             Tone  @+0xe0 (stock 7,  max 10)    Mix  @+0x118 (stock 50, max 100)
Every location is checked against the stock value first; nothing is written if any check fails.
"""
import argparse, hashlib, struct, sys
from elftools.elf.elffile import ELFFile

STOCK_TILE_SHA256 = "ec9a0dec3c65cb122b9d12593715549399189eb338f82ce4efb2e97e05827187"
KNOBS = [("Depth", 0x70, 40, 100), ("Rate", 0xA8, 24, 49), ("Tone", 0xE0, 7, 10), ("Mix", 0x118, 50, 100)]

ap = argparse.ArgumentParser()
ap.add_argument("elf"); ap.add_argument("--name"); ap.add_argument("--tile")
ap.add_argument("--defaults", nargs=4, type=int, metavar=("DEPTH", "RATE", "TONE", "MIX"))
ap.add_argument("--knobs", nargs=4, metavar="NAME:MAX:DEFAULT",
                help="rename and re-range the four knob entries (Depth Rate Tone Mix order), e.g. ShftA:24:19 Width:10:7 ShftB:24:7 Mix:100:50")
a = ap.parse_args()

d = bytearray(open(a.elf, "rb").read())
C = [s for s in ELFFile(open(a.elf, "rb")).iter_sections() if s.name == ".const"][0]["sh_offset"]
edits = []

if a.name:
    assert len(a.name) <= 11, "name must be <= 11 chars (12-byte field incl. NUL)"
    assert d[C + 0x38:C + 0x44] == b"Chorus" + b"\0" * 6, ("unexpected name field", d[C + 0x38:C + 0x44])
    edits.append((C + 0x38, a.name.encode().ljust(12, b"\0")))

if a.tile:
    t = open(a.tile, "rb").read()
    assert len(t) == 92, "tile must be 92 bytes"
    assert hashlib.sha256(d[C + 0x400:C + 0x400 + 92]).hexdigest() == STOCK_TILE_SHA256, "tile is not the stock Chorus picture"
    edits.append((C + 0x400, t))

if a.defaults:
    for (nm, base, stock, mx), new in zip(KNOBS, a.defaults):
        off = C + base
        assert d[off:off + 12].rstrip(b"\0") == nm.encode(), (nm, d[off:off + 12])
        cur_max, cur_def = struct.unpack_from("<II", d, off + 0x0C)
        assert cur_max == mx and cur_def == stock, (nm, cur_max, cur_def)
        assert 0 <= new <= mx, f"{nm} default {new} outside 0..{mx}"
        edits.append((off + 0x10, struct.pack("<I", new)))

if a.knobs:
    assert not a.defaults, "use --knobs or --defaults, not both"
    for (nm, base, stock, mx), spec in zip(KNOBS, a.knobs):
        name, new_max, new_def = spec.split(":"); new_max, new_def = int(new_max), int(new_def)
        off = C + base
        assert d[off:off + 12].rstrip(b"\0") == nm.encode(), (nm, d[off:off + 12])
        cur_max, cur_def, cur_2 = struct.unpack_from("<III", d, off + 0x0C)
        assert cur_max == mx and cur_def == stock, (nm, cur_max, cur_def)
        assert len(name) <= 11 and 0 <= new_def <= new_max, (name, new_max, new_def)
        edits.append((off, name.encode().ljust(12, b"\0")))
        edits.append((off + 0x0C, struct.pack("<II", new_max, new_def)))
        if cur_2 == mx:                                   # Rate and Mix repeat the maximum at +0x14
            edits.append((off + 0x14, struct.pack("<I", new_max)))

for off, data in edits:
    d[off:off + len(data)] = data
open(a.elf, "wb").write(d)
print(f"patched {len(edits)} field(s) in {a.elf}" + (f" ; defaults Depth/Rate/Tone/Mix = {a.defaults}" if a.defaults else ""))
