#!/usr/bin/env python3
"""Generate C for every image (game, bios, kern) from the ROM + flow json (<flow dir>/<name>.json).
Game selected by env GOLGO_GAME (see tools/gamecfg.py): golgo13 -> generated/, others -> generated_<id>/ with prefixed symbols."""
import sys, os, json
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import gamecfg
from recomp import Gen
from images import get_images
ROOT = gamecfg.ROOT; PFX = gamecfg.PFX
rom = open(os.path.join(gamecfg.REGIONS, 'maincpu.bin'), 'rb').read()
out = gamecfg.OUT_MIPS
os.makedirs(out, exist_ok=True)
for f in os.listdir(out):
    if f.endswith(('.c', '.h', '.json')): os.remove(os.path.join(out, f))
names = []; total = {}
for name, d in get_images(rom).items():
    fl = json.load(open(os.path.join(gamecfg.FLOW, f'{name}.json')))
    code = fl['code']; lead = None
    if d.get('all_entries'):
        t0, t1 = d['text']; lead = code
        code = sorted(set(code) | set(range(t0, t1, 4)))
    g = Gen(d['img'], d['base'], code, fl['funcs'], fl['jump_tables'], PFX + name, lead_code=lead, all_entries=bool(d.get('all_entries')))
    rep = g.run(out)
    json.dump(rep, open(os.path.join(out, f'{PFX}{name}_report.json'), 'w'), indent=1)
    names.append(PFX + name); total[name] = {k: v for k, v in rep.items() if k not in ('files', 'warn_examples')}
NL = chr(10)
with open(os.path.join(out, PFX + 'lookup.c'), 'w', newline=NL) as f:
    f.write('/* GENERATED */' + NL + '#include "rt.h"' + NL)
    for n in names: f.write(f'blk_fn {n}_lookup(uint32_t pc);' + NL)
    f.write(NL + 'blk_fn ' + PFX + 'code_lookup(uint32_t pc)' + NL + '{' + NL + '    blk_fn f;' + NL)
    for n in names: f.write(f'    if ((f = {n}_lookup(pc))) return f;' + NL)
    f.write('    return 0;' + NL + '}' + NL)
    for n in names: f.write(f'void {n}_export(blk_fn *dst, uint32_t dst_base, uint32_t dst_words);' + NL)
    f.write(NL + '/* flat dispatch tables for the runtime: RAM = physical 0..ram_words*4, ROM = 0x1fc00000.. */' + NL)
    f.write('void ' + PFX + 'code_export_all(blk_fn *ram_tbl, uint32_t ram_words, blk_fn *rom_tbl, uint32_t rom_words)' + NL + '{' + NL)
    for n in names:
        f.write(f'    {n}_export(ram_tbl, 0u, ram_words);' + NL + f'    {n}_export(rom_tbl, 0x1fc00000u, rom_words);' + NL)
    f.write('}' + NL)
print(json.dumps(total, indent=1))
