#!/usr/bin/env python3
"""make-font-rom.py <fnt00-7F.inc> <fnt80-FF.inc> <out mda.rom>

An MDA or CGA card draws text with the shapes in IBM's character ROM, which
PCem reads from mda.rom and which is not free. For the test script's free
machine (chimera issue 260) this makes a stand-in from the 8x8 font the
Xi 8088 BIOS carries for its graphics modes (from the kbd package's
alt-8x8, GPL-3.0 like the BIOS): the same 256 shapes, at the place in the
8 KB ROM where PCem reads the CGA's (0x1800). The MDA's 8x14 shapes are
left empty; the test's machine has a CGA.
"""
import re
import sys


def glyphs(path):
    out = []
    for line in open(path, encoding="ascii", errors="replace"):
        m = re.match(r"\s*db\s+(.*?)\s*(;.*)?$", line)
        if not m:
            continue
        for item in m.group(1).split(","):
            out.append(int(item.strip().rstrip("hH"), 16))
    return out


def main():
    low, high, out = sys.argv[1], sys.argv[2], sys.argv[3]
    font = glyphs(low) + glyphs(high)
    if len(font) != 256 * 8:
        sys.exit("expected 2048 bytes of glyphs, found %d" % len(font))
    rom = bytearray(8192)
    rom[0x1800:0x1800 + len(font)] = bytes(font)
    open(out, "wb").write(rom)
    print("%s: 256 glyphs at 0x1800" % out)


if __name__ == "__main__":
    main()
