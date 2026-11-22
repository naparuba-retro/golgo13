#!/usr/bin/env python3
"""Keeps the distinct, stable frames of a PPM sequence and writes them as PNG.
usage: dedupe_frames.py in_dir out_dir [min_change=2.0] [stable=0.6]   (needs numpy)"""
import sys, os, glob, zlib, struct
import numpy as np
def read_ppm(p):
    d = open(p, 'rb').read(); parts = d.split(None, 4); w, h = int(parts[1]), int(parts[2])
    return np.frombuffer(parts[4][:w * h * 3], dtype=np.uint8).reshape(h, w, 3)
def write_png(path, a):
    h, w, _ = a.shape; raw = b''.join(b'\x00' + a[y].tobytes() for y in range(h))
    def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 6)) + ch(b'IEND', b''))
src, dst = sys.argv[1], sys.argv[2]
chg = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0
stb = float(sys.argv[4]) if len(sys.argv) > 4 else 0.6
os.makedirs(dst, exist_ok=True)
import re
files = sorted(glob.glob(os.path.join(src, '*.ppm')), key=lambda f: int(re.findall(r'(\d+)', os.path.basename(f))[-1]))
last = None; kept = 0
for i, f in enumerate(files):
    a = read_ppm(f).astype(np.int16)
    nxt = read_ppm(files[i + 1]).astype(np.int16) if i + 1 < len(files) else a
    stable = np.abs(a - nxt).mean() < stb
    if a.mean() < 3 or not stable: continue                       # black / still animating
    if last is not None and np.abs(a - last).mean() < chg: continue   # same picture as the last kept one
    write_png(os.path.join(dst, 'brief_%03d_%s.png' % (kept, re.findall(r'(\d+)', os.path.basename(f))[-1])), a.astype(np.uint8)); last = a; kept += 1
print(kept, 'frames kept of', len(files))
