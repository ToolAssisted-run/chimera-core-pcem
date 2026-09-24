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

finish() {
	printf '\n%d passed, %d failed, %d skipped\n' "$pass" "$fail" "$skip"
	# A gate that skipped everything has proven nothing, and must not read as a
	# pass (gates.md).
	[ "$pass" -eq 0 ] && { echo "NOTHING RAN"; exit 1; }
	[ "$fail" -eq 0 ] || exit 1
	exit 0
}

# A SKIP is visible here and in the summary, and a gate that skipped
# everything is NOT a pass - see finish().
[ -x "$run" ]  || { report SKIP "everything" "no chimera-run at $run"; }

# ---------------------------------------------------------------- 1. build
if sh "$here/build-package.sh" -r "$chimera_root" -m "$mb" > "$work/package.log" 2>&1; then
	report PASS "package builds"
else
	report FAIL "package builds" "see build/gate/package.log"
fi

if MINIBOX_DIR="$mb" sh "$here/build-run-wbx.sh" > "$work/run-wbx.log" 2>&1; then
	report PASS "the harness builds"
else
	report FAIL "the harness builds" "see build/gate/run-wbx.log"
fi

# The core.wbx inside the package must be NEWER than the driver it is built
# from. This is the leg that would have caught the stale-binary package.
if [ -f "$root/build/wbx/pcem.wbx" ] \
   && [ "$root/build/wbx/pcem.wbx" -nt "$here/pcem-driver.c" ]; then
	report PASS "core.wbx is not stale"
else
	report FAIL "core.wbx is not stale" "older than pcem-driver.c"
fi

# ------------------------------------------- 1b. the declaration is legal
# A slot id of "floppyA" shipped and nothing caught it - not the package
# build, not this gate, not loading the core. Only a user creating a project
# ever saw it, and the complaint named THEIR file. Never again.
if python3 "$root/tools/check-declaration.py" "$here/waterbox.config" \
   "$here/file_slots.json" > "$work/decl.log" 2>&1; then
	report PASS "the declaration is legal" "$(tail -1 "$work/decl.log")"
else
	report FAIL "the declaration is legal" "$(grep -m1 BAD "$work/decl.log")"
fi

# ------------------------------------------------- 1c. the presets are legal
# A preset is a bundle of setting values the wizard writes INTO the settings
# and is then finished with, and a key the core misdeclared is IGNORED and only
# named on a status line nobody reads. So it is checked here instead.
if python3 "$root/tools/check-presets.py" "$here/waterbox.config" \
   > "$work/presets.log" 2>&1; then
	report PASS "the presets are legal" "$(tail -1 "$work/presets.log")"
else
	report FAIL "the presets are legal" "$(grep -m1 BAD "$work/presets.log")"
fi

# NEGATIVE CONTROL, run every time rather than described. Three breaks, one per
# rule that has ever been got wrong here:
#
#   * a preset that names a setting this core does not have - the frontend
#     drops it and only whispers on a status line;
#   * a preset that puts a string where the core declared an int;
#   * a preset that does not name its BOARD. That is the most important thing a
#     preset says, and the first version of this work left it out of all five
#     on a misreading of ApplySelectedPreset - so this is the rule most in need
#     of a control, not least.
python3 - "$here/waterbox.config" "$work/presets-broken.config" <<'PRESETBREAK'
import json, sys
cfg = json.loads(open(sys.argv[1]).read())
cfg["presets"][0]["values"]["thereIsNoSuchSetting"] = 1
cfg["presets"][1]["values"]["memSizeKB"] = "eight megabytes"
cfg["presets"][2]["values"].pop("machine", None)
open(sys.argv[2], "w").write(json.dumps(cfg))
PRESETBREAK
if python3 "$root/tools/check-presets.py" "$work/presets-broken.config" \
   > "$work/presets-neg.log" 2>&1; then
	report FAIL "a misdeclared preset is caught" "it passed - the leg is blind"
else
	report PASS "a misdeclared preset is caught" \
		"negative control, $(grep -c BAD "$work/presets-neg.log") caught"
