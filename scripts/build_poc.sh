#!/usr/bin/env bash
# Proof-of-concept effects built by BINARY-PATCHING the stock Chorus ELF (no reassembly of stock code).
#   XXPOC0  stock ELF repacked unchanged (tests the repack path only)
#   XXPOC1  stock + our kernel, passthrough
#   XXPOC2  stock + our kernel, gain 0.5 (audible proof our code runs)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"
PYTHON="${PYTHON:-python3}"
[[ -f "$DONOR" ]] || [[ -n "${DONOR_OK:-}" ]] || true
DE="$ZT2_DIR/decode_effect.py"
DONOR="$STOCK_DIR/CHORUS.ZD2"
W="$OSS_PORT/build/poc"; mkdir -p "$W"
"$PYTHON" "$DE" -c "$W/stock.code" "$DONOR"
pack() { "$PYTHON" "$DE" --donor "$1" --donor-elf --force-name "$2" --force-id "$3" -o "$EFFECTS_DIR/$2.ZD2" "$DONOR" >/dev/null; echo "built $EFFECTS_DIR/$2.ZD2"; }
pack "$W/stock.code" XXPOC0 0x06001080
for n in 1 2; do
  "$TI_CGT/bin/cl6x" -mv6740 --abi=eabi -O1 --include_path="$TI_CGT/include" --visibility=hidden \
      --diag_wrap=off -DPOC=$n --obj_directory="$W" --output_file="$W/poc$n.obj" "$OSS_PORT/src/poc.c" >/dev/null 2>&1 || \
  "$TI_CGT/bin/cl6x" -mv6740 --abi=eabi -O1 --include_path="$TI_CGT/include" --visibility=hidden \
      --diag_wrap=off -DPOC=$n -c --obj_directory="$W" "$OSS_PORT/src/poc.c"
  mv "$W/poc.obj" "$W/poc$n.obj" 2>/dev/null || true
  "$PYTHON" "$SCRIPT_DIR/patch_audio_fn.py" "$W/stock.code" "$W/poc$n.obj" "$W/poc$n.code"
  pack "$W/poc$n.code" XXPOC$n $(printf "0x060010%x0" $((8+n)))
done
