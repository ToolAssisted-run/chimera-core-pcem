#!/bin/sh
# build.sh <source checkout> <out dir>: the test script's free PC (chimera
# issue 260).
#
# Every machine PCem has needs a BIOS, and the ones in its ROM set are not
# ours to put on a public CI runner. Sergey Kiselev's 8088 BIOS is free
# (GPL-3.0), and PCem's Xi 8088 machine is built for it. This builds it from
# the commit 8088_bios.ref names, with nasm and cmake, in a copy of the
# checkout, and makes the two files a work folder needs:
#
#   xi8088_bios-xi8088.bin   the BIOS, laid out as PCem reads it (the
#                            "noide" image: 128 KB, the BIOS at 32 KB)
#   mda.rom                  a stand-in for IBM's character ROM, made from
#                            the BIOS's own 8x8 font (make-font-rom.py), so
#                            the CGA shows the BIOS's screen
#
# For the tests only: neither file is in the package.
set -eu
src="$1"
out="$2"
here="$(cd "$(dirname "$0")" && pwd)"
want="$(sed -n 's/^commit //p' "$here/8088_bios.ref")"
have="$(git -C "$src" rev-parse HEAD 2>/dev/null || echo "not a git checkout")"
if [ "$have" != "$want" ]; then
	echo "the source is at $have, not at $want (tests/free-bios/8088_bios.ref)"
	exit 1
fi
command -v nasm >/dev/null 2>&1 || { echo "no nasm: install nasm"; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "no cmake: install cmake"; exit 1; }
rm -rf "$out"
mkdir -p "$out/build"
cp -a "$src/." "$out/src"
(cd "$out/build" && cmake "$out/src" > cmake.log && make bios-xi8088.bin > make.log)
cp "$out/build/bios-xi8088-noide.rom" "$out/xi8088_bios-xi8088.bin"
python3 "$here/make-font-rom.py" "$out/src/src/fnt00-7F.inc" "$out/src/src/fnt80-FF.inc" "$out/mda.rom" > /dev/null
echo "xi8088_bios-xi8088.bin: $(wc -c < "$out/xi8088_bios-xi8088.bin") bytes, sha1 $(sha1sum "$out/xi8088_bios-xi8088.bin" | cut -c1-40), built with $(nasm -v)"
