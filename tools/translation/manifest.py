#!/usr/bin/env python3
"""Lists every translatable image (a layout of a tile pack) found in the user's ROM and writes translations/manifest.json.
The manifest holds metadata only (ids, sizes, palette, how many tiles can be rewritten): no pixel data.
usage: manifest.py [--rom roms/golgo13] [--out translations/manifest.json]

An asset id is "<pack bank offset, 8 hex digits>/<layout index, 2 digits>", e.g. "030a9100/02"."""
import sys, os, json, argparse, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g13pack as G
import numpy as np

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--rom', default='roms/golgo13'); ap.add_argument('--out', default='translations/manifest.json')
    a = ap.parse_args()
    d = G.load_bank(a.rom); assets = []
    for info in G.find_packs(d):
        use = G.tile_use_counts(info)
        for lay in info['layouts']:
            tiles = [e['tile'] for e in lay['entries'] if e['tile'] is not None]
            writable = [e for e in lay['entries'] if e['tile'] is not None and use[e['tile']] == 1]
            area = sum(e['w'] * e['h'] for e in lay['entries']); warea = sum(e['w'] * e['h'] for e in writable)
            crc = 0
            for e in lay['entries']:
                if e['tile'] is not None: crc = zlib.crc32(G.tile_pixels(d, info, e['tile']).tobytes(), crc)
            assets.append({'id': '%08x/%02d' % (info['base'], lay['index']), 'kind': 'tile_layout', 'canvas': [lay['W'], lay['H']],
                           'palette': lay['pal'], 'tiles': len(tiles), 'writable_fraction': round(warea / area, 3) if area else 0.0,
                           'content_crc32': '%08x' % crc})
    json.dump({'format': 1, 'assets': assets}, open(a.out, 'w'), indent=0)
    print('%d assets -> %s' % (len(assets), a.out))

if __name__ == '__main__':
    main()
