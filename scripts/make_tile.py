#!/usr/bin/env python3
"""Draw the 23x30 effect tile ("Tape / Flut" + a wave pictogram) for the Tape Flut effects.

The pedal's on-screen tile (`picTotalDisplay_*` in .const) is 23 columns x 4 pages: column-major, one byte
= 8 vertical pixels, bit 0 = top row of the page.  The ZD2 container's ICON section holds the same picture
as a 23x30 1-bit BMP, drawn inverted (palette index 1 = white).

usage: make_tile.py {stereo|mono} --preview
       (imported by build_tape.sh via:  make_tile.py {stereo|mono} --tile OUT.bin --bmp STOCK_ICON.bmp OUT.bmp)
"""
import math, sys

W, H, PAGES = 23, 28, 4            # drawn area 23x28; the tile is 23x32 with the last 4 rows blank
FONT = {                            # 3x5 glyphs, as in the stock tile's letters
    "T": ["###", ".#.", ".#.", ".#.", ".#."],
    "A": [".#.", "#.#", "###", "#.#", "#.#"],
    "P": ["##.", "#.#", "##.", "#..", "#.."],
    "E": ["###", "#..", "##.", "#..", "###"],
    "F": ["###", "#..", "##.", "#..", "#.."],
    "L": ["#..", "#..", "#..", "#..", "###"],
    "U": ["#.#", "#.#", "#.#", "#.#", "###"],
}

def canvas(variant):
    px = [[0] * W for _ in range(PAGES * 8)]
    # rounded border, like the stock tile
    for x in range(1, W - 1): px[0][x] = px[H - 1][x] = 1
    for x in (0, 1, W - 2, W - 1): px[1][x] = px[H - 2][x] = 1
    for y in range(2, H - 2): px[y][0] = px[y][W - 1] = 1
    def text(s, y0):
        x0 = (W - (4 * len(s) - 1)) // 2
        for i, ch in enumerate(s):
            for dy, row in enumerate(FONT[ch]):
                for dx, c in enumerate(row):
                    if c == "#": px[y0 + dy][x0 + 4 * i + dx] = 1
    text("TAPE", 3)
    text("FLUT", 10)
    # flutter pictogram, rows 17..24: a sine ("wow") -- two anti-phase sines for stereo
    xs = range(3, W - 3)
    def wave(phase, cy=20, amp=2.4):
        prev = None
        for x in xs:
            y = int(round(cy - amp * math.sin(2 * math.pi * (x - 3) / 17.0 * 1.5 + phase)))
            lo, hi = (y, y) if prev is None else (min(prev, y), max(prev, y))
            if prev is not None and hi - lo > 1:       # steep part: connect with a vertical run (half way)
                mid = (lo + hi) // 2
                for yy in range(lo, mid + 1): px[yy][x - 1 if prev < y else x] = 1
                for yy in range(mid, hi + 1): px[yy][x if prev < y else x - 1] = 1
            px[y][x] = 1
            prev = y
    wave(0.0)
    if variant == "stereo":
        wave(math.pi)
    return px

def tile_bytes(px):
    return bytes(sum(px[p * 8 + b][x] << b for b in range(8)) for p in range(PAGES) for x in range(W))

def bmp_bytes(px, stock_bmp):
    hdr = stock_bmp[:62]
    rows = []
    for y in range(29, -1, -1):                    # BMP is bottom-up, 30 rows
        bits = [0 if (y < PAGES * 8 and px[y][x]) else 1 for x in range(W)]   # inverted: lit pixel -> 0
        v = 0
        for x, b in enumerate(bits): v |= b << (31 - x)
        rows.append(v.to_bytes(4, "big"))
    return hdr + b"".join(rows)

def ascii_art(px, rows=H):
    return "\n".join("".join("#" if c else "." for c in px[y]) for y in range(rows))

if __name__ == "__main__":
    v = sys.argv[1]; a = sys.argv[2:]
    px = canvas(v)
    if "--preview" in a: print(ascii_art(px))
    if "--tile" in a:
        open(a[a.index("--tile") + 1], "wb").write(tile_bytes(px))
    if "--bmp" in a:
        i = a.index("--bmp"); open(a[i + 2], "wb").write(bmp_bytes(px, open(a[i + 1], "rb").read()))
