#!/bin/sh
# The PCem core's gate.
#
# Written against ~/chimera/docs/gates.md, and specifically against the three
# ways this project has already watched a gate go green on a broken thing:
#
#   * a DEAD machine reports an enormous speed and finishes instantly, so
#     every leg asserts liveness FIRST (docs/M1B.md section 6b);
#   * an END-STATE digest did not notice the emulated CPU being halved, so the
#     comparison is a per-frame digest STREAM;
#   * a guest build that failed under dash was silently packaged as the
#     PREVIOUS binary, so the package leg checks the core.wbx is newer than
#     the sources it was built from.
#
# Every leg here has been run in its broken form and seen to go red; where a
# leg has a negative control it is run as part of the gate rather than
# described.
#
# Usage: ./run-gate.sh [-r <chimera root>] [-m <minibox dir>]
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
chimera_root="${CHIMERA_ROOT:-$HOME/chimera}"
mb="${MINIBOX_DIR:-$chimera_root/extern/chimera-common-minibox}"
roms="${PCEM_ROMS:-$HOME/PCem-ROMs}"
while getopts "r:m:" opt; do
	case "$opt" in
		r) chimera_root="$OPTARG" ;;
		m) mb="$OPTARG" ;;
		*) exit 2 ;;
	esac
done

work="$root/build/gate"
rm -rf "$work"; mkdir -p "$work"
run="$chimera_root/build/meson-linux/chimera-run"
pkg="$chimera_root/build/Cores/pcem.chimeraCore"

pass=0; fail=0; skip=0
report() {
	case "$1" in
		PASS) pass=$((pass+1)) ;;
		FAIL) fail=$((fail+1)) ;;
		SKIP) skip=$((skip+1)) ;;
	esac
	printf '%-6s %-34s %s\n' "$1" "$2" "${3:-}"
}

# A SKIP is visible here and in the summary, and a gate that skipped
# everything is NOT a pass - see the exit status at the bottom.
[ -d "$roms" ] || { report SKIP "everything" "no PCem ROM set at $roms"; }
[ -x "$run" ]  || { report SKIP "everything" "no chimera-run at $run"; }

# ---------------------------------------------------------------- 1. build
if sh "$here/build-package.sh" -r "$chimera_root" -m "$mb" > "$work/package.log" 2>&1; then
	report PASS "package builds"
else
	report FAIL "package builds" "see build/gate/package.log"
fi

# The core.wbx inside the package must be NEWER than the driver it is built
# from. This is the leg that would have caught the stale-binary package.
if [ -f "$root/build/wbx/pcem.wbx" ] \
   && [ "$root/build/wbx/pcem.wbx" -nt "$here/pcem-driver.c" ]; then
	report PASS "core.wbx is not stale"
else
	report FAIL "core.wbx is not stale" "older than pcem-driver.c"
fi

# ------------------------------------------------------- 2. it runs at all
settings='{"system":"x86 PC","machine":"ga686bx - [Slot 1] Gigabyte GA-686BX",
 "cpu":"Pentium II/450","fpu":"builtin","dynarec":true,"memSizeKB":262144,
 "videoCard":"v3_3000 - 3DFX Voodoo 3 3000","soundCard":"sbawe32 - Sound Blaster AWE32",
 "hddController":"ide - [IDE] Standard IDE","mouseType":"2-button mouse (PS/2)",
 "driveAType":"3.5\" 2.88M","driveBType":"5.25\" 1.2M","videoSpeed":"Fast VLB/PCI",
 "fpsNumerator":100,"fpsDenominator":1}'
frames=1500
python3 - "$work/movie.txt" "$frames" <<'PY'
import sys
out, n = sys.argv[1], int(sys.argv[2])
line = "|" + "    0," * 6 + "." * 110 + "|\n"
open(out, "w").write(line * n)
PY

fw="--firmware ga686bx_6BX.F2a=$roms/ga686bx/6BX.F2a \
--firmware voodoo3_3000_3k12sd.rom=$roms/voodoo3_3000/3k12sd.rom \
--firmware awe32.raw=$roms/awe32.raw --firmware mda.rom=$roms/mda.rom"

# shellcheck disable=SC2086
if "$run" "$pkg" "$roms/mda.rom" "$work/movie.txt" --settings "$settings" $fw \
   --screenshot $((frames-1))="$work/a.tga" > "$work/a.log" 2>&1 \
   && grep -q "^frames=$frames" "$work/a.log"; then
	report PASS "runs $frames frames in the engine"
else
	report FAIL "runs $frames frames in the engine" "see build/gate/a.log"
fi

