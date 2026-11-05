# G1 FOUR OS patches: MIDI clock follower and transport block

**Unofficial, research, at your own risk.** These scripts patch the G1 FOUR **v2.00** OS on your own computer,
from Zoom's updater that you downloaded yourself. They write a modified copy of that updater. This repository
contains no Zoom file and no Zoom code: stock bytes are only checked by MD5/SHA-256 and referred to by address.
The design follows [MIDISTOMP](https://github.com/matujuice/zoom-ms-midistomp)'s clock patch for the MS pedals
(MIT, by Luca/matujuice).

## What it does

| Variant | Boot screen | What it does |
|---|---|---|
| `build_phase0.py` | `m2.00` | two UI strings only. Proves a patched OS boots |
| `diag_a` | `a2.00` | probe: 24 MIDI clocks → +1 BPM, CC 80 v → tempo 40+v |
| `clock` | `c2.00` | the master tempo (rhythm, looper, synced effects) follows incoming USB MIDI clock |
| `transport` | `u2.00` | `clock` plus a MIDI transport block for custom effects ([`src/transport.h`](src/transport.h)) and a UI redraw on tempo changes |

**Clock follower** ([`src/follower.h`](src/follower.h), host-tested by [`src/sim.c`](src/sim.c)):
- locks 2 beats after the clock starts;
- follows jumps of 3% or more in about 2 beats, and smaller changes in 3–4 beats;
- applies a tempo only when it really changed (1 BPM hysteresis), because every apply restarts delays.

The pedal's tempo is a whole number from 40 to 250. `sim.c` runs 90 scenarios (40–250 BPM, jitter up to ±8 ms
per clock). All pass up to ±5 ms; the 2 it reports as failing are ±8 ms of jitter at 175.5 and 220 BPM, where
it re-applies a few times. Real USB MIDI jitter is far smaller.

**Transport block:** 16 words at `0x1181FF00`.
- Words 0–7 follow MIDISTOMP's layout: magic, sequence, running, starts, clocks since Start, tempo × 100.
- Words 8–9 are G1 additions: total clocks, layout tag.
- Effects read it with plain loads. [`../src/clockslicer.c`](../src/clockslicer.c) is the example.

**Patches in the OS:**
- a call at the end of the MIDI task's byte loop (`0xC012932C`);
- a 16-byte trampoline in padding (`0xC01309F0`);
- new code and state in unused L2 SRAM (`0x1181E000`–`0x1181FFFF`), loaded as extra AIS sections;
- the boot screen's version letter.

**Updater copy:**
- the new `Main.bin`;
- its MD5 table entry updated;
- the Preset and bootloader steps set to a type the updater skips.

The bootloader, your patches and the file system are never written. Notes are in the Zoom G1 Four workspace's
`docs/midi-clock-port.md`.

## Build

You need:
- TI C6000 CGT 8.3.12 in `~/ti/c6000_8312`;
- Python with `pyelftools`;
- a checkout of zoom-ms-midistomp (for its `zoomms/ais.py`);
- Zoom's **ZOOM G1 FOUR System v2.00 Updater.app** for Mac.

```bash
export MIDISTOMO=/path/to/zoom-ms-midistomp
export G1_UPDATER="/path/to/ZOOM G1 FOUR System v2.00 Updater.app"
python firmware/tools/build_hook.py transport out/transport
cd out/transport && xattr -cr "ZOOM G1 FOUR Transport Updater.app" && codesign --force --deep --sign - "ZOOM G1 FOUR Transport Updater.app"
```

The script refuses to run unless every stock file matches v2.00 by MD5 and the patch sites match by SHA-256.
The `transport` build should give `Main.bin` MD5 `851c57fdf35453c4bfb124d164d1d306`.

## Flash and recover

1. **Quit the DAW, Guitar Lab and every other MIDI app.** A DAW still sending clock made the transfer time out
   three times; Zoom's updater then crashes while trying to show the error.
2. Hold both arrow buttons while you plug in USB. The pedal shows FIRMWARE UPDATE.
3. Run the built updater, choose ZOOM G Series for MIDI IN and OUT, and click Execute. Wait for 8/8.

**Recovery:** the same steps with Zoom's stock v2.00 updater. It also rewrites the factory patch area, so back
up your patches first.

## Hardware results

One G1 FOUR, firmware 2.00, 5 November 2026:

| Build | Result |
|---|---|
| `m2.00` | boots, marked version shown |
| `a2.00` | MIDI clock reaches the MIDI task; CC 80 and clock change the tempo audibly; the screen doesn't redraw |
| `c2.00` | rhythm and synced delays follow a DAW's tempo; the RHYTHM screen's BPM only redraws on stop/start |
| `t2.00` | ClkSlicer stays locked to the DAW's bar |
| `u2.00` | flashed after closing the DAW (three earlier attempts timed out with a DAW sending clock); the live BPM redraw is not reported yet |