fi

# Every leg from here on runs a machine, and every PC machine needs its BIOS,
# which is not ours to ship: without the ROM set (a public CI runner) the gate
# stops at what it can prove - the package builds, fresh, from a declaration
# that is legal - and says what it did not run.
if [ ! -d "$roms" ]; then
	report SKIP "every leg that runs a machine" "no PCem ROM set at $roms (PCEM_ROMS)"
	finish
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
# eight axes now: two relative mouse, two absolute mouse POSITION, four
# joystick. A position of zero is the left edge, and since it never changes
# and the first frame deliberately places nothing, this movie moves no mouse.
line = "|" + "    0," * 8 + "." * 110 + "|\n"
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

# ------------------------------------------- 3b. the mouse, and its units
# PCem has NO absolute pointing device - its whole mouse list is two serial,
# two PS/2 and two machine-integrated mice, every one relative - so an absolute
# position is turned into the movement that would reach it. Two things about
# that can be wrong while every other leg here stays green: the difference
# could be taken in wire units instead of pixels (a hundredfold too much
# movement, and the machine still runs), and a jump bigger than one mouse
# report could be truncated (the pointer stops short, and nothing says so).
# Both are arithmetic, so they are checked as arithmetic.
if python3 "$root/tools/check-axes.py" "$here/waterbox.config" \
   "$here/pcem-driver.h" > "$work/axes.log" 2>&1; then
	report PASS "the axis wire matches the driver" "$(tail -1 "$work/axes.log")"
else
	report FAIL "the axis wire matches the driver" "$(cat "$work/axes.log")"
fi

if cc -O1 -Wall -Wextra -o "$work/test-input" "$here/tests/test-input.c" \
   "$here/pcem-input.c" > "$work/test-input.log" 2>&1 \
   && "$work/test-input" >> "$work/test-input.log" 2>&1; then
	report PASS "the mouse arithmetic" "$(tail -1 "$work/test-input.log")"
else
	report FAIL "the mouse arithmetic" "$(tail -3 "$work/test-input.log")"
fi

# AND IT REACHES THE MACHINE. The arithmetic above is a pure function; this is
# the wire. A frame digest cannot see it - a mouse packet sitting in a
# controller's buffer with no guest driver to read it changes the machine and
# draws nothing - so the whole state is hashed instead.
statehash() {
	"$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$gw" 400 \
		--state-hash "$@" 2>/dev/null | sed -n 's/^STATEHASH \([0-9a-f]*\).*/\1/p'
}
mqu="$(statehash)"
mpos="$(statehash --axis 200:2=65535)"
mpos2="$(statehash --axis 200:2=65535)"
mrel="$(statehash --axis 200:0=100)"
if [ -z "$mqu" ] || [ -z "$mpos" ]; then
	report FAIL "the position axis reaches the machine" "a run produced no state hash"
elif [ "$mpos" = "$mqu" ]; then
	report FAIL "the position axis reaches the machine" "moving it changed nothing"
elif [ "$mpos" != "$mpos2" ]; then
	report FAIL "the position axis reaches the machine" "not deterministic: $mpos vs $mpos2"
elif [ "$mpos" = "$mrel" ]; then
	report FAIL "the position axis reaches the machine" "indistinguishable from the relative axis"
else
	report PASS "the position axis reaches the machine" "and is deterministic"
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

# ------------------------------------------- 4b. firmware resolves by hash
# Chimera's Scan Folder is hash-first: FirmwareLocator matches on SHA1 when the
# declaration has one and only falls back to the name. A declaration without
# hashes resolves NOTHING against a folder full of correct ROMs - which is what
# Sergio hit. The requirement in his words is that a single "include
# sub-folders" search matches all firmwares, so that is what this checks.
scanroot="${PCEM_ROM_COLLECTION:-/mnt/c/Users/sergiom/Documents/TAS/firmware/PCem-ROMs}"
if [ -d "$scanroot" ]; then
	if python3 "$root/tools/check-firmware-scan.py" "$here/waterbox.config" \
	   "$scanroot" > "$work/scan.log" 2>&1; then
		report PASS "firmware resolves by hash" \
			"$(sed -n 's/^resolved by hash: //p' "$work/scan.log")"
	else
		report FAIL "firmware resolves by hash" "see build/gate/scan.log"
	fi
