#!/usr/bin/env python3
"""Contact sheets of the layouts of work/g13jnr/bank.bin that are not translated yet (ids printed on each picture), to find Japanese texts.
usage: sheets.py out_dir [min_w max_h]"""
import sys, os, json, glob
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'translation'))
import g13pack as G
from PIL import Image, ImageDraw
def main():
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    minw = int(sys.argv[2]) if len(sys.argv) > 2 else 60; maxh = int(sys.argv[3]) if len(sys.argv) > 3 else 140; minh = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    d = G.load_bank('work/g13jnr/bank.bin'); packs = G.find_packs(d)
    done = set()
    for f in glob.glob('translations/g13jnr/fr/strings*.json'): done |= set(json.load(open(f, encoding='utf-8'))['assets'])
    items = []
    for info in packs:
        for lay in info['layouts']:
            aid = '%08x/%02d' % (info['base'], lay['index'])
            if aid in done: continue
            if lay['W'] < minw or lay['H'] > maxh or lay['H'] < minh: continue
            items.append((aid, G.render_layout(d, info, lay)))
    print(len(items), 'layouts')
    CW, CH, cols, per = (480, 120, 3, 30) if minh == 0 else (240, 135, 6, 48)
    for si in range(0, len(items), per):
        part = items[si:si + per]; rows = (len(part) + cols - 1) // cols
        sh = Image.new('RGB', (cols * CW, rows * CH), (70, 0, 70)); dr = ImageDraw.Draw(sh)
        for i, (aid, rgba) in enumerate(part):
            im = Image.fromarray(rgba, 'RGBA'); im.thumbnail((CW - 4, CH - 16)); x, y = (i % cols) * CW, (i // cols) * CH
            sh.paste(im, (x + 2, y + 14), im); dr.text((x + 2, y + 1), aid, fill=(255, 255, 0))
        sh.save(os.path.join(out, 'sheet_%03d.png' % (si // per)))
if __name__ == '__main__': main()
