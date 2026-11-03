#!/usr/bin/env bash
# Download the stock G1 FOUR Chorus effect from Zoom's own CDN into ./stock (git-ignored).
# The effects are Zoom's property: fetch them yourself, do not commit them.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STOCK_DIR="${STOCK_DIR:-$SCRIPT_DIR/../stock}"; mkdir -p "$STOCK_DIR"
cd "$STOCK_DIR"
curl -fL -o CHORUS.zip "https://www.zoom.co.jp/archive/GUITAR_LAB/ZDLF6/CHORUS.zip"
unzip -o CHORUS.zip CHORUS.ZD2
rm -f CHORUS.zip
echo "stock/CHORUS.ZD2: $(shasum -a 256 CHORUS.ZD2 | cut -d' ' -f1)  (expected 1d78ab6c1a6e909ded03a9401d4217fc468cc2f4f6b26093199688b324126c93)"
