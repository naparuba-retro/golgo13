#!/usr/bin/env python3
"""Builds the game-card pictures of the launcher menu from YOUR OWN manga scans (nothing is redistributed: the output is git-ignored).
Each card is a manga panel, contrast-boosted and tinted with a two-colour ramp, written as data/ui/cards/<game id>.rgba ("G13I" w h + RGBA).
usage: make_art.py [scans_folder] [out_dir]
  scans_folder defaults to "Y:/Scan/Golgo 13/Golgo 13 - Le choix de l'auteur" (pages 001.jpg ...). Pages / crop boxes are in CARDS below
  (box = x0,y0,x1,y1 on a 400-pixel-wide thumbnail of the page): edit them to pick other panels."""
import sys, os, struct
from PIL import Image, ImageOps

SCANS = sys.argv[1] if len(sys.argv) > 1 else "Y:/Scan/Golgo 13/Golgo 13 - Le choix de l'auteur"
OUT = sys.argv[2] if len(sys.argv) > 2 else 'data/ui/cards'
SIZE = (360, 300)
CARDS = {
    'golgo13': dict(page=696, box=(0, 0, 290, 215), tint=((40, 0, 8), (255, 214, 190))),
    'g13knd': dict(page=776, box=(35, 128, 300, 245), tint=((0, 24, 28), (200, 255, 240))),
    'g13jnr': dict(page=156, box=(30, 290, 370, 530), tint=((6, 10, 40), (255, 226, 160))),
}

def card(page, box, tint, contrast=1.25):
    im = Image.open(os.path.join(SCANS, '%03d.jpg' % page)).convert('L'); k = im.width / 400.0
    im = im.crop(tuple(int(v * k) for v in box)); im = ImageOps.fit(im, SIZE, Image.LANCZOS, centering=(0.5, 0.4))
    im = ImageOps.autocontrast(im, cutoff=1).point(lambda v: max(0, min(255, int((v - 128) * contrast + 128))))
    return ImageOps.colorize(im, black=tint[0], white=tint[1]).convert('RGBA')

NOTICE = dict(page=776, box=(35, 128, 300, 245), tint=((10, 10, 14), (255, 240, 220)), size=(520, 200))   # the "Golgo aims" panel of the NOTICE countdown

def main():
    os.makedirs(OUT, exist_ok=True)
    for gid, c in CARDS.items():
        try: im = card(c['page'], c['box'], c['tint'])
        except OSError as e: print('skip', gid, e); continue
        open(os.path.join(OUT, gid + '.rgba'), 'wb').write(struct.pack('<4sII', b'G13I', *im.size) + im.tobytes())
        print(gid, '->', os.path.join(OUT, gid + '.rgba'))
    try:
        global SIZE; SIZE = NOTICE['size']; im = card(NOTICE['page'], NOTICE['box'], NOTICE['tint'])
        open(os.path.join(OUT, 'notice.rgba'), 'wb').write(struct.pack('<4sII', b'G13I', *im.size) + im.tobytes()); print('notice ->', os.path.join(OUT, 'notice.rgba'))
    except OSError as e: print('skip notice', e)

if __name__ == '__main__': main()
