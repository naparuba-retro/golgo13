#!/usr/bin/env python3
"""Finds every picture-pack file on the g13jnr NAND (descrambled image work/g13jnr/nand_plain.bin), decompresses it and writes
work/g13jnr/bank.bin (packs back to back, 0x1000 aligned) + work/g13jnr/bank.json (id = bank offset -> NAND offset, size, counts)."""
import sys, os, struct, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'translation'))
import unpack as U, g13pack as G

def ensure_nand_plain():
    """work/g13jnr/nand_plain.bin = the 6 NAND chips descrambled (data bytes only), built from roms/g13jnr.zip."""
    out = 'work/g13jnr/nand_plain.bin'
    if os.path.exists(out): return
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import nand
    n = nand.Nand(nand.load('roms/g13jnr.zip')); os.makedirs('work/g13jnr', exist_ok=True)
    with open(out + '.tmp', 'wb') as f:
        for dev in range(6):
            for page in range(nand.BLOCKS * nand.PAGES_PER_BLOCK): f.write(n.plain_page(dev, page))
    os.replace(out + '.tmp', out)

def ensure_scan_tables(exe):
    """The tile scan tables are built by the game at start-up: read them from a RAM dump of a short headless run."""
    if os.path.exists(U.TBL_CACHE): return
    ram = 'work/g13jnr/ram_tables.bin'
    import subprocess; subprocess.run([exe, '--game', 'g13jnr', '--headless', '--noaudio', '--frames', '1300', '--dump-ram', ram], check=True, capture_output=True)
    U.load_tables(ram); os.remove(ram)

def main():
    ensure_nand_plain(); ensure_scan_tables(sys.argv[1] if len(sys.argv) > 1 else os.path.join('build', 'golgo13.exe' if os.name == 'nt' else 'golgo13'))
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