else
	report SKIP "firmware resolves by hash" "no ROM collection at $scanroot"
fi

# --------------------------------------- 4c. Auto fits the drive to the disk
# PCem does not refuse a disk the drive cannot reach - it clamps the head at
# the drive's last track and the guest gets read errors - so Auto getting this
# wrong is a confusing failure rather than a loud one.
if python3 "$root/tools/check-auto.py" "$root/build/wbx/run-wbx" \
   "$root/build/wbx/pcem.wbx" "$gw" > "$work/auto.log" 2>&1; then
	report PASS "Auto fits the drive to the disk" "$(tail -1 "$work/auto.log")"
else
	report FAIL "Auto fits the drive to the disk" "$(grep -m1 BAD "$work/auto.log")"
fi

# NEGATIVE CONTROL: a drive named explicitly must OVERRIDE Auto, or the
# setting is decorative and a person cannot pick a drive at all.
python3 - "$gw/settings" <<'PY'
import json, sys
p = sys.argv[1]; d = json.load(open(p)); d["driveAType"] = '5.25" 360k'
json.dump(d, open(p, "w"))
PY
printf '{"floppy_a":["big.img"]}' > "$gw/slots"
python3 -c "open('$gw/big.img','wb').write(b'\0'*2949120)"
forced=$("$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$gw" 2 --drive-types 2>/dev/null \
	| sed -n 's/^DRIVES a=\([0-9]*\).*/\1/p')
rm -f "$gw/slots" "$gw/big.img"
python3 - "$gw/settings" <<'PY'
import json, sys
p = sys.argv[1]; d = json.load(open(p)); d["driveAType"] = "Auto"
json.dump(d, open(p, "w"))
PY
if [ "${forced:-}" = "1" ]; then
	report PASS "a named drive overrides Auto" "negative control"
else
	report FAIL "a named drive overrides Auto" "got type ${forced:-none}, wanted 1"
fi

# ------------------ 4c-bis. every preset reaches the machine, and boots
# The leg 1c above reads the DECLARATION. This one resolves each preset the way
# the wizard does and runs it, and asks two different questions:
#
#   * did the values REACH the machine? A per-device value the driver drops
#     leaves PCem's own default in place, so the machine boots, the picture
#     looks right and every digest matches - because the default is what this
#     core did before these settings existed. A boot leg alone therefore cannot
#     fail for the thing most likely to break (gates.md B), so what is read is
#     GetComposedConfig: the .cfg PCem was actually handed.
#   * does it reach a BIOS SCREEN? Liveness is not a stable digest - a machine
#     stuck at frame 1 is perfectly deterministic - so the picture has to have
#     CHANGED across the run as well as be drawn at the end.
#
# Proven to bite: with compose_device_sections() commented out, this reported
# 18 problems across all five presets, naming every device key that had gone
# missing. 9000 frames is not arbitrary either - at 2000 the Compaq Deskpro 386
# is still counting memory and has 361 lit pixels.
if python3 "$root/tools/check-preset-boots.py" "$here/waterbox.config" \
   "$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$roms" \
   "$work/presets" 9000 > "$work/preset-boots.log" 2>&1; then
	report PASS "every preset reaches the machine and boots" \
		"$(tail -1 "$work/preset-boots.log")"
else
	report FAIL "every preset reaches the machine and boots" \
		"$(grep -m1 BAD "$work/preset-boots.log")"
fi

