#!/usr/bin/env bash
# Install the HTSN Configurator desktop launcher onto this user's Desktop.
set -e
SRC="$(cd "$(dirname "$0")" && pwd)/HTSN Configurator.desktop"
DST="$HOME/Desktop/HTSN Configurator.desktop"
cp "$SRC" "$DST"
chmod +x "$DST"
if command -v gio >/dev/null 2>&1; then
    gio set "$DST" metadata::trusted true 2>/dev/null || true
fi
echo "Installed launcher: $DST"
echo "Double-click the 'HTSN Configurator' icon to start."
