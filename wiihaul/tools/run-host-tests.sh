#!/usr/bin/env bash
# Build and run every host test binary — the game logic, no devkitPPC
# needed. Mirrors WiiKart's single-test-file workflow, just split by
# module (truck/yard/camera/config/game) instead of one big file.
#
# Usage: tools/run-host-tests.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CC="${CC:-gcc}"
CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -Isource"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

run() {
    local name="$1"; shift
    echo "== $name =="
    "$CC" $CFLAGS "$@" -lm -o "$OUT/$name"
    "$OUT/$name"
    echo
}

run wiihaul-test-truck   tests/test_truck.c source/truck.c
run wiihaul-test-yard    tests/test_yard.c source/truck.c source/yard.c
run wiihaul-test-camera  tests/test_camera.c source/truck.c source/camera.c
run wiihaul-test-config  tests/test_config.c source/truck.c source/config.c
run wiihaul-test-game    tests/test_game.c source/truck.c source/yard.c \
                         source/camera.c source/game.c

echo "all host test suites passed"
