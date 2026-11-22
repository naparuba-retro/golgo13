#!/usr/bin/env python3
"""Convert a P6 PPM to PNG using only the standard library. Usage: ppm2png.py in.ppm out.png"""
import sys, zlib, struct
def read_ppm(path):
    d = open(path, 'rb').read(); parts = d.split(None, 4)
    assert parts[0] == b'P6'; w, h = int(parts[1]), int(parts[2]); off = len(d) - w * h * 3
    return w, h, d[off:]
def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, data): c = struct.pack('>I', len(data)) + t + data; return c + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
if __name__ == '__main__':
    w, h, rgb = read_ppm(sys.argv[1]); write_png(sys.argv[2], w, h, rgb); print(f"{sys.argv[1]} -> {sys.argv[2]} ({w}x{h})")
