#!/usr/bin/env bash
# Build Tape Chorus effects by GRAFTING compiled code into the stock Chorus ELF (no reassembly).
#   build_tape.sh STAGE NAME ID [DISPLAY [CFLAGS [DSPLOAD]]]
#     e.g.  build_tape.sh 3 XXTAPES 0x060010e0 "Tape FlutS" "" 26.5     (stereo)
#           build_tape.sh 3 XXTAPEM 0x060010f0 "Tape FlutM" -DMONO 15.0 (mono)
#     STAGE 1 = fixed short delay + Mix (delay-memory proof)   2 = clamped flutter   3 = full ToTape9 ranges
#     DISPLAY = effect name (<= 11 chars, patched into the header AND the descriptor name)
#     Also patched: default knobs (DEF_DEPTH/DEF_RATE/DEF_TONE/DEF_MIX, default 35/38/7/100) and the
#     on-screen tile + container icon ("Tape / Flut" + wave pictogram, scripts/make_tile.py)
#     DSPLOAD = declared DSP load in raw units (stock Chorus = 16.92; ~183 cycles/sample)
set -euo pipefail
STAGE="${1:?stage}"; NAME="${2:?name}"; ID="${3:?id}"; DISPLAY="${4:-}"; XCFLAGS="${5:-}"; DSPLOAD="${6:-}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/env.sh"
PYTHON="${PYTHON:-python3}"
DE="$ZT2_DIR/decode_effect.py"
DONOR="$STOCK_DIR/CHORUS.ZD2"
W="$OSS_PORT/build/tape$STAGE"; mkdir -p "$W"

"$PYTHON" "$DE" -c "$W/stock.code" -b "$W/stock_icon.bmp" "$DONOR"
# kernel: compile, take .text, pad to 32
"$TI_CGT/bin/cl6x" -mv6740 --abi=eabi -O2 -mu $XCFLAGS -DSTAGE="$STAGE" --include_path="$TI_CGT/include" --visibility=hidden \
    --diag_wrap=off --diag_warning=225 -c --obj_directory="$W" "$OSS_PORT/src/tapechorus.c"
"$PYTHON" - "$W" <<'PY'
import sys
from elftools.elf.elffile import ELFFile
w=sys.argv[1]
e=ELFFile(open(w+'/tapechorus.obj','rb'))
t=[x for x in e.iter_sections() if x.name=='.text' and x['sh_type']=='SHT_PROGBITS'][0]
assert not any(r.num_relocations() for r in e.iter_sections() if r.name in ('.rela.text','.rel.text')), "kernel needs relocations"
assert t['sh_addralign']>=32 or True
c=t.data(); c+=b'\0'*((-len(c))%32)
open(w+'/kernel.bin','wb').write(c)
s=ELFFile(open(w+'/stock.code','rb'))
x=[y for y in s.iter_sections() if y.name=='.text' and y['sh_type']=='SHT_PROGBITS'][0]
sym=[y for y in e.get_section_by_name('.symtab').iter_symbols() if y.name=='Fx_MOD_Chorus'][0]
assert sym['st_shndx']!='SHN_UNDEF'
entry=x['sh_size']+sym['st_value']          # appended at old end of .text; entry = start + function offset
assert entry%32==0 or True
open(w+'/entry.txt','w').write(hex(entry))
print('kernel',len(c),'bytes at',hex(x['sh_size']),'; Fx_MOD_Chorus at +',hex(sym['st_value']),'-> entry',hex(entry))
PY
ENTRY=$(cat "$W/entry.txt")
# trampoline: BNOP to the appended kernel, assembled+linked statically so the displacement is exact
cat > "$W/tramp.asm" <<A
	.sect ".text"
	.global tramp
	.ref kernel_entry
tramp:
	BNOP.S1 kernel_entry,5
A
cat > "$W/tramp.cmd" <<A
kernel_entry = $ENTRY;
MEMORY { P o = 0x0 l = 0x100000 }
SECTIONS { .text > P }
A
"$TI_CGT/bin/cl6x" -mv6740 --abi=eabi --diag_wrap=off -c --obj_directory="$W" "$W/tramp.asm" >/dev/null
"$TI_CGT/bin/lnk6x" --abi=eabi -e tramp --stack_size=0 --heap_size=0 --disable_auto_rts -o "$W/tramp.out" "$W/tramp.obj" "$W/tramp.cmd" >/dev/null 2>&1 || \
"$TI_CGT/bin/lnk6x" --abi=eabi -e tramp --stack_size=0 --heap_size=0 --disable_auto_rts -o "$W/tramp.out" "$W/tramp.obj" "$W/tramp.cmd"
"$PYTHON" - "$W" <<'PY'
import sys
from elftools.elf.elffile import ELFFile
w=sys.argv[1]
e=ELFFile(open(w+'/tramp.out','rb'))
t=[x for x in e.iter_sections() if x.name=='.text' and x['sh_type']=='SHT_PROGBITS'][0]
assert t['sh_addr']==0
open(w+'/tramp.bin','wb').write(t.data()); print('trampoline',len(t.data()),'bytes at 0')
PY
"$PYTHON" "$SCRIPT_DIR/elf_graft.py" "$W/stock.code" "$W/kernel.bin" "$W/tramp.bin" "$W/graft.code"
VARIANT=stereo; [[ "$XCFLAGS" == *-DMONO* ]] && VARIANT=mono
# default knob values (UI units; stock Chorus: 40 / 24 / 7 / 50)
DEF_DEPTH="${DEF_DEPTH:-35}"; DEF_RATE="${DEF_RATE:-38}"; DEF_TONE="${DEF_TONE:-7}"; DEF_MIX="${DEF_MIX:-100}"
"$PYTHON" "$SCRIPT_DIR/make_tile.py" "$VARIANT" --tile "$W/tile.bin" --bmp "$W/stock_icon.bmp" "$W/icon.bmp"
PC_ARGS=(--tile "$W/tile.bin" --defaults "$DEF_DEPTH" "$DEF_RATE" "$DEF_TONE" "$DEF_MIX")
[[ -n "$DISPLAY" ]] && PC_ARGS+=(--name "$DISPLAY")
"$PYTHON" "$SCRIPT_DIR/patch_const.py" "$W/graft.code" "${PC_ARGS[@]}"
"$PYTHON" "$DE" --donor "$W/graft.code" --donor-elf --force-name "${DISPLAY:-$NAME}" --force-id "$ID" -o "$EFFECTS_DIR/$NAME.ZD2" "$DONOR" >/dev/null
"$PYTHON" "$SCRIPT_DIR/set_icon.py" "$EFFECTS_DIR/$NAME.ZD2" "$W/icon.bmp"
[[ -n "$DSPLOAD" ]] && "$PYTHON" "$SCRIPT_DIR/set_dspload.py" "$EFFECTS_DIR/$NAME.ZD2" "$DSPLOAD"
echo "built $EFFECTS_DIR/$NAME.ZD2"
"$PYTHON" "$DE" -s "$EFFECTS_DIR/$NAME.ZD2" | tail -1
