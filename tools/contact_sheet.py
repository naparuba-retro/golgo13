#!/usr/bin/env python3
"""Contact sheet of PPM files: contact_sheet.py out.png cols thumb_w file1.ppm file2.ppm ... (labels = file stem). Needs numpy."""
import sys, zlib, struct
import numpy as np
sys.path.insert(0, __file__.rsplit('tools', 1)[0] + 'tools')
def read_ppm(p):
    d = open(p, 'rb').read(); parts = d.split(None, 4); w, h = int(parts[1]), int(parts[2])
    return np.frombuffer(parts[4][:w * h * 3], dtype=np.uint8).reshape(h, w, 3)
def write_png(path, a):
    h, w, _ = a.shape; raw = b''.join(b'\x00' + a[y].tobytes() for y in range(h))
    def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 6)) + ch(b'IEND', b''))
out, cols, tw, files = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4:]
th = tw * 3 // 4; rows = (len(files) + cols - 1) // cols
sheet = np.zeros((rows * th, cols * tw, 3), np.uint8)
for i, f in enumerate(files):
    im = read_ppm(f); ys = np.linspace(0, im.shape[0] - 1, th).astype(int); xs = np.linspace(0, im.shape[1] - 1, tw).astype(int)
    r, c = divmod(i, cols); sheet[r * th:(r + 1) * th, c * tw:(c + 1) * tw] = im[ys][:, xs]
write_png(out, sheet)
print(out, sheet.shape)
