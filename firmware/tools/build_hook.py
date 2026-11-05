#!/usr/bin/env python3
"""Build a G1 FOUR v2.00 OS with a Task_MIDI byte hook, plus its updater (docs/midi-clock-port.md).

usage: build_hook.py <variant> <out dir>     (needs TI CGT 8.3.12 in ~/ti/c6000_8312, pyelftools, and
       the MIDISTOMO and G1_UPDATER environment variables, see g1fw.py)

variants (sources in firmware/src/):
  diag_a  hook.c   at 0x1181F000, vars 0x1181F400:  24 clocks -> tempo +1, CC 80 v -> tempo 40+v.  boot "a2.00"
  clock   clock.c  at 0x1181E000, state 0x1181F000: follows MIDI clock (follower.h, sim.c).        boot "c2.00"

Common patch:
  0xC012932C  Task_MIDI loop end: LDW *+B14[39],B0  ->  CALLP 0xC01309F0,B3
  0xC01309F0  16 zero bytes after a return            ->  far jump to diag_entry (entry.asm)
  boot screen version format "v%s" (0xC017416C)       ->  "<letter>%s"
"""
import struct
import subprocess
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import g1fw  # noqa: E402

VARIANTS = {
    "diag_a": dict(src="hook.c", cmd="link.cmd", code=0x1181F000, data=0x1181F400, data_size=32,
                   letter=b"a", app="ZOOM G1 FOUR DiagA Updater"),
    "clock": dict(src="clock.c", cmd="link_clock.cmd", code=0x1181E000, data=0x1181F000, data_size=1024,
                  letter=b"c", app="ZOOM G1 FOUR MidiClock Updater"),
    # clock + MIDI transport block (transport.h) at 0x1181FF00: magic, G1 layout tag, the rest 0
    "transport": dict(src="clock.c", cmd="link_clock.cmd", code=0x1181E000, data=0x1181F000, data_size=1024,
                      letter=b"u", app="ZOOM G1 FOUR Transport Updater",
                      extra=[(0x1181FF00, struct.pack("<16I", 0x5A4D5401, *([0] * 8), 0x47310001, *([0] * 6)))]),
}
TI = Path.home() / "ti/c6000_8312/ti-cgt-c6000_8.3.12/bin"
SRC = g1fw.FW / "src"
import hashlib  # noqa: E402
HOOK_SITE = 0xC012932C
# stock bytes are checked by SHA-256 only (no Zoom bytes in this file):
HOOK_SHA = "7084cc60946ceb886d6d99eb9b8e6e8fe8f8b4a46eb1b9a8f77abdd10791992b"        # the 4 bytes at HOOK_SITE
TRAMP_SHA = "71bcbc6a632dfbd699a93328e6daad7696c83cfb65e7e840c0ddb396c99454c2"      # the 20 bytes at TRAMP - 4 (a return, then 16 zero bytes)
TRAMP = 0xC01309F0
VERSION_FMT = 0xC017416C


def run(*cmd):
    r = subprocess.run([str(c) for c in cmd], cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        g1fw.fail(f"{cmd[0]} failed:\n{r.stdout}\n{r.stderr}")
    return r.stdout


variant, out_dir = sys.argv[1], Path(sys.argv[2]).resolve()
v = VARIANTS[variant]
obj = g1fw.FW / "build" / variant
obj.mkdir(parents=True, exist_ok=True)
stem = Path(v["src"]).stem

# --- compile the hook ---------------------------------------------------------
run(TI / "cl6x", "-mv6740", "--abi=eabi", "-O2", "--mem_model:data=far", f"-I{TI.parent}/include",
    f"-fr={obj}", f"-fs={obj}", "-k", v["src"], "entry.asm")
run(TI / "lnk6x", "--abi=eabi", "-o", obj / "hook.elf", "-m", obj / "hook.map",
    obj / f"{stem}.obj", obj / "entry.obj", v["cmd"])
elf = ELFFile(open(obj / "hook.elf", "rb"))
entry = elf.header["e_entry"]
blob = bytearray()
for s in sorted((s for s in elf.iter_sections() if s["sh_flags"] & 2 and s["sh_size"]), key=lambda s: s["sh_addr"]):
    if s.name not in (".text", ".diagtext"):
        g1fw.fail(f"unexpected allocated section {s.name}: the hook must not use data sections")
    off = s["sh_addr"] - v["code"]
    blob += bytes(off - len(blob)) + s.data()
blob += bytes(-len(blob) % 32)  # whole fetch packets
if v["code"] < v["data"] < v["code"] + len(blob):
    g1fw.fail("hook code runs into its data")
dis = run(TI / "dis6x", obj / "hook.elf")
(obj / "hook.dis").write_text(dis)
for bad in ("B14", ".bss", ".far", ".const", "__c6xabi", "__TI"):
    users = [l for l in dis.splitlines() if bad in l and "*+B14[166]" not in l and "*+B14[39]" not in l]
    if users:
        g1fw.fail(f"hook code uses {bad}:\n" + "\n".join(users))

# --- patch the OS -------------------------------------------------------------
stock, img, tail = g1fw.load_stock_os()


def word(a):
    return struct.unpack("<I", img.read(a, 4))[0]


def sha(a, n):
    return hashlib.sha256(img.read(a, n)).hexdigest()


if sha(HOOK_SITE, 4) != HOOK_SHA:
    g1fw.fail("hook site does not match the v2.00 OS")
if sha(TRAMP - 4, 20) != TRAMP_SHA:
    g1fw.fail("trampoline site does not match the v2.00 OS")
if img.read(VERSION_FMT, 4) != b"v%s\0":
    g1fw.fail("version format is not 'v%s'")
img.write(VERSION_FMT, v["letter"] + b"%s\0")
img.write(HOOK_SITE, struct.pack("<I", g1fw.callp_s2_b3(HOOK_SITE, TRAMP)))
img.write(TRAMP, struct.pack("<4I", g1fw.mvkl_s2(entry, 0), g1fw.mvkh_s2(entry, 0), g1fw.b_s2_reg(0), g1fw.NOP5))
img.add_section(v["code"], bytes(blob))
img.add_section(v["data"], bytes(v["data_size"]))
for addr, data in v.get("extra", []):
    img.add_section(addr, data)
new_os = g1fw.finish_os(img, tail, stock)

app = g1fw.build_updater(out_dir, v["app"], new_os)
print(f"{variant}: hook code {len(blob)} B at 0x{v['code']:08X}, entry 0x{entry:08X}, "
      f"data {v['data_size']} B at 0x{v['data']:08X}")
print(f"Main.bin {len(new_os)} B (stock {len(stock)}), md5 {g1fw.md5(new_os)}")
print("wrote", app)
