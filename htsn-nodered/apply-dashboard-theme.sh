#!/usr/bin/env bash
# Re-apply the HTSN card theme to the Node-RED Dashboard CSS.
# Run this after any `npm install` / dashboard update that resets app.min.css.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THEME="$HERE/dashboard-theme.css"
CSS="${DASHBOARD_CSS:-$HERE/node_modules/node-red-dashboard/dist/css/app.min.css}"

if [ ! -f "$CSS" ]; then
  # Fall back to the RPi user dir layout when run from the repo checkout.
  CSS="$HOME/htsn-nodered/node_modules/node-red-dashboard/dist/css/app.min.css"
fi
if [ ! -f "$CSS" ]; then
  echo "ERROR: dashboard app.min.css not found" >&2
  exit 1
fi

grep -q "HTSN-CARD-THEME" "$CSS" && { echo "theme already applied"; exit 0; }
cp "$CSS" "$CSS.bak"
cat "$THEME" >> "$CSS"
echo "applied HTSN card theme to $CSS"
echo "note: hard-refresh the dashboard (Ctrl+Shift+R) to load new CSS"
