#!/usr/bin/env python3
"""Run flow discovery on every image and write work/flow/<name>.json (+ summary)."""
import sys, os, json
sys.path.insert(0, os.path.dirname(__file__))
from flowlib import Flow
from images import get_images
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import gamecfg
ROOT = gamecfg.ROOT
rom = open(os.path.join(gamecfg.REGIONS, 'maincpu.bin'), 'rb').read()
os.makedirs(gamecfg.FLOW, exist_ok=True)
only = sys.argv[1:]
for name, d in get_images(rom).items():
    if only and name not in only: continue
    f = Flow(d['img'], d['base'], d['text'], d['roots'])
    rep = f.analyze()
    json.dump(f.export(), open(os.path.join(gamecfg.FLOW, f'{name}.json'), 'w'))
    print(name, rep)
    print("   unresolved jr:", [hex(x) for x in f.unres][:12])
