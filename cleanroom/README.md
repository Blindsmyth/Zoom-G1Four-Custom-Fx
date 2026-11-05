# Clean-room effects

Effects in this folder contain **no Zoom bytes at all**: no stock effect, no graft, no handlers or tables taken
from one. They are built with [Stomphacks](https://github.com/thammer/stomphacks)' pipeline (MIT, Thomas Hammer),
which generates the entry stub, init, bypass, parameter table, edit handlers and the container from its own
documented ABI. Stomphacks targets the MS Plus pedals; `scripts/g1_retarget.py` then changes the one header field
the G1 FOUR checks (the target bitfield, which needs bit `0x2`) and recomputes the CRC. Nothing else changes.

This is the route to a Modwerk-listable effect (an effect from original source that runs on the stock firmware)
and the base of the NAM port (nam2zoom's A2 Lite host is built on the same pipeline).

```bash
PYTHON=../../zt2-venv/bin/python ZT2_DIR=../../zoom-zt2 scripts/build_cleanroom.sh gain
```

Output: `dist/cleanroom/<FILE>.ZD2` and Stomphacks' `REPORT.md`, then the G1 pre-flight check. The build is
reproducible: the same sources give the same bytes. Needs TI C6000 CGT (8.3.12 works; Stomphacks documents 8.5.0)
and Pillow in the Python environment.

| Effect | File / ID | What it is | Source |
| --- | --- | --- | --- |
| CR Gain | `XXSHGAIN.ZD2` / `0x06001100` (MODULATION, test range) | one-knob level trim, 80 = unity | Stomphacks `effects/gain/gain.c`, unchanged (MIT, see `LICENSE.stomphacks`) |

## Known differences from G1 FOUR stock effects (untested on the pedal)

- **Picture records.** The in-code image info has one record (a blank 24×30 tile); G1 stock effects fill three
  (the 23×30 tile, a 20×10 category icon and a 24×22 add/delete icon). If the browser crashes or draws garbage,
  this is the first suspect.
- **`ICON` section** holds Stomphacks' 72×97 frame, not the G1's 23×30 BMP. The G1 OS reads `INFO` and `DATA` and
  appears to skip the rest; Guitar Lab may show it oddly.
- **`DT_BIND_NOW`** is set (from Stomphacks' link flags), which no stock effect does. Probably harmless to the loader.
- **No `ctx[7]`/`ctx[8]` shuttle copy.** G1 stock effects do one per frame; the Stomphacks kernel touches only the
  bus. Its purpose on the G1 is unknown.
- **Header opaque fields are zero** (Stomphacks' hardware-proven choice on the MS-70CDR+).

If the first test crashes, change one of these at a time, in that order.
