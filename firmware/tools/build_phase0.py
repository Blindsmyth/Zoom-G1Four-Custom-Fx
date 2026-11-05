#!/usr/bin/env python3
"""Phase 0 test build for the G1 FOUR v2.00 OS: change two UI strings and nothing else.

Proves that a modified Main.bin boots and that the patched updater flashes it.
  * boot screen version format "v%s" (0xC017416C, used only by the startup code 0xC014DC08) -> "m%s"
  * TEMPO popup title "TEMPO" (0xC0181698, used only by 0xC01648FA) -> "Tempo"

The updater copy:
  * Main.bin replaced (same size), its MD5 table entry updated (_MD5Tbl, offset 0x1F751)
  * _RomProcedure steps 1-4 (Preset.bin erase/write, bootloader erase/write) set to type 2,
    which -[MainWindow DoUpdate] skips (it acts on types 0 and 1 only)
  * the remaining steps are untouched: erase MAIN_INFO, erase+write OS, write MAIN_INFO
  * MAIN_INFO.bin, ROM.bin, Preset.bin, FS.bin are left byte-identical

usage: build_phase0.py <stock Updater.app> <out dir> <midistomo repo>
The output app must be re-signed on the Mac before it runs:
  xattr -cr <app> && codesign --force --deep --sign - <app>
"""
import hashlib
import shutil
import struct
import sys
from pathlib import Path

stock_app, out_dir, midistomo = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
sys.path.insert(0, midistomo)
from zoomms import ais  # noqa: E402

PATCHES = [  # (address, stock bytes, new bytes)
    (0xC017416C, b"v%s\0", b"m%s\0"),
    (0xC0181698, b"TEMPO\0", b"Tempo\0"),
]
STOCK_MD5 = {
    "Main.bin": "f714e6f9ec6f7edc465d2e9c5e0e9e39",
    "MAIN_INFO.bin": "c64c36fa24f8cff18b596965508934a8",
    "FS.bin": "edc06e42361b249ed193cc61eb529453",
    "Preset.bin": "94ca16b9f87db0605694b20351bb8380",
    "ROM.bin": "e43bd878c99dc4245c766c67ef47ad1c",
}
MD5_TABLE = 0x1F751
ROM_PROCEDURE = 0x20870
STOCK_STEPS = [  # type, flash addr, length, file (0 Main, 1 MAIN_INFO, 2 FS, 3 Preset, 4 ROM)
    (0, 0x0BF000, 0x1000, 255),
    (0, 0x028000, 0xB000, 255),
    (1, 0x028000, 0xB000, 3),
    (0, 0x000000, 0x28000, 255),
    (1, 0x000000, 0x28000, 4),
    (0, 0x033000, 0x8C000, 255),
    (1, 0x033000, 0, 0),
    (1, 0x0BF000, 0x1000, 1),
]
SKIP_STEPS = (1, 2, 3, 4)
OS_LIMIT = 0x8C000  # flash 0x033000-0x0BEFFF


def md5(b):
    return hashlib.md5(b).hexdigest()


def fail(msg):
    sys.exit("ERROR: " + msg)


res = stock_app / "Contents" / "Resources"
exe_path = stock_app / "Contents" / "MacOS" / "EFX Updater"
for name, want in STOCK_MD5.items():
    if md5((res / name).read_bytes()) != want:
        fail(f"{name} is not the stock v2.00 file")

# --- Main.bin -------------------------------------------------------------
stock_os = (res / "Main.bin").read_bytes()
img = ais.parse(stock_os)
tail = stock_os[img.end_offset:]  # anything after the final jump is kept as is
if ais.build(img) + tail != stock_os:
    fail("AIS round trip of the stock Main.bin is not exact")
for addr, old, new in PATCHES:
    if len(old) != len(new):
        fail("patch changes length")
    if img.read(addr, len(old)) != old:
        fail(f"0x{addr:08X}: expected {old!r}, found {img.read(addr, len(old))!r}")
    img.write(addr, new)
new_os = ais.build(img) + tail
if len(new_os) != len(stock_os) or len(new_os) > OS_LIMIT:
    fail("rebuilt Main.bin has the wrong size")
diff = [i for i in range(len(stock_os)) if stock_os[i] != new_os[i]]
expected = sum(sum(1 for a, b in zip(o, n) if a != b) for _, o, n in PATCHES)
if len(diff) != expected:
    fail(f"{len(diff)} bytes differ, expected {expected}")
# the bootloader scans for AIS opcodes word by word: make sure no new opcode-like word appears
for i in range(0, len(new_os) - 3, 4):
    w_new, w_old = struct.unpack_from("<I", new_os, i)[0], struct.unpack_from("<I", stock_os, i)[0]
    if w_new != w_old and (w_new >> 8) == 0x585359:
        fail(f"new word at 0x{i:X} looks like an AIS opcode")

# --- updater executable ---------------------------------------------------
exe = bytearray(exe_path.read_bytes())
for i, step in enumerate(STOCK_STEPS):
    got = struct.unpack_from("<4I", exe, ROM_PROCEDURE + 16 * i)
    if got != step:
        fail(f"_RomProcedure step {i} is {got}, expected {step}")
if exe[MD5_TABLE:MD5_TABLE + 32] != STOCK_MD5["Main.bin"].encode() or exe.count(STOCK_MD5["Main.bin"].encode()) != 1:
    fail("Main.bin MD5 not found exactly once at the table")
exe[MD5_TABLE:MD5_TABLE + 32] = md5(new_os).encode()
for i in SKIP_STEPS:
    struct.pack_into("<I", exe, ROM_PROCEDURE + 16 * i, 2)

# --- write ----------------------------------------------------------------
out_dir.mkdir(parents=True, exist_ok=True)
out_app = out_dir / "ZOOM G1 FOUR Phase0 Updater.app"
if out_app.exists():
    fail(f"{out_app} already exists")
shutil.copytree(stock_app, out_app, symlinks=True)
(out_app / "Contents" / "MacOS" / "EFX Updater").write_bytes(bytes(exe))
(out_app / "Contents" / "Resources" / "Main.bin").write_bytes(new_os)
(out_dir / "Main.bin").write_bytes(new_os)

print("Main.bin md5", md5(new_os), "bytes changed:", ", ".join(f"0x{i:X}" for i in diff))
for i in range(len(STOCK_STEPS)):
    t, a, n, f = struct.unpack_from("<4I", exe, ROM_PROCEDURE + 16 * i)
    print(f"step {i}: type {t} addr 0x{a:06X} len 0x{n:X} file {f}" + ("  (skipped)" if t == 2 else ""))
print("wrote", out_app)
