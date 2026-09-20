#!/bin/bash
# M1b: build the SAME emulation core for the miniBox guest (musl, -mcmodel=large).
# M1a: headless native build of TASEmulators/pcem for the speed measurement.
# Compiles the emulation core exactly as upstream ships it; replaces only the
# wx/OpenAL platform layer with a measurement driver. The source list is
# derived from upstream's own Makefile.am so it cannot drift.
set -u
TOOLS=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$TOOLS/.." && pwd)
SRC=${PCEM_SRC:-$ROOT/extern/pcem/src}
OUT=${M1B_DIR:-$ROOT/build/m1b}
OBJ=$OUT/obj
mkdir -p "$OBJ"

MB=${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}
SR=$MB/build/meson-cpp/guest-sysroot
[ -f "$SR/lib/musl-gcc.specs" ] || { echo "no miniBox guest sysroot at $SR" >&2; exit 1; }
GUESTFLAGS="-specs $SR/lib/musl-gcc.specs -fvisibility=hidden -mcmodel=large \
 -mstack-protector-guard=global -fno-stack-protector -fno-pic -fno-pie \
 -fcf-protection=none -DCHIMERA_GUEST -DCHIMERA_JIT_ARENA_INVISIBLE -I$MB/extern/emulibc \
 -I$MB/source/guest/include -I$MB/extern/jsmn"
GCCVER=$(gcc -dumpfullversion)
GUESTCXXFLAGS="$GUESTFLAGS -fexceptions -I$SR/include/c++/$GCCVER \
 -I$SR/include/c++/$GCCVER/x86_64-linux-musl"

CFLAGS="$GUESTFLAGS -O2 -fcommon -msse2 -w -DPACKAGE_STRING=\"PCem-v17+st-2\" -D_GNU_SOURCE -DRELEASE_BUILD -I$SRC -I$OUT"
CXXFLAGS="$GUESTCXXFLAGS -O2 -fcommon -w -DPACKAGE_STRING=\"PCem-v17+st-2\" -D_GNU_SOURCE -DRELEASE_BUILD -I$SRC -I$OUT"

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
SOURCES="$SOURCES $TOOLS/guest-spike.c"

FAIL=0
for s in $SOURCES; do
  tag=$(printf '%s' "$s" | md5sum | cut -c1-6)
  o="$OBJ/$(basename "$s" | sed 's/\.[^.]*$//')__$tag.o"
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ]; then
    extra=""
    case "$s" in rom.c|nvr.c) extra="-Dfopen=spike_fopen" ;; esac
    case "$s" in
      *.cc|*.cpp) g++ $CXXFLAGS -c "$s" -o "$o" 2>&1 | head -4; [ ${PIPESTATUS[0]} -ne 0 ] && { echo "FAILED: $s"; FAIL=1; } ;;
      *)          gcc $CFLAGS $extra -c "$s" -o "$o" 2>&1 | head -4; [ ${PIPESTATUS[0]} -ne 0 ] && { echo "FAILED: $s"; FAIL=1; } ;;
    esac
  fi
done
[ $FAIL -ne 0 ] && { echo "COMPILE FAILED"; exit 1; }

# The C++ guest link recipe, verbatim from source/guest/meson.build:55-82.
# Order is load-bearing: -lstdc++ -lgcc -lgcc_eh -lc with libc LAST; --no-relax
# because the large code model otherwise fails to convert GOTPCREL; the -u
# pthread_* because libgcc_eh references them weakly and weak refs do not pull
# archive members, leaving them at address 0 that a reloc from the fixed base
# cannot reach.
MBB=$MB/build/meson-cpp
g++ $GUESTCXXFLAGS -o "$OUT/pcem-spike.wbx" "$OBJ"/*.o \
   -static -no-pie -Wl,--eh-frame-hdr,-O2,--no-relax \
   -T "$MB/source/guest/linkscript.T" \
   -Wl,-u,pthread_once -Wl,-u,pthread_cond_wait \
   -Wl,-u,pthread_cond_broadcast -Wl,-u,pthread_key_create \
   "$MBB/source/guest/cxxglue.c.o" "$MBB/source/guest/emulibc.c.o" \
   -L"$SR/lib" -lstdc++ -lgcc -lgcc_eh -lc || { echo "LINK FAILED"; exit 1; }
echo "OK: $OUT/pcem-spike.wbx"
sh "$MB/source/guest/check-wbx.sh" "$OUT/pcem-spike.wbx" || echo "WARNING: check-wbx.sh reported problems"
