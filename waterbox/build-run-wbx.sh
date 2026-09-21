#!/bin/sh
# Builds run-wbx, the harness that drives pcem.wbx through the miniBox host
# directly - no engine, no package. The gate's digest-stream, drive-type and
# save-data legs run through it.
#
# It was built by hand until the disk work needed it rebuilt; a harness that
# only one person's shell history knows how to build is a harness that goes
# stale, which is the failure this repo has already had once with the core.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}"
out="${WBX_DIR:-$root/build/wbx}"
mkdir -p "$out"

lib=""
for c in "$mb/build/source/host/libminiboxhost.a" "$mb/build/meson-linux/source/host/libminiboxhost.a"; do
	[ -f "$c" ] && { lib="$c"; break; }
done
[ -n "$lib" ] || { echo "no libminiboxhost.a under $mb/build" >&2; exit 1; }

gcc -O2 -g -Wall -I"$mb/source/host" -o "$out/run-wbx" "$here/run-wbx.c" \
	"$lib" -lpthread -lm
echo "OK: $out/run-wbx"
