#!/usr/bin/env python3
"""Finds where the things drawn in a scene come from: for each textured quad of a primitive log (golgo13 --primlog F) whose 32x32 4-bit tile is
identical to a tile of a ROM image pack, prints the pack / layout and the screen position (needs the VRAM dump taken at the same frame).
usage: scene_sources.py <primlog.txt> <vram.bin> [--rom roms/golgo13]
Reads only your own ROM and dumps; prints metadata (no pixels)."""
import sys, os, re, argparse, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, g13pack as G

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('log'); ap.add_argument('vram'); ap.add_argument('--rom', default='roms/golgo13'); a = ap.parse_args()
    d = G.load_bank(a.rom); idx = {}
    for info in G.find_packs(d):
        owner = collections.defaultdict(list)
        for lay in info['layouts']:
            for e in lay['entries']:
                if e['tile'] is not None: owner[e['tile']].append((lay['index'], e['x'], e['y']))
        for t in range(info['nt']):
            b = d[info['pool_off'] + t * 512: info['pool_off'] + (t + 1) * 512]
            if len(set(b)) > 1: idx.setdefault(b, []).append((info['base'], t, owner.get(t, [])))
    v = np.fromfile(a.vram, dtype=np.uint16).reshape(1024, 1024)
    found = collections.defaultdict(list); unknown = collections.Counter()
    for l in open(a.log, errors='replace'):
        m = re.match(r'POLY#\d+ cmd=\w+ nv=(\d) tp=(\d) abr=\d tx=(\d+) ty=(\d+) clut=(\d+),(\d+) win=.*?: (.*)', l)
        if not m: continue
        tp, tx, ty, cx, cy = map(int, m.groups()[1:6])
        vs = [tuple(map(int, x)) for x in re.findall(r'\((-?\d+),(-?\d+) u(\d+) v(\d+)\)', m[7])]
        xs = [q[0] for q in vs]; ys = [q[1] for q in vs]; us = [q[2] for q in vs]; vv = [q[3] for q in vs]
        if tp != 0 or (min(us) & 3) or ty + min(vv) + 32 > 1024: unknown[(tp, cy)] += 1; continue
        reg = v[ty + min(vv): ty + min(vv) + 32, tx + min(us) // 4: tx + min(us) // 4 + 8]
        hit = idx.get(reg.astype('<u2').tobytes())
        if hit: found[(hit[0][0], tuple(sorted({o[0] for o in hit[0][2]})))].append((min(xs), min(ys), max(xs), max(ys)))
        else: unknown[(tp, cy)] += 1
    for (base, lays), q in sorted(found.items()):
        print('pack %08x layouts %s : %d quads, screen bbox %s' % (base, list(lays), len(q), (min(b[0] for b in q), min(b[1] for b in q), max(b[2] for b in q), max(b[3] for b in q))))
    print('not found in packs (tp, clut_y): %s' % dict(unknown))

if __name__ == '__main__':
    main()
