# Findings: making custom effects for the Zoom G1 FOUR

This is the story of getting Airwindows' ToTape9 flutter running on a G1 FOUR, written for whoever
wants to build their own effect. Everything here was observed on one G1 FOUR unless it says it
comes from another project. The raw log of every build we tried is in
[hardware-test-log.md](hardware-test-log.md).

## 1. What an effect is

A G1 FOUR effect is a `.ZD2` file: a container (header, icon, text, parameter JSON, DSP load) wrapped
around a **TI C6000 ELF shared object** — the code that runs on the pedal's DSP. The firmware loads the
ELF, applies its relocations, and calls into it through pointers it finds in a descriptor table in the
ELF's `.const` section: an init function, an on/off handler, an audio function and one edit handler per
knob. Installing an effect means uploading the `.ZD2` over USB-MIDI (SysEx file API) **and** adding it to
`FLST_SEQ.ZT2`, the pedal's effect list. [zoom-zt2](https://github.com/mungewell/zoom-zt2) does both.

The audio function is called once per 16-sample block at 44.1 kHz with two arguments: `A4` = the effect
instance (an array of pointers) and `B4` = the host context. The effect bus is 16 left floats followed by
16 right floats, processed in place. See [abi-g1four.md](abi-g1four.md).

## 2. The big lesson: graft, don't rebuild

Our first approach was the obvious one for a reverse-engineering project: disassemble a stock effect
(`dis6x`), fix the assembly up so it re-assembles, replace the audio function with our C, relink with
`lnk6x`, repack the container. It builds cleanly, its relocation table matches the original, and it
**crashed the pedal** every time.

We bisected on hardware (full log in [hardware-test-log.md](hardware-test-log.md)):

| Build | Result |
|---|---|
| stock Chorus bytes, only ID/name changed | works |
| stock Chorus disassembled → reassembled → relinked, *nothing else changed* | **crash** |
| …plus `.const` aligned to 8 like the stock file, plus DWARF/symbols stripped, plus the profiler symbols stock files export | **crash** (high-pitched tone) |
| stock bytes repacked through our container tool | works |
| stock ELF with a few dozen bytes of *our compiled kernel patched in place* | works |

We never found the root cause of the recompile crash. We diffed the reassembled audio function against
the original: all 311 instructions are identical, and the small functions differ only in re-encoding
(16-bit compact ↔ 32-bit) and alignment padding. Remaining suspects are the ELF layout (empty `NOBITS`
sections now mapped into the loadable segment, an extra `DT_BIND_NOW` tag, missing `__TI_pprof_*` dynamic
symbols) and timing-sensitive hand-scheduled code. **We stopped investigating because grafting avoids the
whole class of problem.**

**The graft** (`scripts/elf_graft.py`, `scripts/build_tape.sh`):

1. Compile the kernel with `cl6x`. It must be position independent (no relocations, no globals, no
   library calls); the build asserts zero relocations.
2. Append the compiled bytes to the end of the stock ELF's executable `.text` segment (virtual address =
   old end, 32-byte aligned) and shift every file offset that follows (program headers, section headers,
   the `.dynamic` tags that hold file offsets).
3. Overwrite the first bytes of the stock audio function with one `BNOP` trampoline to the new code.
4. Repack the container with the donor ELF (`decode_effect.py --donor-elf`), set the ID, name and declared DSP load.

Everything else — descriptor table, edit handlers, init, relocation records, symbol tables — stays
**byte-identical** to the stock effect, so none of it can be subtly wrong. We verify that after every build
(identical relocation set; only the expected offset fields differ).

If your kernel is small enough you can skip the append and patch it directly over the stock audio function
(`scripts/patch_audio_fn.py`, used for the proof-of-concept builds).

## 3. The pedal live-previews every installed effect