# ------------------------------------------- 4d. a real game, on a real 1981 PC
# Every other leg runs a Pentium II. This one boots Alley Cat (1984) on an IBM
# PC 5150 with CGA off a 180 KB single-sided disk, which is the other end of
# the 93 machines and the end DOSBox-X cannot do.
#
# The liveness check here is deliberately NOT a stable digest: a stuck machine
# is perfectly deterministic, and this project has been caught by that three
# times. It requires the picture to CHANGE across the run.
game="${PCEM_ALLEYCAT:-/mnt/c/Users/sergiom/Documents/TAS/roms/dos/alleyCat/disk1.img}"
pcrom="${PCEM_ROM_COLLECTION:-/mnt/c/Users/sergiom/Documents/TAS/firmware/PCem-ROMs}/ibmpc/pc102782.bin"
if [ -f "$game" ] && [ -f "$pcrom" ]; then
	ac="$work/alleycat"; mkdir -p "$ac"
	cp "$pcrom" "$ac/ibmpc_pc102782.bin"
	cp "${PCEM_ROM_COLLECTION:-/mnt/c/Users/sergiom/Documents/TAS/firmware/PCem-ROMs}/mda.rom" "$ac/mda.rom"
	cp "$game" "$ac/disk1.img"
	python3 - "$ac" <<'PY'
import json, sys
d = sys.argv[1]
json.dump({"system": "x86 PC", "machine": "ibmpc - [8088] IBM PC",
           "cpu": "8088/4.77", "memSizeKB": 640, "videoCard": "cga - CGA",
           "soundCard": "none - None", "hddController": "none - None",
           "driveAType": "Auto", "cdDrive": "None",
           "fpsNumerator": 100, "fpsDenominator": 1}, open(d + "/settings", "w"))
json.dump({"floppy_a": ["disk1.img"]}, open(d + "/slots", "w"))
PY
	# Auto must fit a 5.25" 360k drive (type 1) to a 180 KB disk - a drive
	# nobody would have guessed, which is what makes this a real test of it.
	dt=$("$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$ac" 2 --drive-types 2>/dev/null \
		| sed -n 's/^DRIVES a=\([0-9]*\).*/\1/p')
	if [ "${dt:-}" = "1" ]; then
		report PASS "Auto fits a 180 KB disk" "5.25\" 360k"
	else
		report FAIL "Auto fits a 180 KB disk" "fitted type ${dt:-none}, wanted 1"
	fi

	# Boot, answer the joystick prompt and the skill menu, and require the
	# picture to keep changing once the game is up.
	distinct=$("$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$ac" 60000 \
		--press 12500=48 --press 19500=36 --press 42000=56 \
		--digests --every 250 2>/dev/null \
		| awk 'NR>1{print $3}' | tail -40 | sort -u | wc -l)
	if [ "${distinct:-0}" -ge 20 ]; then
		report PASS "Alley Cat boots and animates" "$distinct distinct frames of the last 40"
	else
		report FAIL "Alley Cat boots and animates" "only ${distinct:-0} distinct frames - stuck?"
	fi
else
	report SKIP "Alley Cat boots and animates" "no game image at $game"
fi

# --------------------------- 4e. hard disks: the bytes have to SURVIVE
# The claim is not "a write did not crash". It is that a guest write is held,
# leaves through the save-data channel, goes back in as the seed, and is read
# by the machine on the other side. So the proof is a ROUND TRIP, checked on
# the bytes and not on the screen:
#
#   run 1  a boot sector writes a magic string to LBA 1 of a blank disk,
#          and the export must contain it;
#   run 2  a different boot sector, seeded with run 1's EXPORT, reads LBA 1
#          and copies it to LBA 2 only if it matches - so the magic at LBA 2
#          of run 2's export cannot exist unless the machine read the seed.
#
# The differential is run 2's boot sector against a FRESH blank disk, where
# LBA 2 must stay zero. Without it, an overlay that simply smeared the magic
# over the disk would pass.
if sh "$root/tools/make-hdd-probe.sh" "$work/hdd" > "$work/hdd-probe.log" 2>&1; then
	report PASS "the disk probes assemble"
else
	report FAIL "the disk probes assemble" "see build/gate/hdd-probe.log"
fi

