#!/usr/bin/env python3
"""Finds every picture-pack file on the g13jnr NAND (descrambled image work/g13jnr/nand_plain.bin), decompresses it and writes
work/g13jnr/bank.bin (packs back to back, 0x1000 aligned) + work/g13jnr/bank.json (id = bank offset -> NAND offset, size, counts)."""
import sys, os, struct, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'translation'))
import unpack as U, g13pack as G

def main():
    n = open('work/g13jnr/nand_plain.bin', 'rb').read(); tabs = U.load_tables()
    pat = struct.pack('<II', 0x18, 0x18); pos = 0; found = []
    while True:
        pos = n.find(pat, pos)
        if pos < 0: break
        if pos % 512 == 8 and pos >= 8 and struct.unpack_from('<I', n, pos - 4)[0] == 0x18:
            try:
                out = U.unpack(n, tabs, pos - 8); info = G.pack_info(out, 0, len(out))
                if info: found.append((pos - 8, out, info))
            except Exception as e: pass
        pos += 4
    bank = bytearray(); meta = []; seen = set()
    import hashlib
    for off, out, info in found:
        h = hashlib.sha1(out).hexdigest()
        if h in seen: continue
        seen.add(h)
        o = (len(bank) + 0xfff) & ~0xfff; bank += bytes(o - len(bank)) + out
        meta.append({'id': '%08x' % o, 'nand': off, 'size': len(out), 'tiles': info['nt'], 'palettes': info['npal'], 'layouts': len(info['layouts'])})
    open('work/g13jnr/bank.bin', 'wb').write(bank); json.dump(meta, open('work/g13jnr/bank.json', 'w'), indent=1)
    print(len(found), 'packs, bank %d bytes' % len(bank))

if __name__ == '__main__': main()
