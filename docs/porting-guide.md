# Porting guide: make your own effect the same way

This is the recipe we ended with. It reuses a stock effect as a *shell* — its descriptor, parameter edit
handlers, init, on/off, icon and text stay byte-for-byte — and replaces only the audio function.

## 1. Pick a donor

The donor decides your knobs (names, ranges, count) and the coefficients your kernel can read. We used the
stock **Chorus** (Depth, Rate, Tone, Mix). Pick one whose knobs resemble what you need; you cannot add
knobs without writing edit handlers (not attempted here; the MS-series projects found hand-made handlers
a common source of freezes).

Download it from Zoom's server (`https://www.zoom.co.jp/archive/GUITAR_LAB/ZDLF6/<NAME>.zip`) and look at it:

```bash
python zoom-zt2/decode_effect.py -s CHORUS.ZD2                 # id, name, version, DSP load
python zoom-zt2/decode_effect.py -c chorus.code CHORUS.ZD2     # the ELF
dis6x --all --realquiet chorus.code | less                      # read its audio function + edit handlers
readelf -W -S -r chorus.code
```

Work out, from the edit handlers and the audio function, which coefficient words your kernel will read and what
they mean (e.g. Depth/Rate are *derived integers* here, see [abi-g1four.md](abi-g1four.md)). Tabulate their
values across each knob's full range before you decide what is "valid".

## 2. Write the kernel (`src/yourfx.c`)

```c
#pragma CODE_SECTION(Fx_MOD_Chorus, ".text")
void Fx_MOD_Chorus(unsigned *S, unsigned *ctx);   /* S = instance, ctx = host context */
```

* Read `ctx[1]` (32 floats, L then R), write it back in place; do the `ctx[8]→*ctx[7]` shuttle every frame.
* **Position independent**: no globals, no `static` data, no tables, no library or helper calls, no `switch`,
  no `double`, no float division or integer `/`,`%`, no unsigned↔float casts, no libm. Implement `sin`,
  reciprocal, etc. with polynomials / Newton steps. The build fails if the object has relocations.
* **Fail closed** (see [findings.md §3](findings.md)): init-ran check → silence; range-check every coefficient
  with `!(x>=lo && x<=hi)`; clamp every output sample; keep all state in the delay arena behind a magic number,
  clear it lazily, mask all ring indices.
* Avoid `SPLOOP` (software-pipelined) loops: compile with `-O2 -mu` (or `-O1`).
* Keep the kernel under ~16 KB in total; whole files above ~32 KB hung an MS-60B on boot.

Test it on your computer first: `tests/run.sh` shows how to build the kernel with `-DHOST_TEST` and drive
it with garbage and with signals (`tests/*.c`).

## 3. Graft it in

```bash
scripts/build_tape.sh <stage> <FILE8> <0x06001xxx> "<Name up to 11 chars>" "<extra cflags>" <dspload>
```

`build_tape.sh` is specific to the Chorus donor in three places you must adapt for another donor:

| In | Chorus value | What to check in your donor |
|---|---|---|
| `elf_graft.py` `SLOT` | `0x4C0` | the stock audio function starts at `.text+0` and is this long; the trampoline is written there and the rest zeroed |
| `build_tape.sh` | 12-byte name field patched at `.const+0x38`, expecting `"Chorus"` | where your donor keeps its name field |
| kernel symbol | `Fx_MOD_Chorus` | any name; the script looks the entry up in the object's symbol table |

If the audio function isn't at `.text+0` in your donor, extend `elf_graft.py` to patch the trampoline at the right
offset (and make sure you don't clobber code that is still used).

## 4. Verify before it goes near a pedal

```bash
readelf -W -r stock.code ; readelf -W -r graft.code      # identical relocation sets
dis6x --all --realquiet graft.code | grep -c SPLOOP       # 0
python scripts/loop_cost.py graft.code <start> <end>      # cycles per sample vs the stock effect
python zoom-zt2/decode_effect.py -s dist/NAME.ZD2         # id / group / name / dspload as intended
```

Then install, test in an unsaved patch with autosave off ([safety.md](safety.md)), and keep the first build boring:
passthrough, then a trivial audible change, then the real algorithm one piece at a time. That order is exactly
how we found out which part of our pipeline was broken.

## 5. Declare an honest DSP load

`INFO.dspload` is trusted. Estimate with `scripts/loop_cost.py` relative to the stock effect's loop and set it
with `scripts/set_dspload.py FILE.ZD2 RAW` (raw units; the app shows raw/2.5 %).
