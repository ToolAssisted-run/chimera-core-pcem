#!/usr/bin/env python3
"""chimera-run's --screenshot writes a 32-bit BGRA TGA, bottom-up. This turns
one into a PNG so a person (or a model) can look at it."""
import struct, sys, zlib

def convert(src, dst):
    d = open(src, 'rb').read()
    w, h = struct.unpack_from('<HH', d, 12)
    desc = d[17]
    px = d[18:]
    order = range(h) if desc & 0x20 else range(h - 1, -1, -1)   # bit 5: top-left
    rows = []
    for y in order:
        o = y * w * 4
        rows.append(b'\x00' + b''.join(
            bytes((px[o + x * 4 + 2], px[o + x * 4 + 1], px[o + x * 4])) for x in range(w)))
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    open(dst, 'wb').write(
        b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(b''.join(rows), 6))
        + chunk(b'IEND', b''))
    return w, h

for src in sys.argv[1:]:
    dst = src.rsplit('.', 1)[0] + '.png'
    print(dst, "%dx%d" % convert(src, dst))
