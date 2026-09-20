#!/bin/bash
# M1a: headless native build of TASEmulators/pcem for the speed measurement.
# Compiles the emulation core exactly as upstream ships it; replaces only the
# wx/OpenAL platform layer with a measurement driver. The source list is
# derived from upstream's own Makefile.am so it cannot drift.
set -u
TOOLS=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$TOOLS/.." && pwd)
SRC=${PCEM_SRC:-$ROOT/extern/pcem/src}
OUT=${M1A_DIR:-$ROOT/build/m1a}
OBJ=$OUT/obj
mkdir -p "$OBJ"

CFLAGS="-O3 -fcommon -msse2 -I$SRC -I$OUT -w -DPACKAGE_STRING=\"PCem-v17+st-2\" -D_GNU_SOURCE -DRELEASE_BUILD"
CXXFLAGS="-O3 -fcommon -I$SRC -I$OUT -w -DPACKAGE_STRING=\"PCem-v17+st-2\" -D_GNU_SOURCE -DRELEASE_BUILD"

cd "$SRC"
SOURCES=$(python3 - <<'PYEOF'
import re, os
txt = open('Makefile.am').read().replace('\\\n', ' ')
srcs = []
for line in txt.splitlines():
    m = re.match(r'\s*pcem_SOURCES\s*\+?=\s*(.*)', line)
    if m:
        srcs += m.group(1).split()
# The wx GUI, OpenAL, ALSA MIDI, networking and host CD-ROM are the platform
# layer a Chimera driver replaces. wx-thread.c is pure pthread and is kept.
drop_prefix = ('wx-', 'slirp/')
drop_exact = {'soundopenal.c', 'midi_alsa.c', 'ne2000.c', 'nethandler.c',
              'cdrom-ioctl.c', 'cdrom-ioctl-linux.c', 'cdrom-ioctl-osx.c'}
out = []
for s in srcs:
    if s in drop_exact or s.startswith(drop_prefix):
        continue
    if s.startswith('codegen_backend_') and 'x86-64' not in s:
        continue
    if os.path.exists(s):
        out.append(s)
out += ['wx-thread.c', 'cdrom-ioctl-dummy.c',
        'codegen_backend_x86-64.c', 'codegen_backend_x86-64_ops.c',
        'codegen_backend_x86-64_ops_sse.c', 'codegen_backend_x86-64_uops.c']
print(' '.join(sorted(set(out))))
PYEOF
)
SOURCES="$SOURCES $TOOLS/driver.c"

FAIL=0
for s in $SOURCES; do
  tag=$(printf '%s' "$s" | md5sum | cut -c1-6)
  o="$OBJ/$(basename "$s" | sed 's/\.[^.]*$//')__$tag.o"
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ]; then
    case "$s" in
      *.cc|*.cpp) g++ $CXXFLAGS -c "$s" -o "$o" || { echo "FAILED: $s"; FAIL=1; } ;;
      *)          gcc $CFLAGS  -c "$s" -o "$o" || { echo "FAILED: $s"; FAIL=1; } ;;
    esac
  fi
done
[ $FAIL -ne 0 ] && { echo "COMPILE FAILED"; exit 1; }

g++ -O3 -o "$OUT/pcem-bench" "$OBJ"/*.o -lpthread -lm || { echo "LINK FAILED"; exit 1; }
echo "OK: $OUT/pcem-bench"
