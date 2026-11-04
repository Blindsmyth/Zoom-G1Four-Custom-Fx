#!/usr/bin/env python3
"""Replace the container's ICON (23x30 1-bit BMP, same size) and recompute the CRC.
usage: set_icon.py FILE.ZD2 ICON.bmp"""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.environ.get("ZT2_DIR", os.path.join(ROOT, "zoom-zt2")))
import crcmod, zoomzt2
f, bmp = sys.argv[1], open(sys.argv[2], "rb").read()
cfg = zoomzt2.ZD2.parse(open(f, "rb").read())
assert len(bmp) == cfg["ICON"]["length"] == 182, (len(bmp), cfg["ICON"]["length"])
assert bmp[:2] == b"BM"
cfg["ICON"]["data"] = bmp
data = zoomzt2.ZD2.build(cfg)
crc = crcmod.Crc(0x104c11db7, rev=True, initCrc=0, xorOut=0xFFFFFFFF); crc.update(data[12:-16])
cfg["checksum"] = crc.crcValue ^ 0xFFFFFFFF
open(f, "wb").write(zoomzt2.ZD2.build(cfg))
print(f"{os.path.basename(f)}: icon replaced")
