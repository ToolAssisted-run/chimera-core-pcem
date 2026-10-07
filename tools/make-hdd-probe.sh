#!/bin/sh
# Builds the two boot-sector probes and a blank hard disk for the disk gate.
#
#   <out>/writer.img   360 KB floppy, boot sector writes MAGIC to LBA 1
#   <out>/reader.img   360 KB floppy, boot sector reads LBA 1 and, on a match,
#                      copies it to LBA 2
#   <out>/marker.img   360 KB floppy, boot sector fills the screen with '#'
#   <out>/clock.img    360 KB floppy, boot sector times the CPU's clock against
#                      the BIOS timer and writes the count to LBA 1
#   <out>/blank.img    a blank hard disk, 63/16/20 = 10,321,920 bytes
#
# usage: make-hdd-probe.sh <outdir>
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?usage: make-hdd-probe.sh <outdir>}"
mkdir -p "$out"

build() {
	mode="$1"; name="$2"
	cpp -DMODE="$mode" -x assembler-with-cpp "$here/hdd-probe.S" > "$out/.probe$mode.s"
	as --32 -o "$out/.probe$mode.o" "$out/.probe$mode.s"
	ld -m elf_i386 -Ttext 0x7c00 --oformat binary -o "$out/.probe$mode.bin" "$out/.probe$mode.o"
	[ "$(stat -c %s "$out/.probe$mode.bin")" = 512 ] || {
		echo "probe $mode is $(stat -c %s "$out/.probe$mode.bin") bytes, not 512" >&2; exit 1; }
	# a 5.25" 360k floppy: the boot sector, then nothing
	python3 -c "
import sys
boot = open(sys.argv[1],'rb').read()
open(sys.argv[2],'wb').write(boot + b'\0' * (368640 - len(boot)))
" "$out/.probe$mode.bin" "$out/$name"
	rm -f "$out/.probe$mode.s" "$out/.probe$mode.o" "$out/.probe$mode.bin"
}

build 1 writer.img
build 2 reader.img
build 3 marker.img
build 4 clock.img

# 63 sectors x 16 heads x 20 cylinders x 512, so the geometry the driver
# derives from the length is exactly the geometry it was made with
python3 -c "
import sys
open(sys.argv[1],'wb').write(b'\0' * (63*16*20*512))
" "$out/blank.img"

echo "probes in $out"
