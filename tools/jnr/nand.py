"""Golgo 13 Juusei no Requiem (Namco System 10 MEM(N)) NAND access: 6 chips of 1024 blocks x 32 pages x 528 bytes (512 data + 16 spare).
Descrambling and the type-2 stream decrypter follow MAME namcos10.cpp / ns10crypt.cpp (BSD-3, reimplemented here)."""
import struct, os, sys, zipfile

PAGE = 0x210; PAGES_PER_BLOCK = 32; BLOCKS = 1024
NAND_FILES = ['glt1vera_0.2j', 'glt1vera_1.1j', 'glt1vera_2.2h', 'glt1vera_3.1h', 'glt1vera_4.2f', 'glt1vera_5.1f']
UNSCRAMBLE = [0xe, 0xd, 0xc, 0xf, 0x9, 0xb, 0x8, 0xa, 0x6, 0x7, 0x4, 0x5, 0x1, 0x3, 0x0, 0x2]   # bitswap<16> source bits, MSB first
DATA_XOR = 0xaaaa

def _bitswap(v, order):
    r = 0
    for s in order: r = (r << 1) | ((v >> s) & 1)
    return r
_UNS = [_bitswap(v ^ DATA_XOR, UNSCRAMBLE) for v in range(65536)]

def load(src):
    """src: folder with the 6 files, or the zip. Returns list of 6 bytes objects."""
    if os.path.isdir(src): return [open(os.path.join(src, n), 'rb').read() for n in NAND_FILES]
    z = zipfile.ZipFile(src); return [z.read(n) for n in NAND_FILES]

class Nand:
    def __init__(self, chips): self.chips = chips
    def raw_page(self, dev, page): return self.chips[dev][page * PAGE: page * PAGE + PAGE]
    def raw_words(self, dev, page):                       # data bytes as the controller returns them: big-endian words
        d = self.chips[dev]; a = page * PAGE
        return [(d[a + i] << 8) | d[a + i + 1] for i in range(0, 0x200, 2)]
    def plain_page(self, dev, page):                      # descrambled (non-encrypted data), little-endian words as in memory
        out = bytearray()
        for w in self.raw_words(dev, page): out += struct.pack('<H', _UNS[w])
        return bytes(out)
    def remap(self):
        """Bad block table at the top of chip 0 block 0 (plain): (dev<<12 | block, new block) pairs, big endian, until ffff ffff."""
        d = self.chips[0]; t = {}; i = 0
        while i + 4 <= 0x4000:
            a, b = struct.unpack_from('>HH', d, i)
            if a == 0xffff and b == 0xffff: break
            t[(a >> 12, a & 0xfff)] = b; i += 4
        return t

# ---- type-2 stream decrypter (gamshara / g13jnr family) ----
M64 = (1 << 64) - 1
def gf2(n): return bin(n).count('1') & 1

class Type2:
    def __init__(self, eMask, dMask, xMask, nonlinear, iv_func=None):
        self.e = eMask; self.d = dMask; self.x = xMask; self.nl = nonlinear; self.iv_func = iv_func
        self.pc = 0; self.pp = 0; self.mask = 0
    SBOX = [0, 12, 13, 6, 2, 4, 9, 8, 11, 1, 7, 15, 10, 5, 14, 3]
    def init(self, iv):
        if self.iv_func: self.pc = self.iv_func(iv) & M64
        else:
            s = self.SBOX[iv]
            self.pc = (((s >> 3) & 1) << 15) | (((s >> 2) & 1) << 12) | (((s >> 1) & 1) << 11) | ((s & 1) << 8)
        self.pp = 0; self.mask = 0
    def decrypt(self, c):
        p = (c ^ self.mask) & 0xffff
        self.pc = ((self.pc << 16) ^ c) & M64; self.pp = ((self.pp << 16) ^ p) & M64
        m = 0
        for j in range(15, -1, -1):
            m <<= 1; m ^= gf2(self.e[j] & self.pc); m ^= gf2(self.d[j] & self.pp)
        m ^= self.x; m ^= self.nl(self.pc, self.pp)
        self.mask = m & 0xffff
        return p

def g13jnr_decrypter():
    e = [0x00005600001c0582, 0x0000000000004024, 0x0000010212403000, 0x0000000000008404, 0x0000005121060021, 0x0000010212402001, 0x0000000001024000, 0x0000000000000840,
         0x00000981c2100148, 0x0000000020108400, 0x0000000008110134, 0x0000000000000003, 0x00004c9801080102, 0x0000000040860083, 0x0000000000000001, 0x0000000000000288]
    d = [0x0000441800340584, 0x0000000000004028, 0x0000010212800000, 0x0000000000000404, 0x0000485022000041, 0x0000010212804001, 0x0000000001028000, 0x0000000000000841,
         0x00000e0104080150, 0x0000000023208400, 0x0000000040110104, 0x0000000000000003, 0x0000470001100102, 0x00000000408c0083, 0x0000000000000002, 0x0000000000000308]
    def nl(pc, pp):
        masks = pc ^ pp
        return ((1 & (masks >> 6) & (masks >> 10)) << 14)
    return Type2(e, d, 0x9546, nl)