hdd_settings() {
	python3 - "$1" <<'HDDSET'
import json, sys
d = sys.argv[1]
json.dump({"system": "x86 PC", "machine": "ga686bx - [Slot 1] Gigabyte GA-686BX",
           "cpu": "Pentium II/450", "fpu": "builtin", "dynarec": True,
           "memSizeKB": 16384, "videoCard": "v3_3000 - 3DFX Voodoo 3 3000",
           "soundCard": "none - None", "hddController": "ide - [IDE] Standard IDE",
           "mouseType": "2-button mouse (PS/2)", "driveAType": "Auto",
           "driveBType": "None", "cdDrive": "None", "videoSpeed": "Fast VLB/PCI",
           "fpsNumerator": 100, "fpsDenominator": 1}, open(d + "/settings", "w"))
json.dump({"floppy_a": ["boot.img"], "hdd": ["disk.img"]}, open(d + "/slots", "w"))
HDDSET
}

hdd_run() {  # <workdir> <floppy> <disk image to seed from> <export dir>
	rm -rf "$1"; mkdir -p "$1" "$4"
	cp "$gw/ga686bx_6BX.F2a" "$gw/voodoo3_3000_3k12sd.rom" "$gw/awe32.raw" \
	   "$gw/mda.rom" "$1/"
	cp "$2" "$1/boot.img"
	cp "$3" "$1/disk.img"
	hdd_settings "$1"
	"$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$1" 3000 \
		--hdd-stats --savedata-out "$4"
}

magic_at() {  # <image> <lba> -> "yes" or "no"
	python3 - "$1" "$2" <<'HDDMAGIC'
import sys
off = int(sys.argv[2]) * 512
d = open(sys.argv[1], "rb").read(off + 16)
print("yes" if d[off:off + 16] == b"CHIMERA-PCEM-HD!" else "no")
HDDMAGIC
}

hdd_run "$work/hdd/w1" "$work/hdd/writer.img" "$work/hdd/blank.img" \
	"$work/hdd/out1" > "$work/hdd1.log" 2>&1 || true
if [ -f "$work/hdd/out1/disk.img" ] \
   && [ "$(magic_at "$work/hdd/out1/disk.img" 1)" = "yes" ]; then
	report PASS "a guest write leaves as save data" \
		"$(sed -n 's/^HDD 0 .*held_blocks=\([0-9]*\).*/\1 block(s) held/p' "$work/hdd1.log")"
else
	report FAIL "a guest write leaves as save data" "no magic at LBA 1; see build/gate/hdd1.log"
fi

hdd_run "$work/hdd/w2" "$work/hdd/reader.img" "$work/hdd/out1/disk.img" \
	"$work/hdd/out2" > "$work/hdd2.log" 2>&1 || true
hdd_run "$work/hdd/w3" "$work/hdd/reader.img" "$work/hdd/blank.img" \
	"$work/hdd/out3" > "$work/hdd3.log" 2>&1 || true
seeded="no"; control="yes"
[ -f "$work/hdd/out2/disk.img" ] && seeded=$(magic_at "$work/hdd/out2/disk.img" 2)
[ -f "$work/hdd/out3/disk.img" ] && control=$(magic_at "$work/hdd/out3/disk.img" 2)
if [ "$seeded" = "yes" ] && [ "$control" = "no" ]; then
	report PASS "an exported disk seeds the next run" "and a blank one does not"
else
	report FAIL "an exported disk seeds the next run" \
		"seeded=$seeded control=$control (the control must be no)"
fi

# The overlay is SPARSE: a 10 MB blank disk with one sector written must hold
# one 4 KiB block, not the disk. This is the leg that notices the day somebody
# turns the overlay back into a copy.
held=$(sed -n 's/^HDD 0 .*held_blocks=\([0-9]*\).*/\1/p' "$work/hdd1.log")
if [ -n "${held:-}" ] && [ "$held" -ge 1 ] && [ "$held" -le 8 ]; then
	report PASS "the overlay is sparse" "$held block(s) for one sector written"
else
	report FAIL "the overlay is sparse" "held ${held:-none} blocks"
