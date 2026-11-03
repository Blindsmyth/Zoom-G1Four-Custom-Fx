#!/usr/bin/env bash
set -euo pipefail

PREFIX="${1:-$HOME/ti/c6000_8312}"
URL="https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-vqU2jj6ibH/8.3.12/ti_cgt_c6000_8.3.12_osx_installer.app.zip"
TMP="$(mktemp -d)"

echo "Downloading TI C6000 CGT 8.3.12..."
curl -fL -o "$TMP/installer.zip" "$URL"
unzip -q "$TMP/installer.zip" -d "$TMP"

ARCH="$(uname -m)"
case "$ARCH" in
    arm64) INST="$TMP/ti_cgt_c6000_8.3.12_osx_installer.app/Contents/MacOS/osx-arm64" ;;
    x86_64) INST="$TMP/ti_cgt_c6000_8.3.12_osx_installer.app/Contents/MacOS/osx-x86_64" ;;
    *) echo "Unsupported arch: $ARCH" >&2; exit 1 ;;
esac

mkdir -p "$PREFIX"
"$INST" --mode unattended --unattendedmodeui none --prefix "$PREFIX"
echo "Installed to $PREFIX/ti-cgt-c6000_8.3.12"
rm -rf "$TMP"
