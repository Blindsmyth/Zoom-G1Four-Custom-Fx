#!/usr/bin/env python3
"""Byte-patch a compiled kernel over the stock Chorus audio function, keeping everything else identical.

usage: patch_audio_fn.py STOCK.code KERNEL.obj OUT.code [--max 0x4c0]
The kernel object must contain only a relocation-free .text.  It replaces bytes 0..len of the stock
.text (the stock Fx_MOD_Chorus starts at .text+0 and is 0x4c0 bytes), remaining bytes become NOPs (0).
"""
import sys
from elftools.elf.elffile import ELFFile
stock, obj, out = sys.argv[1:4]
maxlen = int(sys.argv[sys.argv.index("--max") + 1], 0) if "--max" in sys.argv else 0x4C0
e = ELFFile(open(obj, "rb"))
t = [x for x in e.iter_sections() if x.name == ".text" and x["sh_type"] == "SHT_PROGBITS"][0]
if any(s.name.startswith(".rela") for s in e.iter_sections()) and e.get_section_by_name(".rela.text"):
    if e.get_section_by_name(".rela.text").data_size:
        sys.exit("kernel has relocations: not position independent")
code = t.data()
if t["sh_addralign"] < 32:
    print("note: kernel .text alignment", t["sh_addralign"])
if len(code) > maxlen:
    sys.exit(f"kernel is {len(code)} bytes, stock function slot is {maxlen}")
raw = bytearray(open(stock, "rb").read())
se = ELFFile(open(stock, "rb"))
st = [x for x in se.iter_sections() if x.name == ".text" and x["sh_type"] == "SHT_PROGBITS"][0]   # real code
off = st["sh_offset"]
assert off == 0x40 and st["sh_size"] >= maxlen
raw[off:off + maxlen] = code + b"\x00" * (maxlen - len(code))
open(out, "wb").write(raw)
print(f"patched {len(code)} kernel bytes into stock .text[0:{maxlen:#x}] -> {out}")