fi

# --------------------------- 4f. a written disk block is MACHINE STATE
# If it were not, a rewind past a write would leave the disk ahead of the
# machine and every re-record after it would be wrong. The leg needs the disk
# to have CHANGED between the state and the end of the run, or it proves
# nothing, so that is asserted too.
st=$("$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$work/hdd/w1" 3000 \
	--state-roundtrip 300 2>/dev/null | sed -n 's/^STATE //p')
sv=$(printf '%s' "$st" | sed -n 's/.*disk_at_save=\([0-9a-f]*\).*/\1/p')
en=$(printf '%s' "$st" | sed -n 's/.*disk_at_end=\([0-9a-f]*\).*/\1/p')
ld=$(printf '%s' "$st" | sed -n 's/.*disk_after_load=\([0-9a-f]*\).*/\1/p')
r1=$(printf '%s' "$st" | sed -n 's/.*replay=\([0-9a-f]*\)\/[0-9a-f]*.*/\1/p')
r2=$(printf '%s' "$st" | sed -n 's/.*replay=[0-9a-f]*\/\([0-9a-f]*\).*/\1/p')
if [ -n "${sv:-}" ] && [ "$sv" != "$en" ] && [ "$ld" = "$sv" ] && [ "$r1" = "$r2" ]; then
	report PASS "the disk rewinds with the machine" "and the replay is identical"
else
	report FAIL "the disk rewinds with the machine" "$st"
fi

# ------------------------------- 4g. geometry comes out of the image
# PCem keeps C/H/S in its config and NOTHING in the emulation core derives it
# (hdd_load hands hdc[d] straight to ide.c as the drive's identity), so a zero
# there is a drive of no sectors and "Auto" has to mean something. The blank
# disk is 63/16/20 by construction and the composed .cfg has to say so.
cfg=$("$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$work/hdd/w1" 2 \
	--dump-cfg 2>/dev/null)
geom=$(printf '%s\n' "$cfg" | sed -n 's/^hdc_\(sectors\|heads\|cylinders\) = /\1=/p' | tr '\n' ' ')
if [ "$geom" = "sectors=63 heads=16 cylinders=20 " ]; then
	report PASS "Auto derives the disk geometry" "63/16/20"
else
	report FAIL "Auto derives the disk geometry" "got [$geom]"
fi

# NEGATIVE CONTROL: Custom must override it, or the three numbers are
# decorative and an image Auto cannot read is unusable.
python3 - "$work/hdd/w1/settings" <<'HDDCUST'
import json, sys
p = sys.argv[1]; d = json.load(open(p))
d["hddGeometry"] = "Custom"; d["hddSectors"] = 17
d["hddHeads"] = 15; d["hddCylinders"] = 40
json.dump(d, open(p, "w"))
HDDCUST
cfg=$("$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$work/hdd/w1" 2 \
	--dump-cfg 2>/dev/null)
geom=$(printf '%s\n' "$cfg" | sed -n 's/^hdc_\(sectors\|heads\|cylinders\) = /\1=/p' | tr '\n' ' ')
if [ "$geom" = "sectors=17 heads=15 cylinders=40 " ]; then
	report PASS "Custom geometry overrides Auto" "negative control"
else
	report FAIL "Custom geometry overrides Auto" "got [$geom]"
fi

