#!/usr/bin/env bash
# Launch WiiKart in the Dolphin Wii/GameCube emulator.
#
# Usage: tools/run-dolphin.sh [path/to/wiikart.dol]
set -euo pipefail

DOL="${1:-"$(dirname "$0")/../wiikart.dol"}"

if [ ! -f "$DOL" ]; then
    echo "error: $DOL not found." >&2
    echo "Build it first (see README.md) or download the CI artifact." >&2
    exit 1
fi

for bin in dolphin-emu dolphin-emu-nogui Dolphin; do
    if command -v "$bin" >/dev/null 2>&1; then
        exec "$bin" -b -e "$DOL"
    fi
done

echo "error: Dolphin not found on PATH." >&2
echo "Install it from https://dolphin-emu.org/download/ then re-run," >&2
echo "or open $DOL from Dolphin's GUI (File > Open)." >&2
exit 1
