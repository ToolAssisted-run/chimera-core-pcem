#!/bin/bash
# run.sh <cfg> <bench-ms> <shot-every-ms> [extra args...]
set -u
TOOLS=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$TOOLS/.." && pwd)
HERE=${M1A_DIR:-$ROOT/build/m1a}
RUN=$HERE/run
CFG=${1:?cfg}; MS=${2:?bench-ms}; EVERY=${3:-0}; shift 3
rm -f "$HERE"/shots/*.ppm "$HERE"/shots/*.png
mkdir -p "$HERE/shots"
cd "$HERE"
PCEM_PATH=$RUN/pcem ./pcem-bench --config "$CFG" --bench-ms "$MS" \
    --shot-every-ms "$EVERY" --shot-dir shots "$@" 2>&1 | grep -v "^Load ROM image"
python3 "$TOOLS/ppm2png.py" "$HERE"/shots/*.ppm >/dev/null 2>&1
ls "$HERE"/shots/*.png 2>/dev/null
