# G1 FOUR effect ABI — what we know

Derived by disassembling the stock **Chorus** (`CHORUS.ZD2`, v1.50) with TI's `dis6x` and confirmed by
running our own code in its place on a G1 FOUR. **confirmed** = observed working on the pedal;
**inferred** = read from the stock code but not independently tested; **(other)** = from another project's
notes on MS-series pedals, plausible here but not verified on a G1 FOUR.

Related reading: [stomphacks `zd2-abi.md`](https://github.com/thammer/stomphacks/blob/main/docs/zd2-abi.md),
[ZoomMultistompZDL `build/ABI.md`](https://github.com/repeat98/ZoomMultistompZDL/blob/main/build/ABI.md).

## The audio call

`void Fx(uw *S /*A4*/, uw *ctx /*B4*/)` — standard C6000 EABI, called once per block. 44.1 kHz, float32,
**16 frames per channel** per call (**confirmed**).

| ctx word | meaning |
|---|---|
| `ctx[1]` | **effect bus**: 16 left floats at `[0..15]`, 16 right floats at `[16..31]`, processed in place (**confirmed**) |
| `ctx[7]` | pointer to a pointer: destination of the "current sample" shuttle |
| `ctx[8]` | pointer to 16 input words: the stock effect copies `ctx[8][i]` → `**ctx[7]` once per frame; we replicate it exactly (**confirmed** harmless; purpose partly unknown) |
| `ctx[9]`, `ctx[10]` | the stock Chorus writes its LFO phase to `*ctx[9]` and adds a modulation source `ctx[10][i]`; we leave them alone (no ill effect seen) |
| host services (function pointers at `ctx + 4·n`) | word 3 `ramp(dst,target,rate)`, word 13 `schedule(callback, 20, arg)` (used by the on/off handler; argument meaning guessed), word 30 float-map, word 31 int-map, word 40 `get_param(obj,idx)`, word 43 `memcpy`, word 44 `memset` (**inferred** from stock edit handlers; **(other)** same offsets documented for MS-70CDR+) |

Never write the other ctx slots (some are live firmware state). Never write `ctx[4]`.

## The instance `S`

| `S[n]` | meaning |
|---|---|
| `S[0]` | host parameter object (what `get_param` reads) |
| `S[1]` | float **coefficient table**, 30 words, copied from ROM defaults by init, then written by the edit handlers |
| `S[2]` | per-instance state; the stock init zeroes **56 bytes** (14 words). We do not use it. |
| `S[3]` | delay-line arena descriptor `{base, end, span_bytes}` (**confirmed** ≥ 4096 bytes usable on the G1 FOUR; **(other)** ≥ 705 KB on MS-70CDR+) |

### Coefficient table `S[1]` (stock Chorus)

Defaults come from `_Fx_MOD_Chorus_Coe` (30 words in `.const`).

| word | role | default / range |
|---|---|---|
| 0 | bypass crossfade, 1 = active (host ramps it) | 0 |
| 2 | output level | 1.0 |
| 3 | wet gate: host ramps it to 0 for ~20 ms after a Depth change, then back to 1 | 0 |
| 5, 26, 28 | untouched by any edit handler; **exactly 1.0f after init** → used as the "init has run" check | 1.0 |
| 7, 8, 9 | tone stage A (high-pass / treble shelf): `y = b0·x + b1·x1 + a1·y1` | see Tone table |
| 10, 11, 12 | tone stage B (low-pass): same form | see Tone table |
| 15 | **Rate** — `int`, LFO phase increment, 9739 … 545392 (≈ 0.1 … 5.6 Hz in the stock chorus), curve unmeasured | 501079 |
| 22 | **Depth** — `int`, **inverted**: 423 at Depth 0 … 264 at Depth 100, curve ≈ `D²` | 288 |
| 23 | Depth, second derived value (float; unused by us) | |
| 29 | **Mix** — float 0..1 (the stock effect adds wet to a unity dry; we crossfade) | 0 |
| others | stock-internal (LFO offsets etc.) | |

### Tone table (`ModChoToneTbl`, 11 entries × 6 floats → words 7..12)

Both stages are `y = b0·x + b1·x1 + a1·y1`; stage A runs first, then stage B.

| Tone | A: b0, b1, a1 | B (LP): b0 = b1, a1 | LP corner | LP DC gain |
|---|---|---|---|---|
| 0 | 0.9989, −0.9989, 0.9979 | 0.0247, 0.9650 | 250 Hz | 1.41 |
| 1 | same | 0.0433, 0.9312 | 500 Hz | 1.26 |
| 2 | same | 0.0747, 0.8668 | 1 kHz | 1.12 |
| 3 | same | 0.1255, 0.7491 | 2 kHz | 1.00 |
| 4 | same | 0.2030, 0.5941 | 3.5 kHz | 1.00 |
| 5 | same | 0.2712, 0.4577 | 5 kHz | 1.00 |
| 6 | same | 0.4634, 0.0731 | 10 kHz | 1.00 |
| 7 | same | 0.9965, −0.9929 | ≈ 22 kHz (open) | 1.00 |
| 8 | 0.9825, −0.9825, 0.9650 | open | | |
| 9 | 1.3363, −1.3363, 0.8921 | open | | shelf +3 dB |
| 10 | 1.7450, −1.7450, 0.7491 | open | | shelf +6 dB, bass −20 dB @ 100 Hz |

(Stage A at 0–7 is a ~15 Hz DC blocker; at 8–10 it becomes a rising treble shelf with bass cut.) The
kernel's measured response matches the ideal response of this chain to within 0.5 dB at every position
(`tests/tone_test.c`). Note `b0 > 1`, negative `a1`: do not write a "sanity" check that rejects them.

## The `.const` descriptor (stock Chorus layout, inferred)

`Chorus` (336 bytes) at `.const+0` is an array of 0x38-byte entries: entry 0 `OnOff`, entry 1 the effect
(name `"Chorus\0\0"` at `+0x38`), then `Depth`, `Rate`, `Tone`, `Mix` at `+0x70, +0xa8, +0xe0, +0x118`.
Code pointers are stored in the entries and relocated at load: `_onf` at `+0x1c`, `_init` at `+0x54`,
the audio function at `+0x58`, edit handlers inside the knob entries. `effectTypeImageInfo` (304 bytes at
`+0x150`) points at the on-screen picture, the category icon and the add/delete icon. We patch only the
8-byte name (`scripts/build_tape.sh` asserts it is `"Chorus"` first); the rest stays byte-identical.

## The container

* Header: `ZDLF`, length (always 120), CRC32, target bits, 73 unknown bytes, version (4 chars), group
  byte, **ID (uint32, little-endian)**, name (≤ 11 chars + padding), group name. Then sections `ICON`,
  `TXJ1`, `TXE1`, `INFO`, `DATA` (the ELF), `INF2`, `CCOE`, `PRMJ`, `PRME` (parameter JSON), trailer.
* **ID**: top byte = group (MODULATION = 6); low bits free. Use `0x0?001000–0x0?0013ff` for private
  builds (never a stock ID; check the stock list in zoom-zt2's `zoom_fx_AllZDL7/master.txt`).
* **`INFO.dspload`**: a float the pedal trusts for its budget (shown as `raw/2.5` %). Stock Chorus: 16.92.
* The pedal's effect browser shows **icons only**; the `name` field changes the catalogue entry, not the
  icon. (We have not drawn a custom icon yet.)

## Delay arena rules we follow

Validate before use: non-null, 4-byte aligned `base/end/span`, `end > base`, `span ≥ end−base`,
`4096 ≤ end−base ≤ 8 MB`; put a header with a magic and version at `base`; clear in 128-word chunks while
passing audio through; never index outside your own ring. See `src/tapechorus.c`.
