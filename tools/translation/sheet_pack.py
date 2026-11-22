#!/usr/bin/env python3
"""Renders every layout of a pack as a labelled sheet (index, size, picture trimmed to its content) to read/translate its texts.
usage: sheet_pack.py <pack hex, e.g. 031b5520> [--out build/sheet.png] [--rom roms/golgo13] [--scale 1] [--max 40]"""
import sys, os, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, g13pack as G
from PIL import Image, ImageDraw

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('pack'); ap.add_argument('--out', default='build/sheet.png'); ap.add_argument('--rom', default='roms/golgo13')
    ap.add_argument('--scale', type=int, default=1); ap.add_argument('--max', type=int, default=60); a = ap.parse_args()
    d = G.load_bank(a.rom); base = int(a.pack, 16); info = [p for p in G.find_packs(d) if p['base'] == base][0]
    rows = []
    for lay in info['layouts'][:a.max]:
        im = G.render_layout(d, info, lay); al = im[:, :, 3] > 0
        if not al.any(): continue
        ys, xs = np.where(al); im = im[max(ys.min() - 2, 0):ys.max() + 3, max(xs.min() - 2, 0):xs.max() + 3]
        rows.append((lay['index'], lay['W'], lay['H'], Image.fromarray(im, 'RGBA')))
    Wd = max(r[3].width for r in rows) * a.scale + 70; Hd = sum(r[3].height * a.scale + 6 for r in rows)
    sh = Image.new('RGBA', (Wd, Hd), (55, 55, 85, 255)); dr = ImageDraw.Draw(sh); y = 0
    for idx, W, H, im in rows:
        im = im.resize((im.width * a.scale, im.height * a.scale), Image.NEAREST); dr.text((2, y + 2), '%02d' % idx, fill=(255, 255, 0)); dr.text((2, y + 14), '%dx%d' % (W, H), fill=(160, 200, 255))
        bg = Image.new('RGBA', im.size, (0, 0, 0, 255)); bg.alpha_composite(im); sh.paste(bg, (66, y)); y += im.height + 6
    os.makedirs(os.path.dirname(a.out) or '.', exist_ok=True); sh.save(a.out); print(a.out, sh.size)

if __name__ == '__main__':
    main()