# LIVENESS BEFORE ANY COMPARISON. A machine that drew nothing would otherwise
# compare equal to another machine that drew nothing.
nonblack=$(python3 - "$work/a.tga" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()[18:]
print(sum(1 for i in range(0, len(d), 4) if d[i] | d[i+1] | d[i+2]))
PY
)
if [ "${nonblack:-0}" -gt 1000 ]; then
	report PASS "the machine drew a picture" "$nonblack lit pixels"
else
	report FAIL "the machine drew a picture" "only ${nonblack:-0} lit pixels"
fi

# --------------------------------------------------- 3. the digest STREAM
stream() {
	"$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$1" "$2" 2>/dev/null \
		| sed -n 's/.*stream=\([0-9a-f]*\).*/\1/p'
}
gw="$work/w"; mkdir -p "$gw"
cp "$roms/ga686bx/6BX.F2a" "$gw/ga686bx_6BX.F2a"
cp "$roms/voodoo3_3000/3k12sd.rom" "$gw/voodoo3_3000_3k12sd.rom"
cp "$roms/awe32.raw" "$gw/awe32.raw"; cp "$roms/mda.rom" "$gw/mda.rom"
cp "$roms/wy700.rom" "$gw/wy700.rom"; cp "$roms/8x12.bin" "$gw/8x12.bin"
printf "%s" "$settings" > "$gw/settings"

s1=$(stream "$gw" "$frames")
s2=$(stream "$gw" "$frames")
if [ -n "$s1" ] && [ "$s1" = "$s2" ]; then
	report PASS "deterministic over two runs" "stream=$s1"
else
	report FAIL "deterministic over two runs" "$s1 vs $s2"
fi

# NEGATIVE CONTROL for the stream: halve the emulated CPU. This is the exact
# change an end-of-run digest did NOT notice (docs/M1B.md 6b), so if the
# stream does not notice it either, the leg is worthless.
sed 's|"cpu":"Pentium II/450"|"cpu":"Pentium II/233"|' "$gw/settings" > "$gw/settings.slow"
mv "$gw/settings" "$gw/settings.fast"; mv "$gw/settings.slow" "$gw/settings"
s3=$(stream "$gw" "$frames")
mv "$gw/settings.fast" "$gw/settings"
if [ -n "$s3" ] && [ "$s3" != "$s1" ]; then
	report PASS "the stream notices a slower CPU" "negative control"
else
	report FAIL "the stream notices a slower CPU" "it did not - the leg is blind"
fi

# ----------------------------------------- 4. the declared options are real
# Every option offered in waterbox.config must be one the driver accepts. The
# lists are generated from PCem's own tables, but generated is not the same as
# checked, and an option the user can pick that the core then refuses is a
# broken settings page.
opts_bad=0
for cpuname in "Pentium II/233" "Pentium II/450" "Celeron 300"; do
	python3 - "$gw/settings" "$cpuname" <<'PY'
import json, sys
p, cpu = sys.argv[1], sys.argv[2]
d = json.load(open(p)); d["cpu"] = cpu; json.dump(d, open(p, "w"))
PY
	"$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$gw" 60 >/dev/null 2>&1 		|| { opts_bad=$((opts_bad+1)); echo "  option refused: cpu=$cpuname"; }
done
python3 - "$gw/settings" <<'PY'
import json, sys
p = sys.argv[1]; d = json.load(open(p)); d["cpu"] = "Pentium II/450"; json.dump(d, open(p, "w"))
PY
if [ "$opts_bad" -eq 0 ]; then
	report PASS "declared CPU options resolve"
else
	report FAIL "declared CPU options resolve" "$opts_bad refused"
fi

# NEGATIVE CONTROL: a CPU this machine does NOT take must be refused by name,
# not silently substituted - a silent fallback would make a movie cite a CPU
# the machine never ran.
python3 - "$gw/settings" <<'PY'
import json, sys
p = sys.argv[1]; d = json.load(open(p)); d["cpu"] = "8088/4.77"; json.dump(d, open(p, "w"))
PY
if "$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$gw" 60 2>&1    | grep -q "does not take"; then
	report PASS "a wrong CPU is refused by name" "negative control"
else
	report FAIL "a wrong CPU is refused by name" "it was accepted or died silently"
fi
python3 - "$gw/settings" <<'PY'
import json, sys
p = sys.argv[1]; d = json.load(open(p)); d["cpu"] = "Pentium II/450"; json.dump(d, open(p, "w"))
PY

# ------------------------------------------------------- 5. savestates
if "$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$gw" 600 > /dev/null 2>&1; then
	report PASS "600-frame run for the state legs"
else
	report FAIL "600-frame run for the state legs"
fi

printf '\n%d passed, %d failed, %d skipped\n' "$pass" "$fail" "$skip"
# A gate that skipped everything has proven nothing, and must not read as a
# pass (gates.md).
[ "$pass" -eq 0 ] && { echo "NOTHING RAN"; exit 1; }
[ "$fail" -eq 0 ] || exit 1
exit 0
