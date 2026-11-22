#!/usr/bin/env python3
"""Extracts the tiled 4bpp image packs from the banked graphics ROM to PNG files (reads the user's own ROM files; never commit the output).
usage: extract_images.py [rom_dir] [out_dir]
The pack format and the parsing live in tools/translation/g13pack.py."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'translation'))
import g13pack as G
from g13pack import load_bank, write_png, pal_rgba   # re-exported: tools/harvest_ram.py and others import these from here

def parse_pack(d, base, limit):
    """returns list of RGBA images or None if the block at base is not a valid pack (kept for harvest_ram.py)"""
    info = G.pack_info(d, base, limit)
    return None if info is None else [G.render_layout(d, info, lay) for lay in info['layouts']]

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    rom = args[0] if len(args) > 0 else 'roms/golgo13'
    out = args[1] if len(args) > 1 else 'extract/images'
    os.makedirs(out, exist_ok=True)
    d = load_bank(rom); n = 0; packs = G.find_packs(d)
    for info in packs:
        for lay in info['layouts']:
            im = G.render_layout(d, info, lay)
            if im[:, :, 3].any(): write_png(os.path.join(out, 'pack_%08x_%02d.png' % (info['base'], lay['index'])), im); n += 1
    print('%d packs, %d images -> %s' % (len(packs), n, out))
