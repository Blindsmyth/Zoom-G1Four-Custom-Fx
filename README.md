# Zoom G1 FOUR custom effects

Custom effects for the **Zoom G1 FOUR** guitar pedal, plus everything we learned about how to
make them without bricking (or crashing) the pedal.

The first effect is **Tape Chorus**: the wow & flutter section of Airwindows
[ToTape9](https://www.airwindows.com/totape9/), ported to the G1 FOUR with a dry/wet knob, in a
**stereo** and a cheaper **mono** version. It runs on real hardware.

> **Unofficial, use at your own risk.** Zoom does not support custom effects. A broken effect in a
> *saved* patch can brick a pedal. Read [docs/safety.md](docs/safety.md) before you upload anything.

## Tape Chorus

| | `Tape FlutS` (stereo) | `Tape FlutM` (mono) |
|---|---|---|
| File / ID | `XXTAPES.ZD2` / `0x060010e0` | `XXTAPEM.ZD2` / `0x060010f0` |
| Category | MODULATION | MODULATION |
| Delay lines / sweeps | one per channel, cross-linked random speeds (as ToTape9) | one |
| Wet signal | different in each channel (wide) | identical in both |
| Estimated cost (static count) | ~272 cycles/sample | ~146 cycles/sample (stock Chorus: ~183) |
| Declared DSP load | 10.6 % (26.5 raw) | 6.0 % (15.0 raw) |

Knobs (they reuse the stock Chorus' four knobs and edit handlers):

| Knob | What it does |
|---|---|
| **Depth** | flutter depth, ToTape9 law `D^6 × 50` samples (max ±50 samples) |
| **Rate** | flutter speed, ToTape9 law `0.02 × E^3` rad/sample (up to ~137 Hz × random 0.24–0.98) |
| **Tone** | the stock Chorus tone chain: 0–6 sweep a low-pass 250 Hz → 10 kHz, 7–10 open it and add a treble shelf (up to +6 dB) with bass cut |
| **Mix** | dry/wet crossfade (0 = dry, 100 = wet) |

**Defaults** for a newly added effect: Depth 35, Rate 38, Tone 7, Mix 100. The on-screen tile reads
"TAPE / FLUT" with a flutter-wave pictogram (one wave = mono, two interleaved waves = stereo).

Full-range settings are deliberately as extreme as the original plugin: at maximum Depth *and* Rate the
pitch swings from about −68 % to +96 % ("modulated to death" — that is ToTape9). The knobs are
gentle until the top of their travel because of the plugin's cubic and sixth-power curves.

### Installing

**This repo contains no Zoom files and no prebuilt `.ZD2`.** The effect is built on top of the stock Zoom
Chorus (see [NOTICE.md](NOTICE.md)), so you fetch that from Zoom's own server and build locally; the result is
byte-identical to ours (check against [`dist/SHA256SUMS`](dist/SHA256SUMS)). Full steps in
[docs/building.md](docs/building.md); in short:

```bash
scripts/fetch_stock.sh                                                    # stock Chorus from Zoom's server
scripts/build_tape.sh 3 XXTAPES 0x060010e0 "Tape FlutS" ""     26.5          # -> dist/XXTAPES.ZD2 (stereo)
scripts/build_tape.sh 3 XXTAPEM 0x060010f0 "Tape FlutM" -DMONO 15.0            # -> dist/XXTAPEM.ZD2 (mono)
shasum -a 256 -c dist/SHA256SUMS
# pedal in PC mode, Guitar Lab closed, AUTOSAVE OFF, nothing experimental in a saved patch
python scripts/install_zd2.py dist/XXTAPES.ZD2 dist/XXTAPEM.ZD2
```

Look for **Tape FlutS** / **Tape FlutM** under MODULATION. Test in an unsaved patch. To remove:
`python scripts/install_zd2.py --remove XXTAPES.ZD2 --remove XXTAPEM.ZD2`.

## MIDI clock: ClkSlicer and the OS patch (research)

**ClkSlicer** is a 16-step Euclidean gate that stays locked to a DAW's bar over USB MIDI clock
([docs/clock-slicer.md](docs/clock-slicer.md)). It needs the **MIDI clock OS patch** in [`firmware/`](firmware/README.md),
which makes the pedal's master tempo follow MIDI clock and publishes Start/Stop and the clock count for
effects. The patch is built on your computer from Zoom's own updater, and Zoom's stock updater restores the
pedal. It is research, at your own risk.

## What is in this repo

| Path | |
|---|---|
| [`src/tapechorus.c`](src/tapechorus.c) | the DSP kernel (stereo; `-DMONO` for mono) |
| [`src/poc.c`](src/poc.c) | the two proof-of-concept kernels (passthrough, gain 0.5) |
| [`scripts/`](scripts) | build (`build_tape.sh`), ELF graft (`elf_graft.py`), installer (`install_zd2.py`), cycle estimator, … |
| [`tests/`](tests) | host-side tests: fuzz (ASan/UBSan), Tone response, stereo width, pitch swing |
| [`dist/SHA256SUMS`](dist/SHA256SUMS) | checksums of the expected build output (the `.ZD2` files themselves are not in the repo) |
| [`docs/`](docs) | the findings |

## The findings (start here if you want to make your own effect)

1. **[docs/findings.md](docs/findings.md)** — the story and the lessons. The headline: *do not
   disassemble → reassemble → relink a stock effect; graft your compiled code into the stock
   ELF instead.* Plain recompiles of the stock Chorus crashed the pedal, byte-patched grafts did not.
2. **[docs/hardware-test-log.md](docs/hardware-test-log.md)** — every build we put on the pedal and what happened.
3. **[docs/abi-g1four.md](docs/abi-g1four.md)** — instance/context layout, the stock Chorus coefficient
   table, the Tone table, the delay-memory arena, container/ID/DSP-load notes.
4. **[docs/safety.md](docs/safety.md)** — rules and a safe install procedure.
5. **[docs/porting-guide.md](docs/porting-guide.md)** — how to build your own effect the same way.
6. **[docs/building.md](docs/building.md)** — toolchain, build, test, install.

## Tested on

One Zoom G1 FOUR (the maintainer's). Everything marked *confirmed* in the docs was observed on that
pedal. Other Zoom pedals (G1X FOUR, MS-series, …) use related but different firmware: a lot of the
MS-series knowledge in the linked projects carried over, but do not assume anything.

## Credits

* **Chris Johnson / [Airwindows](https://www.airwindows.com/)** — ToTape9 (MIT). The flutter algorithm is his.
* **[mungewell/zoom-zt2](https://github.com/mungewell/zoom-zt2)** — file format, MIDI SysEx file API, effect tooling. We use `decode_effect.py` / `zoomzt2.py`.
* **[thammer/stomphacks](https://github.com/thammer/stomphacks)** — the *live-preview hazard*, guard-word and output-clamp rules, ZD2 ABI notes, safe-install practice. Several of our fixes come straight from it.
* **[repeat98/ZoomMultistompZDL](https://github.com/repeat98/ZoomMultistompZDL)** (and the forks derived from it) — loader notes, the `ctx[3]` delay-arena finding, safe DSP rules.
* TI C6000 code generation tools 8.3.12 (free download from TI).

## License

Our code and documentation: MIT, see [LICENSE](LICENSE). Third-party notices and the Zoom intellectual-property
note: [NOTICE.md](NOTICE.md).
