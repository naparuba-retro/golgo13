"""Golgo 13 graphics pack library: reads the tiled 4bpp image packs of the banked ROM and describes them structurally
(tile pool, palettes, layouts with per-entry tile indexes) so tools can both render and *rewrite* them.

Pack format (little endian), reverse engineered and verified against what the game draws:
  +0x00  u32 0x18, u32 0x18, u32 A, u32 A, u32 B, u16 ntiles, u16 npal
  +0x18  ntiles tiles of 32x32 pixels, 4 bits per pixel (512 bytes each, low nibble = left pixel)
  +A     palette section: npal u32 offsets, then 16-colour PS1 palettes (15-bit BGR)
  +B     layout section: nlayout u32 offsets (relative to B); each layout:
           u16 width, u16 height, u16 palette index, u16 bits (4), u16 F, then 8-byte entries: u16 x, u16 y, u8 w, u8 h, u16 tile id
  The entry table is one slot ahead of the picture: the tile shown at the position of entry n is the id of entry n-1
  (the first position shows F). Tile index = id (0-based); 0xffff = end marker.
All offsets in this module are offsets into the 'bank' image (the banked ROM as the game sees it, see load_bank)."""
import os, struct, zlib
import numpy as np

BANK_SIZE = 0x3c00000
BANK_SPEC = [(0x0, 'glg1rom0l.10', 'glg1rom0u.14'), (0x1000000, 'glg1rom1l.11', 'glg1rom1u.15'), (0x2000000, 'glg1rom2l.12', 'glg1rom2u.16'),
             (0x3000000, 'glg1fl3l.4', 'glg1fl3u.5'), (0x3400000, 'glg1fl4l.6', 'glg1fl4u.7'), (0x3800000, 'glg1fl5l.8', 'glg1fl5u.9')]

BANK_SPEC_KND = [(0x0, 'gls1rom0l.ic10', 'gls1rom0u.ic14'), (0x1000000, 'gls1rom1l.ic11', 'gls1rom1u.ic15'), (0x2000000, 'gls1rom2l.ic12', 'gls1rom2u.ic16'),
                 (0x3000000, 'gls1fl3l.ic4', 'gls1fl3u.ic5'), (0x3400000, 'gls1fl4l.ic6', 'gls1fl4u.ic7'), (0x3800000, 'gls1fl5l.ic8', 'gls1fl5u.ic9')]

def load_bank(rom_dir):
    if os.path.isfile(rom_dir): return open(rom_dir, 'rb').read()      # a ready-made bank image (g13jnr: tools/jnr/packs.py)
    bank = bytearray(BANK_SIZE)
    spec = BANK_SPEC_KND if os.path.exists(os.path.join(rom_dir, BANK_SPEC_KND[0][1])) else BANK_SPEC   # g13knd or golgo13 file names
    for off, l, u in spec:
        a = open(os.path.join(rom_dir, l), 'rb').read(); b = open(os.path.join(rom_dir, u), 'rb').read()
        bank[off:off + 2 * len(a):2] = a; bank[off + 1:off + 2 * len(a):2] = b
    return bytes(bank)

def write_png(path, rgba):
    h, w, _ = rgba.shape; raw = b''.join(b'\x00' + rgba[y].tobytes() for y in range(h))
    def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 6)) + ch(b'IEND', b''))

def pal_rgba(p16):
    out = np.zeros((16, 4), np.uint8)
    for i in range(16):
        v = struct.unpack_from('<H', p16, i * 2)[0]
        out[i] = ((v & 31) << 3 | (v & 31) >> 2, (v >> 5 & 31) << 3 | (v >> 5 & 31) >> 2, (v >> 10 & 31) << 3 | (v >> 10 & 31) >> 2, 0 if v == 0 else 255)
    return out

