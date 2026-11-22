#!/usr/bin/env python3
"""Builds work/g13jnr/{maincpu.bin (BIOS ROM region, 4 MB), game.bin (decrypted game image at 0x80010000)} from roms/g13jnr.zip (or a folder).
The game image sits in chip 0 blocks 1023..1015 then 10..43 (block order found by following the decrypter stream against a MAME RAM dump), decrypted
with the type-2 stream (iv 0) started at its first page; its length is 1354 pages (0xa9400 bytes)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
import nand

BLOCKS = [1023, 1022, 1021, 1020, 1019, 1018, 1017, 1016, 1015] + list(range(10, 44))
IMAGE_PAGES = 1354

def main(src='roms/g13jnr.zip', out='work/g13jnr'):
    n = nand.Nand(nand.load(src)); os.makedirs(out, exist_ok=True)
    rom = bytearray([0x55]) * 0x400000                       # BIOS ROM region: NAND pages 0x40.. descrambled (pages 0x40-0x11f at 0, 0x120.. at 0x20000)
    def copy(page0, count, dst):
        for p in range(page0, page0 + count): rom[dst + (p - page0) * 0x200: dst + (p - page0 + 1) * 0x200] = n.plain_page(0, p)
    copy(0x40, 0xe0, 0); copy(0x120, 0x1f00, 0x20000)
    open(os.path.join(out, 'maincpu.bin'), 'wb').write(bytes(rom))
    dec = nand.g13jnr_decrypter(); dec.init(0); img = bytearray()
    for blk in BLOCKS:
        for p in range(32):
            if len(img) >= IMAGE_PAGES * 512: break
            img += b''.join(struct.pack('<H', dec.decrypt(w)) for w in n.raw_words(0, blk * 32 + p))
    open(os.path.join(out, 'game.bin'), 'wb').write(bytes(img[:IMAGE_PAGES * 512]))
    print('maincpu.bin %d bytes, game.bin %d bytes, entry word %08x' % (len(rom), IMAGE_PAGES * 512, struct.unpack_from('<I', img, 0)[0]))

if __name__ == '__main__': main(*sys.argv[1:])
