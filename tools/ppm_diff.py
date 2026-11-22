#!/usr/bin/env python3
"""Compare two P6 PPM images: share of differing pixels, max channel difference, PSNR. Usage: ppm_diff.py a.ppm b.ppm"""
import sys, math
sys.path.insert(0, __file__.rsplit('/', 1)[0] if '/' in __file__ else '.')
def read(p):
    d = open(p, 'rb').read(); parts = d.split(None, 4); w, h = int(parts[1]), int(parts[2]); return w, h, d[len(d) - w * h * 3:]
w1, h1, a = read(sys.argv[1]); w2, h2, b = read(sys.argv[2])
assert (w1, h1) == (w2, h2), "size differs"
n = w1 * h1; diff = 0; mx = 0; se = 0
for i in range(n):
    x = a[i * 3:i * 3 + 3]; y = b[i * 3:i * 3 + 3]
    if x != y:
        diff += 1
        for k in range(3): d = abs(x[k] - y[k]); mx = max(mx, d); se += d * d
psnr = 99.0 if se == 0 else 10 * math.log10(255 * 255 / (se / (n * 3)))
print(f"{sys.argv[1]} vs {sys.argv[2]}: {diff}/{n} pixels differ ({100 * diff / n:.3f} %), max channel diff {mx}, PSNR {psnr:.1f} dB")