(From [stomphacks](https://github.com/thammer/stomphacks); we reproduced it.) While you scroll the effect
browser the pedal **instantiates and runs each installed effect**, so your audio function can be called
*before its init has run*: the coefficient table and state are garbage. An effect that trusts them can put
NaN/Inf on the bus (the firmware's float state then latches silent until a power-cycle) or write through a
garbage index. Rules that follow:

* Detect "init has run". We use three coefficient words that the stock init copies in from ROM and no edit
  handler ever writes (`P[5]`, `P[26]`, `P[28]` are all exactly `1.0f`); otherwise the kernel writes silence.
* Range-check **every** coefficient with `!(x >= lo && x <= hi)` (NaN fails every comparison).
* Clamp/scrub every sample you write: `if (!(x > -8 && x < 8)) x = 0`.
* Keep your own state **in the host delay arena behind a magic number**, never rely on the 56 bytes of
  instance state being initialised, and clear big memory lazily in small chunks.
* Index rings with a mask so no garbage value can leave the buffer.

Our first version broke all three; a crash while scrolling the browser is how it showed.

## 4. Delay memory

The instance's fourth pointer (`S[3]`) points at `{base, end, span_bytes}`: a **per-instance, writable
delay-line arena**. The stock Chorus uses it as its delay line. Other projects report ≥ 705 KB on MS-series
pedals; on the G1 FOUR we only *know* it is at least 4096 bytes (our kernel refuses to run on less and
works). We use 2.1 KB: a 96-byte header (magic, ring write index, sweep phases, random state, smoothing
state, tone-filter state) and a 256-word ring per channel.

## 5. Reusing the stock Chorus' knobs and curves

We did not write new edit handlers. The stock Chorus' handlers read the knob and write *derived* values into
the coefficient table; our kernel reads those back:

* **Depth** → `int P[22]`, and it is **inverted**: 423 at Depth 0 … 264 at Depth 100. (We first assumed the
  opposite; the maintainer heard the knob was backwards.) The host curve is ≈ `D²`, which conveniently makes
  ToTape9's `D⁶` equal `p³`.
* **Rate** → `int P[15]`, an LFO phase increment from 9739 to 545392. The curve between is monotonic but
  unmeasured; we map it linearly to ToTape9's `E`.
* **Mix** → `float P[29]`, 0..1. **Wet gate** `P[3]`: the host mutes it for ~20 ms after a Depth change.
  **Bypass fade** `P[0]`. **Level** `P[2]`.
* **Tone** → `P[7..12]`, a high-pass/shelf stage and a low-pass stage; see the table in
  [abi-g1four.md](abi-g1four.md). We first implemented only the low-pass and our own sanity check
  *silently rejected* positions 7–10 (negative `a1`, gain > 1), so half the knob did nothing. Always
  tabulate the stock coefficients over the whole knob range before writing a validity check.

## 6. Declared DSP load

The container's `INFO.dspload` is **declared, not measured**; the pedal refuses to add an effect to a patch
("Process Overflow") when declared loads sum past its budget. The app shows `raw / 2.5` as a percent. We
estimate real cost by counting cycles in the compiled sample loop (`scripts/loop_cost.py`) relative to the
stock Chorus (~183 cycles/sample, declared 16.92 raw) and declare proportionally. This is a *static
estimate*; if a patch crackles, raise it. Compiler settings matter a lot: `-O1` was ~2× slower than
`-O2 -mu` (`-mu` = no software pipelining; we avoid `SPLOOP` loops as a precaution).

## 7. Other things that bit us

* **IDs**: pick from the playground range `low16 0x1000–0x13ff`, group byte 6 = MODULATION, and avoid
  collisions with the stock list. An ID typo (`0x60010900`) produced a non-existent group; always parse the
  built file back and check.
* **Filenames**: ≤ 8 chars, and installing a file whose name already exists *silently keeps the old one* —
  our installer deletes first.
* **Effect list**: never write back a cached `FLST_SEQ.ZT2`. After you delete effects on the pedal the cache
  is stale and would re-add dangling entries. The installer downloads the live list first.
* **Stray MIDI**: `zoomzt2.file_download` assumes every received message is SysEx. A patch change from
  touching the pedal mid-transfer aborts it. The installer filters non-SysEx messages and has a timeout on
  every call, and tries a clean file-session close if the pedal goes quiet.
* **A crashed pedal** keeps enumerating on USB but answers nothing, not even an identity request. With
  autosave off, a power-cycle recovers it.
* **Shell**: `zsh` does not word-split an unquoted `$FLAGS` variable, so `cl6x $FLAGS` passes one bogus argument. Use arrays or `bash`.

## 8. What we have not figured out

* Why the reassembled stock effect crashes (§2).
* The exact meaning of `ctx[9]`/`ctx[10]` (the stock Chorus exports its LFO phase there; we don't touch them, no ill effect seen).
* The Rate knob curve, the arena size on the G1 FOUR, real DSP cost (we only have static estimates).
* The effect's on-screen icon: both Tape Chorus builds reuse the stock Chorus icon and only the *name* differs.
