# Hardware test log

Every build we put on the pedal (one Zoom G1 FOUR), in order, and what happened. "Crash" = the pedal
froze or produced a loud high-pitched tone when the effect was scrolled past / loaded; a power-cycle
(autosave off) recovered it every time. IDs are in the playground range.

## Round 0 — full rebuild pipeline (disassemble → fix asm → cl6x/lnk6x → repack)

| Build | What it was | Result |
|---|---|---|
| `TAPECHO` v1, id `0x06FFFFF0` | stock Chorus shell reassembled, our tape kernel, our init wrapper | **crash while scrolling the effect browser** |
| `XXTAPCHO` v2, id `0x06001000` | same pipeline; added guard word, arena-resident state, chunked clear, sanitised coefficients, init wrapper moved into the asm to remove a `PCR_S21` relocation | **crash on load** |

At this point we had applied the live-preview safety rules and still crashed → suspected the pipeline.

## Round 1 — bisect (one change at a time, all installed together)

| Build | What it was | Result |
|---|---|---|
| `XXTCH0` | stock Chorus bytes, only ID/name changed (no rebuild) | **works** |
| `XXTCH1` | stock Chorus reassembled + relinked, nothing else changed | **crash** |
| `XXTCH2` | `XXTCH1` + init wrapper raising a guard word | crash |
| `XXTCH3` | `XXTCH2` + our C object with a shuttle-only audio stub | crash |

→ the plain recompile is already broken; the bug is not in our DSP or our init.

## Round 2 — cumulative fixes from the (MS-series, hardware-proven) stomphacks recipe

| Build | Added to `XXTCH1` | Result |
|---|---|---|
| `XXTCHA` | `.const` aligned to 8 (stock `p_align` = 8; ours was 4) | crash, high-pitched tone |
| `XXTCHB` | + DWARF/`.symtab`/`.strtab` stripped | crash, high-pitched tone |
| `XXTCHC` | + `__TI_pprof_*` symbols exported into `.dynsym` | crash, high-pitched tone |

→ none of the obvious ELF differences is the cause. Static diff: the reassembled audio function is
instruction-for-instruction identical to the stock one.

## Round 3 — proof of concept by byte patching (no rebuild)

| Build | What it was | Result |
|---|---|---|
| `XXPOC0` | stock ELF repacked unchanged through `decode_effect.py` | **works** |
| `XXPOC1` | stock ELF + 32-byte kernel patched over the audio function: shuttle, passthrough | **works**, clean dry sound |
| `XXPOC2` | same, 96-byte kernel: shuttle + gain 0.5 | **works**, clearly −6 dB → *our compiled code runs on the G1 FOUR* |

## Round 4 — grafting (code appended to the stock ELF, trampoline at the old entry)

| Build | What it was | Result |
|---|---|---|
| `XXTAPE1` (stage 1) | fixed ~1 ms / ~1.2 ms delay + Mix, state in the delay arena | **works** (loads, no crash; comb-filtering, no modulation — as designed) |
| `XXTAPE2` (stage 2) | flutter with pitch deviation clamped to ≈ ±2 % | works, but too tame: "I don't hear the extremes" |
| `XXTAPE3` (stage 3) | ToTape9's real ranges (`D⁶×50` samples, `0.02·E³` rad/sample) | works, "the desired sound" — but **Depth was reversed** |
| `XXTAPE3` rebuilt | Depth inverted (host value 423 → 264 is Depth 0 → 100) | works; "this is pretty good" |
| `XXTAPE3` rebuilt | full stock tone chain (HP/shelf + LP) per channel, separate L/R delay lines | works; "this is solid" |
| `XXTAPES` / `XXTAPEM` | stereo and mono builds, `-O2 -mu`, honest declared DSP loads (10.6 % / 6.0 %) | installed; maintainer: "this is great" |

## Installer incidents (nothing was damaged)

* Two installer runs stalled waiting for a reply from a pedal that had stopped answering (it was still
  frozen from a crashed effect). No transfer was in progress; the session was closed cleanly and the
  pedal recovered after a power-cycle. → every call in `install_zd2.py` now has a timeout.
* An effect-list download aborted on a stray non-SysEx MIDI message (somebody touched the pedal). →
  non-SysEx messages are now ignored, and the installer always works from the pedal's *live* list.