def pack_info(d, base, limit=0x400000):
    """Structured description of the pack at bank offset `base`, or None if there is no valid pack there."""
    if base + 0x18 > len(d): return None
    h0, h1, A, A2, B, nt, npal = struct.unpack_from('<5IHH', d, base)
    if h0 != 0x18 or h1 != 0x18 or A != A2 or A != 0x18 + nt * 512 or not (0 < nt < 4000) or not (0 < npal < 256) or B <= A or B > limit: return None
    if base + B + 4 > len(d): return None
    pal_offs = struct.unpack_from('<%dI' % npal, d, base + A)
    if pal_offs[0] != npal * 4 or B - A < npal * 4 + 32: return None
    pals = [pal_rgba(d[base + A + o: base + A + o + 32]) for o in pal_offs]
    first = struct.unpack_from('<I', d, base + B)[0]
    nl = first // 4
    if not (0 < nl < 512): return None
    loffs = struct.unpack_from('<%dI' % nl, d, base + B)
    layouts = []
    for li, lo in enumerate(loffs):
        o = base + B + lo
        if o + 10 > len(d): return None
        w, h, pi, bits, F = struct.unpack_from('<5H', d, o)
        if bits != 4 or not (0 < w <= 1024) or not (0 < h <= 1024) or pi >= npal: return None
        end = base + B + (loffs[li + 1] if li + 1 < nl else min(limit - B, 1 << 20))
        raw = []; p = o + 10
        while p + 8 <= end and p + 8 <= len(d):
            x, y, tw, th, tid = struct.unpack_from('<HHBBH', d, p); p += 8
            if tw == 0 or th == 0 or tw > 32 or th > 32 or x > 2048 or y > 2048: break
            raw.append((x, y, tw, th, tid))
        if not raw: return None
        ids = [F] + [e[4] for e in raw[:-1]]
        ents = [{'slot': n, 'x': x, 'y': y, 'w': tw, 'h': th, 'tile': (tid if tid < nt else None)} for n, ((x, y, tw, th, _), tid) in enumerate(zip(raw, ids))]
        W = max(w, max(e['x'] + e['w'] for e in ents)); H = max(h, max(e['y'] + e['h'] for e in ents))
        layouts.append({'index': li, 'w': w, 'h': h, 'W': W, 'H': H, 'pal': pi, 'F': F, 'entries': ents})
    return {'base': base, 'nt': nt, 'npal': npal, 'pool_off': base + 0x18, 'pal_off': base + A, 'pal_offsets': list(pal_offs),
            'palettes': pals, 'layouts': layouts}

def tile_pixels(d, info, idx):
    """32x32 array of palette indexes of pool tile idx."""
    t = np.frombuffer(d, np.uint8, 512, info['pool_off'] + idx * 512).reshape(32, 16)
    px = np.zeros((32, 32), np.uint8); px[:, 0::2] = t & 15; px[:, 1::2] = t >> 4
    return px

def render_layout(d, info, layout):
    pal = info['palettes'][layout['pal']]
    img = np.zeros((layout['H'], layout['W'], 4), np.uint8)
    for e in layout['entries']:
        if e['tile'] is None: continue
        img[e['y']:e['y'] + e['h'], e['x']:e['x'] + e['w']] = pal[tile_pixels(d, info, e['tile'])[:e['h'], :e['w']]]
    return img

def tile_use_counts(info):
    """How many layout entries (over the whole pack) show each pool tile. A tile used once can be rewritten safely."""
    use = {}
    for lay in info['layouts']:
        for e in lay['entries']:
            if e['tile'] is not None: use[e['tile']] = use.get(e['tile'], 0) + 1
    return use

def find_packs(d, limit=0x400000):
    pat = struct.pack('<II', 0x18, 0x18); pos = 0; out = []
    while True:
        pos = d.find(pat, pos)
        if pos < 0: break
        info = pack_info(d, pos, limit)
        if info: out.append(info); pos += 0x18
        else: pos += 4
    return out

def pack_tile_bytes(px):
    """Inverse of tile_pixels: 32x32 palette indexes -> 512 bytes."""
    px = np.asarray(px, np.uint8)
    return ((px[:, 0::2] & 15) | ((px[:, 1::2] & 15) << 4)).astype(np.uint8).tobytes()
