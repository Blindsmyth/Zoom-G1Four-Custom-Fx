#!/usr/bin/env python3
"""Graft new code into a stock Zoom effect ELF WITHOUT re-assembling or re-linking it.

 * appends KERNEL bytes to the end of the executable .text segment (vaddr = old end)
 * overwrites the start of the stock audio function (.text+0) with a trampoline that jumps to it
 * shifts every file offset that follows (phdrs, shdrs, .dynamic file-offset tags)
Everything else stays byte-identical to the stock effect.

usage: elf_graft.py STOCK.code KERNEL.bin TRAMP.bin OUT.code
"""
import struct, sys

stock, kern, tramp, out = sys.argv[1:5]
d = bytearray(open(stock, "rb").read())
K = open(kern, "rb").read()
T = open(tramp, "rb").read()
assert len(K) % 32 == 0, "kernel must be padded to a multiple of 32"

# ---- ELF32 little-endian header ----
assert d[:4] == b"\x7fELF" and d[4] == 1 and d[5] == 1
e_phoff, e_shoff = struct.unpack_from("<II", d, 0x1C)
e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHHHH", d, 0x2A)

phdrs = []
for i in range(e_phnum):
    o = e_phoff + i * e_phentsize
    phdrs.append(list(struct.unpack_from("<8I", d, o)))   # type off vaddr paddr filesz memsz flags align
shdrs = []
for i in range(e_shnum):
    o = e_shoff + i * e_shentsize
    shdrs.append(list(struct.unpack_from("<10I", d, o)))  # name type flags addr off size link info align entsize

# executable LOAD segment (vaddr 0)
code = next(p for p in phdrs if p[0] == 1 and p[2] == 0)
text = next(s for s in shdrs if s[1] == 1 and s[3] == 0 and s[5] and s[2] & 4)   # PROGBITS, AX, addr 0
assert text[4] == code[1] and text[5] == code[4], "unexpected layout"
old_end_off = text[4] + text[5]
old_end_va = text[3] + text[5]
assert old_end_va % 32 == 0
N = len(K)

# ---- build new image: [..old text][kernel][rest] ----
new = bytearray(d[:old_end_off]) + K + bytearray(d[old_end_off:])

def shift(x):  # shift a file offset that lies beyond the old end of .text
    return x + N if x >= old_end_off else x

# header
struct.pack_into("<II", new, 0x1C, shift(e_phoff), shift(e_shoff))
# program headers (they live in the shifted tail)
for i, p in enumerate(phdrs):
    p = p[:]
    if p[0] == 1 and p[2] == 0:                  # executable code segment grows, offset unchanged
        p[4] += N; p[5] += N
    else:
        p[1] = shift(p[1])
    struct.pack_into("<8I", new, shift(e_phoff) + i * e_phentsize, *p)
# section headers
for i, s in enumerate(shdrs):
    s = s[:]
    if s[1] != 8 and s[4] >= old_end_off:        # not NOBITS and located after .text
        s[4] += N
    if s[1] == 1 and s[3] == 0 and s[5] == text[5] and s[4] == text[4] and (s[2] & 4):
        s[5] += N                                  # .text grows
    struct.pack_into("<10I", new, shift(e_shoff) + i * e_shentsize, *s)
# .dynamic: tags holding FILE offsets (HASH, STRTAB, SYMTAB, RELA, JMPREL)
dyn = next(p for p in phdrs if p[0] == 2)
dyn_off = shift(dyn[1]); dyn_sz = dyn[4]
pos = dyn_off
while pos < dyn_off + dyn_sz:
    tag, val = struct.unpack_from("<II", new, pos)
    if tag == 0:
        break
    if tag in (4, 5, 6, 7, 23):
        struct.pack_into("<II", new, pos, tag, shift(val))
    pos += 8

# trampoline over the start of the stock audio function; remaining slot bytes -> NOPs
SLOT = 0x4C0
assert len(T) <= 32
new[text[4]:text[4] + SLOT] = T + b"\x00" * (SLOT - len(T))

open(out, "wb").write(new)
print(f"grafted {N} kernel bytes at vaddr {old_end_va:#x}; trampoline {len(T)} bytes; new size {len(new)} (was {len(d)})")
print(f"kernel entry vaddr = {old_end_va:#x}")
