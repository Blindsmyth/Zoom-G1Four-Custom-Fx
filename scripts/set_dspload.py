#!/usr/bin/env python3
"""Set the declared DSP load (INFO.dspload, raw units; the app shows raw/2.5 as %) of a .ZD2 and
recompute its CRC.   usage: set_dspload.py FILE.ZD2 RAW [OUT.ZD2]"""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.environ.get("ZT2_DIR", os.path.join(ROOT, "zoom-zt2")))
import crcmod, zoomzt2
f, raw = sys.argv[1], float(sys.argv[2])
out = sys.argv[3] if len(sys.argv) > 3 else f
cfg = zoomzt2.ZD2.parse(open(f, "rb").read())
cfg["INFO"]["dspload"] = raw
data = zoomzt2.ZD2.build(cfg)
crc = crcmod.Crc(0x104c11db7, rev=True, initCrc=0, xorOut=0xFFFFFFFF); crc.update(data[12:-16])
cfg["checksum"] = crc.crcValue ^ 0xFFFFFFFF
open(out, "wb").write(zoomzt2.ZD2.build(cfg))
print(f"{os.path.basename(out)}: dspload {raw} raw")
