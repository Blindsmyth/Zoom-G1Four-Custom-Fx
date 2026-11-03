# Notices

## Airwindows ToTape9 (MIT)

The flutter algorithm in `src/tapechorus.c` is ported from **ToTape9** by Chris Johnson / Airwindows
(<https://www.airwindows.com/totape9/>, source in <https://github.com/airwindows/airwindows>), which is
published under the MIT licence:

> Copyright (c) airwindows, Airwindows uses the MIT license

Ported parts: the per-channel delay line with a sine sweep whose speed is re-drawn at random each cycle
(`0.24 + rand × 0.74`, each channel choosing between two candidates by comparing against the other channel's
sine), linear interpolation, depth law `D⁶ × 50` samples and speed law `0.02 × E³` rad/sample. The dry/wet crossfade,
the use of the stock Chorus tone chain, the arena/safety scaffolding and the mono variant are new.

## Zoom material is NOT in this repository

Tape Chorus is a **graft** into Zoom's stock G1 FOUR *Chorus* effect: the built `.ZD2` contains Zoom's compiled
code, icon, text and parameter data around our added code. Those files are Zoom's property, so:

* this repository contains **no** Zoom effect files and no prebuilt `.ZD2` (the `dist/*.ZD2` outputs are git-ignored);
* you download the stock effect from Zoom's own server yourself (`scripts/fetch_stock.sh`) and build locally;
* `docs/abi-g1four.md` records interoperability facts about the file/ELF layout and a few numeric tables
  (coefficient values) observed while making the effects work with the pedal, not Zoom's code.

"Zoom" and "G1 FOUR" are trademarks of ZOOM Corporation. This project is not affiliated with or endorsed by
ZOOM Corporation or Airwindows.

## Tools and prior work used (not included)

* [zoom-zt2](https://github.com/mungewell/zoom-zt2) (`decode_effect.py`, `zoomzt2.py`) — used by the build and install scripts.
* [stomphacks](https://github.com/thammer/stomphacks), [ZoomMultistompZDL](https://github.com/repeat98/ZoomMultistompZDL) — documentation we learned from; each has its own licence.
* TI C6000 Code Generation Tools 8.3.12 — TI's licence applies.
