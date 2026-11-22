#!/usr/bin/env python3
"""Looks inside a scene capture (translations/captures/<scene>.cap): lists the groups of sprites (bit depth, palette row) with their screen areas, or renders
the sprites of a chosen area to a PNG so you can see what they draw.
usage: view_capture.py <scene> [--rect x0,y0,x1,y1] [--tp 0,1] [--clut-y 753,754] [--out file.png] [--scale 2] [--translations translations]"""
import sys, os, argparse, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image, ImageDraw
import build as B

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('scene'); ap.add_argument('--rect'); ap.add_argument('--tp'); ap.add_argument('--clut-y')
    ap.add_argument('--out', default='build/view.png'); ap.add_argument('--scale', type=int, default=2); ap.add_argument('--translations', default='translations'); a = ap.parse_args()
    qs = B.read_capture(os.path.join(a.translations, 'captures', a.scene + '.cap'))
    if a.rect:
        x0, y0, x1, y1 = map(int, a.rect.split(','))
        qs = [q for q in qs if q['x'] < x1 and q['x'] + q['w'] > x0 and q['y'] < y1 and q['y'] + q['h'] > y0]
    if a.tp: tps = set(map(int, a.tp.split(','))); qs = [q for q in qs if q['tp'] in tps]
    if a.clut_y: cl = set(map(int, a.clut_y.split(','))); qs = [q for q in qs if q['clut'][1] in cl]
    g = collections.defaultdict(list)
    for q in qs: g[(q['tp'], q['clut'][1])].append(q)
    for (tp, cy), L in sorted(g.items()):
        print('tp=%d clut_y=%d : %3d quads, area %s' % (tp, cy, len(L), (min(q['x'] for q in L), min(q['y'] for q in L), max(q['x'] + q['w'] for q in L), max(q['y'] + q['h'] for q in L))))
    if not qs: return
    ox = min(q['x'] for q in qs); oy = min(q['y'] for q in qs); W = max(q['x'] + q['w'] for q in qs) - ox; H = max(q['y'] + q['h'] for q in qs) - oy
    im = Image.new('RGBA', (W, H), (60, 60, 100, 255))
    for q in qs: im.alpha_composite(Image.fromarray(q['rgba'], 'RGBA'), (q['x'] - ox, q['y'] - oy))
    im = im.resize((W * a.scale, H * a.scale), Image.NEAREST); d = ImageDraw.Draw(im)
    d.text((2, 2), 'origin %d,%d' % (ox, oy), fill=(255, 255, 0)); os.makedirs(os.path.dirname(a.out) or '.', exist_ok=True); im.save(a.out); print('->', a.out, im.size)

if __name__ == '__main__':
    main()
