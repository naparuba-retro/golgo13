#!/usr/bin/env python3
"""Decompressor of the g13jnr (System 10) picture-pack files stored on the NAND (port of the game's routine at 0x80056e60 /
0x80056c40). The tile scan tables are built by the game at start-up; they are read from a RAM dump (tables_from_ram) and cached
in work/g13jnr/scan_tables.bin so that no RAM dump is needed afterwards."""
import struct, os
SIZES = [0x800] * 5 + [0x1000] * 5
TBL_CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'work', 'g13jnr', 'scan_tables.bin')

def tables_from_ram(ram):
    ptrs = struct.unpack_from('<10I', ram, 0xc6358)
    ends = [q for q in ptrs[1:]] + [ptrs[0] + 0x7800]
    return [ram[(p & 0x1fffffff):(e & 0x1fffffff)] for p, e in zip(ptrs, ends)]

def load_tables(ram_dump=None):
    if os.path.exists(TBL_CACHE):
        d = open(TBL_CACHE, 'rb').read(); o = 0; r = []
        for sz in SIZES: r.append(d[o:o + sz]); o += sz
        return r
    t = tables_from_ram(open(ram_dump, 'rb').read()); open(TBL_CACHE, 'wb').write(b''.join(t)); return t

class Stream:
    def __init__(s, d, p): s.d = d; s.p = p; s.flag = 0; s.nb = 0; s.mtf = [-1] * 16
    def byte(s): b = s.d[s.p]; s.p += 1; return b
    def nibble(s):
        if s.flag: s.flag = 0; return s.nb >> 4
        s.nb = s.byte(); s.flag = 1; return s.nb & 15
    def symbol(s):
        n = s.nibble()
        if n: return s.mtf[n]
        s.mtf[1:] = s.mtf[:15]; b = s.byte(); s.mtf[0] = b; return b

def tile(st, tabs, last):
    """Decodes one 32x32 4bpp tile starting at stream position st.p; returns (512 bytes, last symbol)."""
    st.flag = 0
    t = st.byte(); tab = tabs[t + 5]; tp = 0; out = bytearray(512); a1 = last
    for _ in range(16):
        flags = st.byte()
        for _ in range(8):
            if flags & 1:
                bits = st.byte()
                for _ in range(8):
                    if bits & 1: a1 = st.nibble()
                    hi, off = struct.unpack_from('<hh', tab, tp); tp += 4
                    if hi: out[off] |= (a1 << 4) & 0xff
                    else: out[off] |= a1 & 0xff
                    bits >>= 1
            else:
                for _ in range(8):
                    hi, off = struct.unpack_from('<hh', tab, tp); tp += 4
                    if hi: out[off] |= (a1 << 4) & 0xff
                    else: out[off] |= a1 & 0xff
            flags >>= 1
    return bytes(out), a1

def unpack(d, tabs, base=0):
    """d[base:] = file (word0 = decompressed size). Returns the decompressed bytes."""
    total = struct.unpack_from('<I', d, base)[0]; p = base + 4
    n0 = struct.unpack_from('<I', d, p)[0]; p += 4
    raw = d[p:p + n0]; out = bytearray(raw); p += n0
    h0, h1, A = struct.unpack_from('<3I', raw, 0)
    if h1 != h0: raise ValueError('8bpp segment not supported (%x %x)' % (h0, h1))
    st = Stream(d, p); last = -1
    for _ in range((A - 0x18) // 512):
        if d[st.p] == 5: st.p += 1; out += d[st.p:st.p + 512]; st.p += 512
        else:
            tl, last = tile(st, tabs, last); out += tl
    n1 = struct.unpack_from('<I', d, st.p)[0]; st.p += 4
    out += d[st.p:st.p + n1]
    if len(out) != total: raise ValueError('size %x != %x' % (len(out), total))
    return bytes(out)

if __name__ == '__main__':
    import sys
    tabs = load_tables('work/g13jnr/ram/r03300.bin')
    n = open('work/g13jnr/nand_plain.bin', 'rb').read()
    out = unpack(n, tabs, 0x4248000)
    r = open('work/g13jnr/ram/r03300.bin', 'rb').read()[0x278664:0x278664 + len(out)]
    print(len(out), out == r, sum(1 for a, b in zip(out, r) if a != b))
