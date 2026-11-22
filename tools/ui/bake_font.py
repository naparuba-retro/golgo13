#!/usr/bin/env python3
"""Bakes the two menu fonts (title / body) from fonts installed on this machine into data/ui/font_<face>.bin (read by runtime/ui/ui.c).
The baked files are derived from system fonts: they stay on the user's machine (git-ignored), nothing is redistributed.
usage: bake_font.py [out_dir]     (default data/ui)"""
import sys, os, struct
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else 'data/ui'
FACES = {
    'title': (['impact.ttf', 'Anton-Regular.ttf', 'arialbd.ttf', 'DejaVuSans-Bold.ttf', 'LiberationSans-Bold.ttf'], [28, 36, 46, 58, 72, 120, 190]),
    'body': (['comicbd.ttf', 'arialbd.ttf', 'DejaVuSans-Bold.ttf', 'LiberationSans-Bold.ttf'], [16, 18, 20, 22, 24, 28]),
}
DIRS = ['C:/Windows/Fonts/', '/usr/share/fonts/truetype/dejavu/', '/usr/share/fonts/truetype/liberation/', '/usr/share/fonts/TTF/', '/Library/Fonts/', '']
CODES = list(range(32, 127)) + list(range(160, 256)) + [0x2026, 0x2019, 0x2013, 0x2014, 0x20ac, 0x201c, 0x201d]

def find(names):
    for n in names:
        for d in DIRS:
            if os.path.exists(d + n): return d + n
    return None

def bake(path, sizes):
    blob = struct.pack('<4sI', b'G13F', len(sizes))
    for px in sizes:
        f = ImageFont.truetype(path, px); asc, desc = f.getmetrics(); glyphs = []; bmp = bytearray()
        for c in CODES:
            ch = chr(c); adv = int(round(f.getlength(ch)))
            l, t, r, b = f.getbbox(ch, anchor='ls')
            w, h = max(0, r - l), max(0, b - t)
            if w and h:
                im = Image.new('L', (w + 2, h + 2), 0); ImageDraw.Draw(im).text((-l + 1, -t + 1), ch, font=f, fill=255, anchor='ls')
                data = im.tobytes(); w2, h2 = im.size
            else: data = b''; w2 = h2 = 0
            glyphs.append((c, adv, l - 1, t - 1, w2, h2, len(bmp))); bmp += data
        blob += struct.pack('<IiiII', px, asc, desc, len(glyphs), len(bmp))
        for g in glyphs: blob += struct.pack('<IhhhHHI', *g)
        blob += bytes(bmp)
    return blob

def main():
    os.makedirs(OUT, exist_ok=True); ok = 0
    for face, (names, sizes) in FACES.items():
        p = find(names)
        if not p: print('no font found for', face, names); continue
        data = bake(p, sizes); open(os.path.join(OUT, 'font_%s.bin' % face), 'wb').write(data); ok += 1
        print('%s <- %s (%d bytes)' % (face, os.path.basename(p), len(data)))
    return 0 if ok == len(FACES) else 1

if __name__ == '__main__': sys.exit(main())
