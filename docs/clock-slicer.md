# ClkSlicer: a 16-step gate locked to MIDI clock

**Files:**
- kernel: [`src/clockslicer.c`](../src/clockslicer.c);
- host test: [`tests/slicer_test.c`](../tests/slicer_test.c);
- tile: `scripts/make_tile.py slicer`.

**Build** (a graft on the stock Chorus, like Tape Chorus, so fetch the donor first with
`scripts/fetch_stock.sh`):

```bash
KSRC="$PWD/src/clockslicer.c" TILE_VARIANT=slicer KNOBS="Hits:15:7 Step:5:2 Gate:10:5 Depth:100:100" \
  scripts/build_tape.sh 1 XXCSLICE 0x06001210 "ClkSlicer" "" 8.0
```

The result is `dist/XXCSLICE.ZD2` (MODULATION, ID `0x06001210`, declared load 8.0 raw = 3.2 %). Its SHA-256 is
in `dist/SHA256SUMS`.

## How it keeps phase

It reads the MIDI transport block that the [`firmware/`](../firmware/README.md) `transport` build publishes,
every 16 samples.

- **DAW playing (Start received, clocks coming):** the position is clocks since Start, plus samples since the
  last clock. Every clock re-anchors it, so it can't drift, and every Start restarts the pattern on the
  downbeat.
- **Clock without Start:** it stays on the beat grid, on the last Start's bar if there was one.
- **No clock for 0.5 s, stock firmware, or no clock at all:** it free-runs at the master tempo with no jump.
- **Late clock:** it slows to half speed rather than stepping back, so a chop never plays twice.

## Knobs

The pedal shows each value + 1.

| Knob | Range | What it does |
|---|---|---|
| Hits | 1–16 | open steps out of 16, spread evenly (Euclidean) |
| Step | 1–6 | 1/4, 1/8, 1/16, 1/32, 1/8 triplet, 1/16 triplet |
| Gate | 1–11 | how long each open step stays open (≈9 % … 100 %) |
| Depth | 0–100 | how far closed steps go down (100 = silence) |

Gain changes are smoothed over about 1 ms.

## Tests

**Host** (`cc -O2 -DHOST_TEST -o build/slicer_test tests/slicer_test.c -lm && build/slicer_test [bpm jitter_ms ppm]`).
The sweep covered:
- 40–250 BPM;
- 0, ±1 and ±4 ms of jitter per clock;
- pedal audio clock −150 or +100 ppm against the DAW;
- 5 minutes per run.

Results:
- Every bar's opening was within the clock's jitter of the DAW's bar, plus about 0.5 ms of smoothing.
- No drift, no double chops, and back on the downbeat after a new Start.
- A garbage-input fuzz run with ASan/UBSan gave no non-finite or loud output.

**Hardware:** on one G1 FOUR, firmware 2.00 with the `t2.00` transport build, 5 November 2026, the maintainer
reported "it stays locked" to the DAW.
