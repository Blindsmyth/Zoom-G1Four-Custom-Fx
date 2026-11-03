# Building, testing, installing

## Prerequisites

* macOS or Linux, **Python 3.12** with `construct==2.10.70 crcmod==1.7 pyelftools 'mido[ports-rtmidi]==1.3.3'`
  (use the rtmidi backend; portmidi corrupts file transfers).
* **TI C6000 CGT 8.3.12** (`scripts/install_ti_cgt.sh` downloads it on macOS; or install from TI). Set `TI_CGT`
  if it is not at `~/ti/c6000_8312/ti-cgt-c6000_8.3.12`. GNU `binutils` (`readelf`) is optional (used by the verification steps).
* A checkout of **[zoom-zt2](https://github.com/mungewell/zoom-zt2)** at `./zoom-zt2` (or set `ZT2_DIR`).
  Tested with commit `b1f63b0`.
* The stock **Chorus** effect from Zoom's server, which you fetch yourself (it is Zoom's property):

```bash
scripts/fetch_stock.sh        # -> stock/CHORUS.ZD2   (sha256 1d78ab6c1a6e909ded03a9401d4217fc468cc2f4f6b26093199688b324126c93)
```

## Build

```bash
source venv/bin/activate      # whatever your Python 3.12 env is
#            stage name     id          display  extra-cflags  declared-dsp-load(raw)
scripts/build_tape.sh 3 XXTAPES 0x060010e0 TapeChS ""      26.5     # stereo
scripts/build_tape.sh 3 XXTAPEM 0x060010f0 TapeChM -DMONO  15.0     # mono
```

Stages: `1` = fixed 1 ms delay + Mix (arena proof), `2` = flutter clamped to ±2 %, `3` = full ToTape9 ranges.
The result is `dist/<name>.ZD2` (git-ignored: it contains Zoom's Chorus). The build is deterministic: with the
stock `CHORUS.ZD2` above and the toolchain versions listed, rebuilding gives byte-identical files; verify with
`shasum -a 256 -c dist/SHA256SUMS`.

The build asserts: the kernel has **zero relocations**, `Fx_MOD_Chorus` is found in its symbol table, the
descriptor name is `"Chorus"` before patching. Afterwards compare the relocation set with the stock file's
(`readelf -r`) — it must be identical — and check there is no `SPLOOP` / `.switch` in the kernel region.

Estimate cost: `python scripts/loop_cost.py <file.code> <start> <end>` (static cycle count per sample/block).

## Test on your computer (no pedal)

```bash
tests/run.sh            # stereo build
tests/run.sh -DMONO     # mono build
```

Runs the fuzz test under ASan/UBSan (random garbage coefficients and arena memory; every output must be finite and
|x| < 8), the Tone-response check against the stock table, stereo width, and the pitch swing at several settings.
Needs `clang`. This tests the *logic*; it cannot test what the TI compiler generates — that is what the pedal is for.

## Install

Pedal in PC mode (Guitar Lab → Device → PC mode), **Guitar Lab closed**, **autosave OFF**, USB connected:

```bash
python scripts/install_zd2.py [--remove OLD.ZD2 ...] dist/XXTAPES.ZD2 dist/XXTAPEM.ZD2
```

It downloads the live effect list (saving it to `build/FLST_SEQ.ZT2.live`), removes/uploads files, registers the
new effects, uploads the list, with a timeout on every call and a clean-close on any stall. Don't touch the pedal
while it runs. G1 FOUR storage is ~3 MB; at ~97 % full uploads fail — delete unused effects first.
