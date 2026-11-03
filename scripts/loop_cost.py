#!/usr/bin/env python3
"""Static estimate of cycles per audio block for a function in a Zoom effect ELF (dis6x text).

Counts execute packets (instructions not starting with '||') plus extra NOP cycles, over the
largest backward-branch loop and the 16 iterations it runs.  Straight-line pre/post code is added.
usage: loop_cost.py FILE.code START_VADDR END_VADDR      (function bounds, hex)
"""
import os, re, subprocess, sys
TI = os.environ["TI_CGT"]
f, lo, hi = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
out = subprocess.run([TI + "/bin/dis6x", "--all", "--noaddr", "--realquiet", "--noaddr", f], capture_output=True, text=True).stdout
# need addresses: re-run with addresses
out = subprocess.run([TI + "/bin/dis6x", "--all", "--realquiet", f], capture_output=True, text=True).stdout
ins = []   # (addr, text)
labels = {}
for l in out.split("\n"):
    ml = re.match(r"^([0-9a-f]{8})\s+(\S+):\s*$", l)
    if ml: labels[ml.group(2)] = int(ml.group(1), 16)
for l in out.split("\n"):
    m = re.match(r"^([0-9a-f]{8})\s+(?:[0-9a-f]{4,8}\s+)?(.*)$", l)
    if not m: continue
    a = int(m.group(1), 16)
    if lo <= a < hi and l.split(None, 2)[-1] and "fphead" not in l:
        t = m.group(2).strip()
        if t and not t.endswith(":"):
            ins.append((a, t))
def cycles(rng):
    c = 0
    for a, t in rng:
        if t.startswith("||"): continue
        mm = re.match(r"(?:\[[^\]]*\]\s*)?NOP\s*(\d+)?", t)
        c += int(mm.group(1)) if mm and mm.group(1) else 1
    return c
loops = []
for a, t in ins:
    m = re.search(r"\[\s*!?[AB]\d\]\s+(?:B|BNOP|BDEC)\.S\d\s+\S+\s*\(PC[+-]\d+ = 0x([0-9a-f]+)\)", t)
    if m:
        v = int(m.group(1), 16)
        if v < a: loops.append((v, a))
big = max(loops, key=lambda x: x[1] - x[0]) if loops else None
total = cycles(ins)
if big:
    body = cycles([(a, t) for a, t in ins if big[0] <= a <= big[1] + 24])
    print(f"function {lo:#x}..{hi:#x}: {len(ins)} instrs, sample-loop {big[0]:#x}..{big[1]:#x} = ~{body} cycles/sample, "
          f"~{total - body + 16 * body} cycles/block (16 samples)")
else:
    print("no loop found; straight-line cycles", total)
