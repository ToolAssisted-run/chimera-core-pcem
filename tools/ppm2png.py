#!/usr/bin/env python3
"""Minimal PPM(P6) -> PNG, no third-party modules."""
import sys, zlib, struct

def read_ppm(path):
    d = open(path, 'rb').read()
    # header: P6 <ws> w <ws> h <ws> maxval <single ws> data
    fields, i = [], 2
    while len(fields) < 3:
        while i < len(d) and d[i:i+1].isspace(): i += 1
        if d[i:i+1] == b'#':
            while d[i:i+1] != b'\n': i += 1
            continue
        j = i
        while j < len(d) and not d[j:j+1].isspace(): j += 1
        fields.append(int(d[i:j])); i = j
    i += 1
    w, h, _ = fields
    return w, h, d[i:i + w*h*3]

def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 6))
           + chunk(b'IEND', b''))
    open(path, 'wb').write(png)

for src in sys.argv[1:]:
    w, h, rgb = read_ppm(src)
    dst = src.rsplit('.', 1)[0] + '.png'
    write_png(dst, w, h, rgb)
    print(f"{dst} {w}x{h}")
