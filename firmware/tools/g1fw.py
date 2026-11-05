"""Shared pieces for building modified G1 FOUR v2.00 updaters (see build_phase0.py for the first use).

Environment: MIDISTOMO = a zoom-ms-midistomp checkout (for zoomms/ais.py), G1_UPDATER = Zoom's v2.00 Mac
updater app from your own download. Nothing from Zoom is in this repository.

    load_stock_os(app)         -> (stock Main.bin bytes, parsed AIS image, tail bytes)
    build_updater(app, out_dir, name, new_os)
        copies the stock app, replaces Main.bin, updates its MD5 table entry, skips the Preset and
        bootloader steps (type 2, ignored by -[MainWindow DoUpdate]). Everything else stays stock.
    C6000 encoders for the few words patched by hand, each one checked by the callers with dis6x.
"""
import hashlib
import os
import shutil
import struct
import sys
from pathlib import Path

FW = Path(__file__).resolve().parents[1]          # firmware/
if not os.environ.get("MIDISTOMO"):
    sys.exit("set MIDISTOMO to a checkout of https://github.com/matujuice/zoom-ms-midistomp (MIT; its zoomms/ais.py parses AIS)")
sys.path.insert(0, os.environ["MIDISTOMO"])
from zoomms import ais  # noqa: E402

# Zoom's own "ZOOM G1 FOUR System v2.00 Updater.app" (Mac), from your own download; never committed
STOCK_APP = Path(os.environ.get("G1_UPDATER", ""))
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


def fail(msg):
    sys.exit("ERROR: " + msg)


def md5(b):
    return hashlib.md5(b).hexdigest()


def load_stock_os(app=None):
    app = Path(app) if app else STOCK_APP
    if not (app / "Contents" / "Resources" / "Main.bin").exists():
        fail("set G1_UPDATER to Zoom's 'ZOOM G1 FOUR System v2.00 Updater.app' from your own download")
    res = app / "Contents" / "Resources"
    for name, want in STOCK_MD5.items():
        if md5((res / name).read_bytes()) != want:
            fail(f"{name} is not the stock v2.00 file")
    stock = (res / "Main.bin").read_bytes()
    img = ais.parse(stock)
    tail = stock[img.end_offset:]
    if ais.build(img) + tail != stock:
        fail("AIS round trip of the stock Main.bin is not exact")
    return stock, img, tail


def finish_os(img, tail, stock):
    new = ais.build(img) + tail
    if len(new) > OS_LIMIT:
        fail(f"Main.bin is {len(new)} B, the OS area holds {OS_LIMIT}")
    # the bootloader acts on AIS opcode words: no new word may look like one
    olds = {struct.unpack_from("<I", stock, i)[0] for i in range(0, len(stock) - 3, 4)}
    for i in range(0, len(new) - 3, 4):
        w = struct.unpack_from("<I", new, i)[0]
        if (w >> 8) == 0x585359 and w not in olds:
            fail(f"new word 0x{w:08X} at 0x{i:X} looks like an AIS opcode")
    return new


def build_updater(out_dir, name, new_os, app=None):
    app = Path(app) if app else STOCK_APP
    exe = bytearray((app / "Contents" / "MacOS" / "EFX Updater").read_bytes())
    for i, step in enumerate(STOCK_STEPS):
        if struct.unpack_from("<4I", exe, ROM_PROCEDURE + 16 * i) != step:
            fail(f"_RomProcedure step {i} is not the stock one")
    old = STOCK_MD5["Main.bin"].encode()
    if exe[MD5_TABLE:MD5_TABLE + 32] != old or exe.count(old) != 1:
        fail("Main.bin MD5 not found exactly once at the table")
    exe[MD5_TABLE:MD5_TABLE + 32] = md5(new_os).encode()
    for i in SKIP_STEPS:
        struct.pack_into("<I", exe, ROM_PROCEDURE + 16 * i, 2)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_app = out_dir / f"{name}.app"
    if out_app.exists():
        fail(f"{out_app} already exists")
    shutil.copytree(app, out_app, symlinks=True)
    (out_app / "Contents" / "MacOS" / "EFX Updater").write_bytes(bytes(exe))
    (out_app / "Contents" / "Resources" / "Main.bin").write_bytes(new_os)
    (out_dir / "Main.bin").write_bytes(new_os)
    return out_app


# --- C674x encoders (32-bit, unconditional) -------------------------------
def callp_s2_b3(at, target, p=0):
    """CALLP .S2 target,B3. The displacement counts words from the fetch packet holding `at`."""
    disp = (target - (at & ~31)) >> 2
    if not -(1 << 20) <= disp < (1 << 20):
        fail(f"CALLP 0x{at:08X} -> 0x{target:08X} out of range")
    return 0x10000000 | ((disp & 0x1FFFFF) << 7) | 0x10 | 0x2 | p


def mvkl_s2(value, reg):
    return (reg << 23) | ((value & 0xFFFF) << 7) | 0x2A


def mvkh_s2(value, reg):
    return (reg << 23) | (((value >> 16) & 0xFFFF) << 7) | 0x6A


def b_s2_reg(reg):  # B .S2 Breg
    return (reg << 18) | 0x362 if reg else 0x362


NOP5 = 0x00008000
