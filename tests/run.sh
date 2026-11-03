#!/usr/bin/env bash
# Host-side tests of the DSP kernel (src/tapechorus.c) on your computer: no pedal, no TI compiler needed.
#   - fuzz:   400 instances x 600 blocks of random garbage coefficients / arena memory under
#             AddressSanitizer + UBSan; every output sample must be finite and |x| < 8
#   - tone:   kernel response vs the ideal stock tone chain (HP x LP) at all 11 Tone positions
#   - stereo: wet-signal width for mono and stereo input
#   - pitch:  measured pitch swing at several Depth/Rate settings
# usage: tests/run.sh [-DMONO]      (needs clang)
set -euo pipefail
cd "$(dirname "$0")"
SRC=../src/tapechorus.c
FLAGS="-DSTAGE=3 -DHOST_TEST ${1:-}"
CC="clang -O1 $FLAGS"
echo "== fuzz (ASan+UBSan) =="
clang -g -O1 $FLAGS -fsanitize=address,undefined -fno-sanitize=alignment -o ${TMPDIR:-/tmp}/tcfuzz fuzz.c $SRC -lm && ${TMPDIR:-/tmp}/tcfuzz
echo "== tone response (kernel / ideal, dB) =="
$CC -o ${TMPDIR:-/tmp}/tctone tone_test.c $SRC -lm && ${TMPDIR:-/tmp}/tctone
echo "== stereo width =="
$CC -o ${TMPDIR:-/tmp}/tcstereo stereo_test.c $SRC -lm && ${TMPDIR:-/tmp}/tcstereo
echo "== pitch swing (host_test args: depth(264..423) rate(9739..545392) mix) =="
$CC -o ${TMPDIR:-/tmp}/tcpitch host_test.c $SRC -lm
for a in "264 545392 1" "340 300000 1" "423 545392 1" "423 100000 0"; do ${TMPDIR:-/tmp}/tcpitch $a; done