# ------------------- 4i. the whole route, through the engine and a project
# Every leg above drives the core through run-wbx, which is the guest ABI and
# nothing else. A user's disk travels further than that: a project file names
# it in the hdd slot, the engine mounts it by path, the driver finds it in the
# "slots" mount, and Emulator > Export Save Data pulls it back out through
# ce_session_savedata_read. A slot is invisible to every other leg here,
# because a run without a project has no slots mount at all.
if [ -x "$run" ] && [ -f "$pkg" ]; then
	pj="$work/hdd/project"; rm -rf "$pj"; mkdir -p "$pj/files" "$pj/out"
	cp "$work/hdd/writer.img" "$pj/files/boot.img"
	cp "$work/hdd/blank.img" "$pj/files/disk.img"
	if python3 "$here/tests/make-project.py" "$pkg" "$pj/p.chimeraProject" 3000 \
		--setting "machine=ga686bx - [Slot 1] Gigabyte GA-686BX" \
		--setting "cpu=Pentium II/450" --setting "fpu=builtin" \
		--setting "memSizeKB=16384" \
		--setting "videoCard=v3_3000 - 3DFX Voodoo 3 3000" \
		--setting "soundCard=none - None" \
		--setting "hddController=ide - [IDE] Standard IDE" \
		--setting "cdDrive=None" --setting "driveBType=None" \
		--setting "videoSpeed=Fast VLB/PCI" \
		--file "floppy_a=$pj/files/boot.img" --file "hdd=$pj/files/disk.img" \
		--firmware "ga686bx_6BX.F2a=$roms/ga686bx/6BX.F2a" \
		--firmware "voodoo3_3000_3k12sd.rom=$roms/voodoo3_3000/3k12sd.rom" \
		--firmware "mda.rom=$roms/mda.rom" > "$work/project.log" 2>&1 \
	   && "$run" --project "$pj/p.chimeraProject" "$pkg" --files "$pj/files" \
		--firmware "ga686bx_6BX.F2a=$roms/ga686bx/6BX.F2a" \
		--firmware "voodoo3_3000_3k12sd.rom=$roms/voodoo3_3000/3k12sd.rom" \
		--firmware "mda.rom=$roms/mda.rom" \
		--export-savedata "$pj/out" >> "$work/project.log" 2>&1 \
	   && grep -q "^savedata=1" "$work/project.log" \
	   && [ "$(magic_at "$pj/out/disk.img" 1)" = "yes" ]; then
		report PASS "a project's disk exports through the engine" \
			"$(stat -c %s "$pj/out/disk.img") bytes"
	else
		report FAIL "a project's disk exports through the engine" \
			"see build/gate/project.log"
	fi
else
	report SKIP "a project's disk exports through the engine" "no chimera-run"
fi

# ----------------------- 4h. a big seeded disk costs nothing until written
# The whole point of the sparse overlay, and the number the state cost rests
# on: a 4121 MB Windows XP image must not put 4121 MB anywhere, and a state
# taken at the first frame must not know about it. Skipped when there is no
# such image on this machine.
xpimg="${PCEM_XP_IMAGE:-$root/build/xp/winxp-desktop.img}"
if [ -f "$xpimg" ]; then
	xw="$work/hdd/xp"; rm -rf "$xw"; mkdir -p "$xw"
	cp "$gw/ga686bx_6BX.F2a" "$gw/voodoo3_3000_3k12sd.rom" "$gw/awe32.raw" \
	   "$gw/mda.rom" "$xw/"
	ln -sf "$(cd "$(dirname "$xpimg")" && pwd)/$(basename "$xpimg")" "$xw/winxp.img"
	python3 - "$xw" <<'HDDXP'
import json, sys
d = sys.argv[1]
json.dump({"system": "x86 PC", "machine": "ga686bx - [Slot 1] Gigabyte GA-686BX",
           "cpu": "Pentium II/450", "fpu": "builtin", "dynarec": True,
           "memSizeKB": 262144, "videoCard": "v3_3000 - 3DFX Voodoo 3 3000",
           "soundCard": "sbawe32 - Sound Blaster AWE32",
           "hddController": "ide - [IDE] Standard IDE",
           "mouseType": "2-button mouse (PS/2)", "driveAType": "3.5\" 2.88M",
           "driveBType": "5.25\" 1.2M", "cdDrive": "None",
           "videoSpeed": "Fast VLB/PCI", "fpsNumerator": 100,
           "fpsDenominator": 1}, open(d + "/settings", "w"))
