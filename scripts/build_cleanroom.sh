#!/usr/bin/env bash
# Build a clean-room effect from cleanroom/<name>/ with Stomphacks' pipeline, retarget it for the G1 FOUR
# and run the pre-flight checker. No stock Zoom file enters the build.
#
#   scripts/build_cleanroom.sh gain
#
# Needs: STOMPHACKS (a checkout of https://github.com/thammer/stomphacks, default ../../references/stomphacks),
# ZT2_DIR (zoom-zt2), TI_CGT and PYTHON (with construct, crcmod, pyelftools, Pillow) as in scripts/env.sh,
# and G1CHECK (the pre-flight checker; optional, skipped with a warning when missing).
# Stomphacks' link script does not quote paths, so the build runs in a temporary directory without spaces.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/env.sh"
name="${1:?usage: build_cleanroom.sh <effect folder under cleanroom/>}"
src="$ZOOM_ROOT/cleanroom/$name"
[[ -f "$src/manifest.json" ]] || { echo "no $src/manifest.json" >&2; exit 1; }
STOMPHACKS="${STOMPHACKS:-$ZOOM_ROOT/../../references/stomphacks}"
PYTHON="${PYTHON:-python3}"
[[ -f "$STOMPHACKS/tools/zd2_make_effect.py" ]] || { echo "Stomphacks not found at $STOMPHACKS (set STOMPHACKS)" >&2; exit 1; }

work="$(mktemp -d /tmp/g1cr.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/sh" "$work/fx"
cp -R "$STOMPHACKS/tools" "$work/sh/"
ln -s "$ZT2_DIR" "$work/sh/zoom-zt2"
# Stomphacks' scripts call .venv/bin/python3; a wrapper keeps PYTHON's own environment (a symlink would not).
mkdir -p "$work/sh/.venv/bin"
printf '#!/bin/sh\nexec "%s" "$@"\n' "$(command -v "$PYTHON")" > "$work/sh/.venv/bin/python3"
chmod +x "$work/sh/.venv/bin/python3"
cp "$src"/*.json "$src"/*.c "$src"/*.h "$work/fx/" 2>/dev/null || true

(cd "$work" && ZOOM_TI_CGT="$TI_CGT" "$PYTHON" -E -s sh/tools/zd2_make_effect.py fx/manifest.json)

file="$("$PYTHON" -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["filename"])' "$src/manifest.json")"
mkdir -p "$ZOOM_ROOT/dist/cleanroom"
cp "$work/fx/build/REPORT.md" "$ZOOM_ROOT/dist/cleanroom/$file.REPORT.md"
"$PYTHON" -I "$here/g1_retarget.py" "$work/fx/build/$file.ZD2" "$ZOOM_ROOT/dist/cleanroom/$file.ZD2"

G1CHECK="${G1CHECK:-$ZOOM_ROOT/../../research/tools/g1check.py}"
if [[ -f "$G1CHECK" ]]; then "$PYTHON" -I "$G1CHECK" "$ZOOM_ROOT/dist/cleanroom/$file.ZD2"
else echo "WARNING: pre-flight checker not found (set G1CHECK); do not install unchecked" >&2; fi
(cd "$work" && shasum -a 256 "$ZOOM_ROOT/dist/cleanroom/$file.ZD2")
