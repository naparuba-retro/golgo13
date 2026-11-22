"""Definition of the code images recompiled from the main CPU ROM region (4 MB, little endian)."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import gamecfg
# end of the game's code (everything after is data): found by looking at where code density drops
GAME_TEXT_END = 0x803a0000 if gamecfg.PRIMARY else 0x803b0000
KERNEL_RAM_SIZE = 0x9500
# (ram_offset, size, rom_offset): where the BIOS copies kernel pieces from ROM into low RAM (found by comparing
# a MAME RAM dump with the ROM; the big block is the kernel proper, small ones are vectors and call tables)
KERNEL_SEGMENTS = [
    (0x0500, 0x8f00, 0x6000),   # kernel code+data  RAM 0x500.. <= ROM 0x6000..
    (0x0080, 0x0010, 0x6cf4),   # exception vector stub
    (0x00a0, 0x0030, 0x6010),   # A0/B0/C0 call stubs
    (0x0200, 0x0020, 0x6768),   # tables
    (0x0230, 0x00b0, 0x61cc),
    (0x0310, 0x0160, 0x62ac),
    (0x0490, 0x0040, 0x642c),
]

def build_kernel_image(rom):
    img = bytearray(KERNEL_RAM_SIZE)
    for ram, size, src in KERNEL_SEGMENTS:
        img[ram:ram + size] = rom[src:src + size]
    return bytes(img)

def game_entry(rom):
    """The first word of the game image is `j entry`."""
    w = int.from_bytes(rom[0x28000:0x28004], 'little')
    assert w >> 26 == 2, 'game image does not start with a jump'
    return ((w & 0x3ffffff) << 2) | 0x80000000

def _entry_of(img):
    w = int.from_bytes(img[0:4], 'little')
    assert w >> 26 == 2, 'game image does not start with a jump'
    return ((w & 0x3ffffff) << 2) | 0x80000000

def get_images(rom):
    if gamecfg.GAME == 'g13jnr':     # System 10: BIOS = NAND pages in the ROM region, game image decrypted by tools/jnr/prepare.py
        game = open(os.path.join(gamecfg.REGIONS, 'game.bin'), 'rb').read()
        return {
            'game': dict(img=game, base=0x80010000, text=(0x80010000, 0x80010000 + len(game)), roots=[0x80010000, _entry_of(game)], all_entries=True),
            'bios': dict(img=rom[0x0:0x24000], base=0xbfc00000, text=(0xbfc00000, 0xbfc24000), roots=[0xbfc00000], all_entries=True),
            'kern': dict(img=build_kernel_image(rom), base=0x80000000, text=(0x80000080, 0x80009400), roots=[0x80000080, 0x800000a0, 0x800000b0, 0x800000c0], all_entries=True),
        }
    return {
        'game': dict(img=rom[0x28000:0x28000 + 0xf0000], base=0x80300000, text=(0x80300000, GAME_TEXT_END), roots=[0x80300000, game_entry(rom)], all_entries=True),
        'bios': dict(img=rom[0x0:0x28000], base=0xbfc00000, text=(0xbfc00000, 0xbfc28000), roots=[0xbfc00000], all_entries=True),
        'kern': dict(img=build_kernel_image(rom), base=0x80000000, text=(0x80000080, 0x80009400),
                     roots=[0x80000080, 0x800000a0, 0x800000b0, 0x800000c0], all_entries=True),
    }