json.dump({"hdd": ["winxp.img"]}, open(d + "/slots", "w"))
HDDXP
	xbytes=$("$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$xw" 20 \
		--state-every 1000 2>/dev/null \
		| sed -n 's/^STATESIZE frame=0 bytes=\([0-9]*\).*/\1/p')
	xgeom=$("$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$xw" 2 --dump-cfg 2>/dev/null \
		| sed -n 's/^hdc_\(sectors\|heads\|cylinders\) = /\1=/p' | tr '\n' ' ')
	if [ -n "${xbytes:-}" ] && [ "$xbytes" -lt 33554432 ] \
	   && [ "$xgeom" = "sectors=63 heads=16 cylinders=8374 " ]; then
		report PASS "a 4 GiB seed costs nothing" \
			"state $((xbytes / 1048576)) MiB, geometry 63/16/8374"
	else
		report FAIL "a 4 GiB seed costs nothing" "state ${xbytes:-?} bytes, geometry [$xgeom]"
	fi
else
	report SKIP "a 4 GiB seed costs nothing" "no XP image at $xpimg"
fi

# ------------------- 4j. an AT-class machine POSTs without asking for F1
# An AT keeps its equipment list in CMOS, and a BIOS that finds one floppy
# drive where the CMOS says two stops at "162-System Options Not Set-(Run
# SETUP)" and waits for F1. Every AT-class machine did that until the driver
# started editing the CMOS to describe the machine the settings built.
#
# The assertion is not the text on the screen. It is that the machine got far
# enough to BOOT: the marker probe fills the screen with '#', which no POST
# stop can do. Measured: 81,479 lit pixels when it boots, 1,932 when it stops
# at 162 - so the threshold is nowhere near either.
atrom="$roms/ibmat/62x0820.u27"
if [ -f "$atrom" ]; then
	at="$work/hdd/at"; rm -rf "$at"; mkdir -p "$at"
	cp "$roms/ibmat/62x0820.u27" "$at/ibmat_62x0820.u27"
	cp "$roms/ibmat/62x0821.u47" "$at/ibmat_62x0821.u47"
	cp "$gw/mda.rom" "$at/mda.rom"
	cp "$roms/ibm_vga.bin" "$at/ibm_vga.bin"
	cp "$work/hdd/marker.img" "$at/boot.img"
	python3 - "$at" <<'ATSET'
import json, sys
d = sys.argv[1]
json.dump({"system": "x86 PC", "machine": "ibmat - [286] IBM AT",
           "cpu": "286/6", "fpu": "none", "dynarec": False, "memSizeKB": 640,
           "videoCard": "vga - VGA", "soundCard": "none - None",
           "hddController": "none - None", "cdDrive": "None",
           "driveAType": "Auto", "driveBType": "None",
           "fpsNumerator": 100, "fpsDenominator": 1}, open(d + "/settings", "w"))
json.dump({"floppy_a": ["boot.img"]}, open(d + "/slots", "w"))
ATSET
	"$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$at" 6000 \
		--shot 5999="$at/s.ppm" > "$work/at.log" 2>&1 || true
	lit=0
	[ -f "$at/s.ppm" ] && lit=$(python3 - "$at/s.ppm" <<'ATLIT'
import sys
f = open(sys.argv[1], "rb"); f.readline()
w, h = map(int, f.readline().split()); f.readline()
d = f.read()
print(sum(1 for i in range(0, len(d), 3) if d[i] | d[i + 1] | d[i + 2]))
ATLIT
)
	if [ "${lit:-0}" -gt 20000 ]; then
		report PASS "an IBM AT POSTs without asking for F1" "$lit lit pixels"
	else
		report FAIL "an IBM AT POSTs without asking for F1" \
			"only ${lit:-0} lit pixels - it is sitting at a POST stop"
	fi
else
	report SKIP "an IBM AT POSTs without asking for F1" "no ibmat ROMs at $roms/ibmat"
fi

# ------------------------------------------------------- 5. savestates
if "$root/build/wbx/run-wbx" "$root/build/wbx/pcem.wbx" "$gw" 600 > /dev/null 2>&1; then
	report PASS "600-frame run for the state legs"
else
	report FAIL "600-frame run for the state legs"
fi

finish
