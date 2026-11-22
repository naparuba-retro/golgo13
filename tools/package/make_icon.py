#!/usr/bin/env python3
"""Generates a 256x256 PNG icon (dark square with a red gun reticle). Pure stdlib. usage: make_icon.py out.png"""
import sys, zlib, struct, math
N = 256
px = bytearray()
for y in range(N):
    px.append(0)
    for x in range(N):
        dx, dy = x - N / 2 + 0.5, y - N / 2 + 0.5
        r = math.hypot(dx, dy)
        c = (14, 14, 30)
        if 6 < N / 2 - r < 0 + 14 and r < 118: c = (210, 30, 40)                      # outer ring
        if 70 < r < 80: c = (210, 30, 40)                                              # inner ring
        if (abs(dx) < 4 and 20 < abs(dy) < 120) or (abs(dy) < 4 and 20 < abs(dx) < 120): c = (240, 240, 240)   # cross hair
        if r < 6: c = (240, 200, 40)
        px.extend(c)
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', N, N, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(px), 9)) + chunk(b'IEND', b'')
open(sys.argv[1], 'wb').write(png)
