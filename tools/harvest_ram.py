#!/usr/bin/env python3
"""Finds image packs (same format as tools/extract_images.py) in RAM dumps taken while the game runs (golgo13 --ramseq ...)
and extracts each distinct pack once. This also catches packs that are compressed in the ROM (the game decompresses them to RAM).
usage: harvest_ram.py out_dir dump1.bin dump2.bin ...   (dumps are processed in the order given; reads/writes only under out_dir)"""
import sys, os, struct, hashlib, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_images as E

def main():
    out = sys.argv[1]; dumps = sys.argv[2:]
    os.makedirs(out, exist_ok=True)
    seen = {}; pat = struct.pack('<II', 0x18, 0x18)
    for dp in dumps:
        d = open(dp, 'rb').read(); fr = int(re.findall(r'(\d+)\.bin$', dp)[0]) if re.findall(r'(\d+)\.bin$', dp) else 0
        pos = 0
        while True:
            pos = d.find(pat, pos)
            if pos < 0: break
            res = E.parse_pack(d, pos, len(d) - pos)
            if res is None: pos += 4; continue
            h0, h1, A, A2, B, nt, npal = struct.unpack_from('<5IHH', d, pos)
            key = hashlib.sha1(d[pos:pos + min(B + 0x400, len(d) - pos)]).hexdigest()[:10]
            if key not in seen:
                seen[key] = (fr, pos, len(res))
                for i, im in enumerate(res):
                    if im[:, :, 3].any(): E.write_png(os.path.join(out, 'ram_%s_%02d.png' % (key, i)), im)
            pos += 0x18
    for k, (fr, pos, n) in sorted(seen.items(), key=lambda kv: kv[1][0]):
        print('pack %s first seen at frame %d, RAM %06x, %d images' % (k, fr, pos, n))
    print(len(seen), 'distinct packs')

if __name__ == '__main__':
    main()
