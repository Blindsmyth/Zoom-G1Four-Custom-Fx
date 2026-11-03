#!/usr/bin/env bash
# Source (or let the other scripts source) this to set up paths.  Everything is overridable.
#   TI_CGT   TI C6000 code generation tools 8.3.12   (default ~/ti/c6000_8312/ti-cgt-c6000_8.3.12)
#   ZT2_DIR  a checkout of https://github.com/mungewell/zoom-zt2   (default ./zoom-zt2)
#   PYTHON   Python 3.12 with: construct==2.10.70 crcmod==1.7 pyelftools 'mido[ports-rtmidi]==1.3.3'
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export ZOOM_ROOT="$ROOT"
export OSS_PORT="$ROOT"
export ZT2_DIR="${ZT2_DIR:-$ROOT/zoom-zt2}"
export TI_CGT="${TI_CGT:-$HOME/ti/c6000_8312/ti-cgt-c6000_8.3.12}"
export STOCK_DIR="${STOCK_DIR:-$ROOT/stock}"       # stock Zoom effects you downloaded yourself (not in git)
export EFFECTS_DIR="${EFFECTS_DIR:-$ROOT/dist}"    # build output
export PATH="/opt/homebrew/opt/binutils/bin:$TI_CGT/bin:$PATH"

[[ -x "$TI_CGT/bin/cl6x" ]] || { echo "TI C6000 CGT 8.3.12 not found at $TI_CGT (set TI_CGT)" >&2; return 1 2>/dev/null || exit 1; }
[[ -f "$ZT2_DIR/decode_effect.py" ]] || { echo "zoom-zt2 not found at $ZT2_DIR (git clone https://github.com/mungewell/zoom-zt2, or set ZT2_DIR)" >&2; return 1 2>/dev/null || exit 1; }
