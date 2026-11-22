#!/usr/bin/env python3
"""Builds a translation patch (translations/<lang>/patch.g13t and, for pictures drawn at run time, overlay.g13o) from translations/<lang>/strings.json.

usage: build.py --lang en [--rom roms/golgo13] [--translations translations] [--preview]

Nothing about a particular image is hard coded here. Each entry of strings.json names an asset id ("<pack>/<layout>", see
`manifest.py`) and either a text (rendered with a style) or a replacement image file. The tool renders the new picture inside the
original canvas, quantizes it to the image's own 16-colour palette, rewrites the pool tiles it covers and records the byte changes.

strings.json
  {"meta": {...},
   "styles": { "<name>": {"font": "impact.ttf", "size": 40, "min_size": 14, "fill": "#ffffff", "outline": "#000000", "outline_width": 2,
                           "align": "center", "valign": "middle", "padding": [l, t, r, b], "base": "blank"|"original",
                           "background": "transparent"|"#rrggbb", "inset": [l, t, r, b]  (region repainted when base is original)}},
   "assets": { "030a9100/02": {"text": "Semiconductor Crisis", "style": "story_title"},
               "03189b20/11": {"text": "...", "style": "plate", "lines": ["...", "..."]},
               "xxxxxxxx/NN": {"image": "custom/file.png"} }}      (image paths are relative to the language folder)
Styles defined in translations/styles.json are shared by all languages and can be overridden in a language's strings.json.

Patch file (little endian): "G13T", u32 version=1, u32 count, then per record: u32 region (0 = banked ROM, 1 = main ROM), u32 offset,
u32 length, u32 crc32 of the ORIGINAL bytes (the game refuses to apply a record whose original bytes differ), then the new bytes."""
import sys, os, json, struct, zlib, argparse, glob
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g13pack as G
from PIL import Image, ImageDraw, ImageFont

SYSTEM_FONT_DIRS = ['C:/Windows/Fonts', '/usr/share/fonts', '/usr/local/share/fonts', os.path.expanduser('~/.fonts'), os.path.expanduser('~/.local/share/fonts'),
                    '/System/Library/Fonts', '/Library/Fonts']

def find_font(name, lang_dir):
    for cand in (os.path.join(lang_dir, name), name):
        if os.path.isfile(cand): return cand
    for root in SYSTEM_FONT_DIRS:
        if not os.path.isdir(root): continue
        for dp, _, files in os.walk(root):
            for f in files:
                if f.lower() == name.lower(): return os.path.join(dp, f)
    raise SystemExit('font not found: %s (put it in the language folder or install it)' % name)

def parse_color(c):
    if c in (None, 'transparent'): return None
    c = c.lstrip('#'); return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))

def quantize(rgba, pal):
    """RGBA image -> palette indexes. Transparent pixels use the palette entry that is transparent (alpha 0), else index 0."""
    h, w, _ = rgba.shape
    trans = [i for i in range(16) if pal[i][3] == 0]; opaque = [i for i in range(16) if pal[i][3] != 0]
    tidx = trans[0] if trans else 0
    P = pal[opaque][:, :3].astype(int)
    out = np.full((h, w), tidx, np.uint8)
    rgb = rgba[:, :, :3].astype(int); a = rgba[:, :, 3]
    d = ((rgb[:, :, None, :] - P[None, None, :, :]) ** 2).sum(axis=3)
    best = np.array(opaque, np.uint8)[d.argmin(axis=2)]
    solid = a >= 128
    out[solid] = best[solid]
    if not trans: out[~solid] = best[~solid]
    return out

def draw_text(canvas_wh, lines, style, lang_dir, fill, outline, ow, area):
    """Returns an RGBA layer with the text drawn (centered per style) inside `area` = (x0, y0, x1, y1)."""
    W, H = canvas_wh
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer, 'RGBA')
    fpath = find_font(style.get('font', 'arialbd.ttf'), lang_dir)
    size = int(style.get('size', 20)); min_size = int(style.get('min_size', 8))
    ax0, ay0, ax1, ay1 = area; aw, ah = ax1 - ax0, ay1 - ay0
    while True:
        font = ImageFont.truetype(fpath, size)
        boxes = [d.textbbox((0, 0), ln, font=font, stroke_width=ow) for ln in lines]
        lh = int(style.get('line_height', 0)) or (font.getmetrics()[0] + font.getmetrics()[1])
        tw = max(b[2] - b[0] for b in boxes); th = (len(lines) - 1) * lh + (boxes[-1][3] - boxes[0][1])
        if (tw <= aw and th <= ah) or size <= min_size: break
        size -= 1
    if tw > aw or th > ah: print('  warning: text does not fit even at size %d (%dx%d in %dx%d)' % (size, tw, th, aw, ah))
    align = style.get('align', 'center'); valign = style.get('valign', 'middle')
    y = ay0 + {'top': 0, 'middle': (ah - th) // 2, 'bottom': ah - th}[valign] - boxes[0][1]
    for ln, b in zip(lines, boxes):
        w_ = b[2] - b[0]
        x = ax0 + {'left': 0, 'center': (aw - w_) // 2, 'right': aw - w_}[align] - b[0]
        d.text((x, y), ln, font=font, fill=fill + (255,), stroke_width=ow, stroke_fill=(outline + (255,)) if outline else None)
        y += lh
    return layer, size

def asset_canvas(d, info, lay, style):
    pal = info['palettes'][lay['pal']]
    return G.render_layout(d, info, lay), pal

def build_asset(d, info, lay, spec, styles, lang_dir):
    """Returns (new_index_image HxW, report dict)."""
    W, H = lay['W'], lay['H']; pal = info['palettes'][lay['pal']]
    orig = G.render_layout(d, info, lay)
    if 'image' in spec:
        im = Image.open(os.path.join(lang_dir, spec['image'])).convert('RGBA')
        if im.size != (W, H): raise SystemExit('image %s is %s but the canvas is %dx%d' % (spec['image'], im.size, W, H))
        return quantize(np.array(im), pal), {'mode': 'image'}
    style = dict(styles.get(spec.get('style', 'default'), {}))
    style.update(spec.get('style_override', {}))
    lines = spec.get('lines') or [spec['text']]
    base = style.get('base', 'blank')
    rgba = np.array(orig) if base == 'original' else np.zeros((H, W, 4), np.uint8)
    pad = style.get('padding', [4, 4, 4, 4])
    inset = style.get('inset', [0, 0, 0, 0]) if base == 'original' else [0, 0, 0, 0]
    area = (inset[0] + pad[0], inset[1] + pad[1], W - inset[2] - pad[2], H - inset[3] - pad[3])
    if base == 'original':
        bg = parse_color(style.get('background', 'transparent'))
        x0, y0, x1, y1 = inset[0], inset[1], W - inset[2], H - inset[3]
        rgba[y0:y1, x0:x1] = (bg + (255,)) if bg else (0, 0, 0, 0)
    fill = parse_color(style.get('fill', '#ffffff')) or (255, 255, 255)
    outline = parse_color(style.get('outline')); ow = int(style.get('outline_width', 0)) if outline else 0
    layer, size = draw_text((W, H), lines, style, lang_dir, fill, outline, ow, area)
    comp = Image.fromarray(rgba, 'RGBA'); comp.alpha_composite(layer)
    return quantize(np.array(comp), pal), {'mode': 'text', 'font_size': size, 'lines': lines}

def fnv64(b):
    h = 1469598103934665603
    for x in b: h = ((h ^ x) * 1099511628211) & 0xffffffffffffffff
    return h or 1

def wrap_lines(draw, text, font, maxw, stroke=0):
    words = text.split(' '); lines = []; cur = ''
    for w in words:
        t = (cur + ' ' + w) if cur else w
        if draw.textlength(t, font=font) <= maxw or not cur: cur = t
        else: lines.append(cur); cur = w
    if cur: lines.append(cur)
    return lines

def paint_text(W, H, lines, font, x0, y0, aw, lh, style):
    """RGBA layer (WxH) with the lines drawn from (x0, y0): alignment inside width aw, optional vertical gradient fill, outline, drop shadow."""
    fill = parse_color(style.get('fill', '#ffffff')) or (255, 255, 255)
    outline = parse_color(style.get('outline')); ow = int(style.get('outline_width', 0)) if outline else 0
    grad = style.get('gradient')                       # [top colour, bottom colour] over the text height
    sh = style.get('shadow')                           # {"offset": [dx, dy], "color": "#000000"}
    mask = Image.new('L', (W, H), 0); omask = Image.new('L', (W, H), 0)
    md = ImageDraw.Draw(mask); od = ImageDraw.Draw(omask); y = y0; top = None; bottom = None
    for ln in lines:
        w_ = md.textlength(ln, font=font)
        x = x0 + {'left': 0, 'center': (aw - w_) / 2, 'right': aw - w_}[style.get('align', 'left')]
        md.text((x, y), ln, font=font, fill=255)
        od.text((x, y), ln, font=font, fill=255, stroke_width=ow)
        bb = md.textbbox((x, y), ln, font=font); top = bb[1] if top is None else min(top, bb[1]); bottom = bb[3] if bottom is None else max(bottom, bb[3])
        y += lh
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    if sh:
        dx, dy = sh.get('offset', [2, 2]); sc = parse_color(sh.get('color', '#000000')) or (0, 0, 0)
        shadow = Image.new('RGBA', (W, H), sc + (255,)); shadow.putalpha(omask)
        layer.alpha_composite(shadow, (max(dx, 0), max(dy, 0)), (max(-dx, 0), max(-dy, 0)))
    if ow:
        oc = Image.new('RGBA', (W, H), outline + (255,)); oc.putalpha(omask); layer.alpha_composite(oc)
    if grad and top is not None:
        c0 = parse_color(grad[0]); c1 = parse_color(grad[1]); col = np.zeros((H, W, 4), np.uint8); col[:, :, 3] = 255
        for yy in range(H):
            t = min(max((yy - top) / max(bottom - top, 1), 0.0), 1.0)
            col[yy, :, :3] = [int(c0[k] + (c1[k] - c0[k]) * t) for k in range(3)]
        fg = Image.fromarray(col, 'RGBA')
    else: fg = Image.new('RGBA', (W, H), fill + (255,))
    fg.putalpha(mask); layer.alpha_composite(fg)
    return layer

def fit_text(spec, style, lang_dir, aw, ah):
    """Chooses the font size / line breaks so the text fits aw x ah. Returns (font, lines, line_height, size, fits)."""
    fpath = find_font(style.get('font', 'arialbd.ttf'), lang_dir)
    max_lines = int(style.get('max_lines', 2)); size = int(style.get('size', 24)); min_size = int(style.get('min_size', 10))
    tmp = ImageDraw.Draw(Image.new('RGBA', (8, 8))); text = spec.get('text', '')
    while True:
        font = ImageFont.truetype(fpath, size)
        lines = spec.get('lines') or wrap_lines(tmp, text, font, aw)
        asc, desc = font.getmetrics(); lh = int(style.get('line_height', 0)) or (asc + desc)
        ok = len(lines) <= max_lines and (len(lines) - 1) * lh + asc + desc <= ah + 4 and all(tmp.textlength(l, font=font) <= aw for l in lines)
        if ok or size <= min_size: return font, lines, lh, size, ok
        size -= 1

MIN_KEY_PIXELS = 0
def build_overlay(d, info, lay, spec, styles, lang_dir):
    """Free-form RGBA picture for a layout (no tile limits). Returns (rgba HxWx4, keys [(hash, x, y, w, h)], report)."""
    W, H = lay['W'], lay['H']
    style = dict(styles.get(spec.get('style', 'default'), {})); style.update(spec.get('style_override', {}))
    if 'image' in spec:
        im = Image.open(os.path.join(lang_dir, spec['image'])).convert('RGBA')
        if im.size != (W, H): raise SystemExit('image %s is %s but the canvas is %dx%d' % (spec['image'], im.size, W, H))
        rgba = np.array(im); rep = {'mode': 'overlay-image'}
    else:
        ax0, ay0, ax1, ay1 = style.get('area', [0, 0, W, H])
        font, lines, lh, size, ok = fit_text(spec, style, lang_dir, ax1 - ax0, ay1 - ay0)
        if not ok: print('  warning: %s does not fit (%d lines at size %d)' % (spec.get('text'), len(lines), size))
        ty = ay0
        if style.get('valign') == 'middle':
            th = (len(lines) - 1) * lh + sum(font.getmetrics()); ty = ay0 + (ay1 - ay0 - th) // 2
        text = paint_text(W, H, lines, font, ax0, ty, ax1 - ax0, lh, style)
        if style.get('base') == 'original':                 # keep the original picture, clear the `erase` rectangle, draw the text there
            base = G.render_layout(d, info, lay).copy(); ex0, ey0, ex1, ey1 = style.get('erase', [ax0, ay0, ax1, ay1]); base[ey0:ey1, ex0:ex1] = (list(parse_color(style['erase_fill'])) + [255]) if style.get('erase_fill') else 0
            if style.get('frame'):                            # border of `frame_width` px (default 2) around the erased rectangle
                fc = list(parse_color(style['frame'])) + [255]; fw = int(style.get('frame_width', 2))
                base[ey0:ey0 + fw, ex0:ex1] = fc; base[ey1 - fw:ey1, ex0:ex1] = fc; base[ey0:ey1, ex0:ex0 + fw] = fc; base[ey0:ey1, ex1 - fw:ex1] = fc
            img = Image.fromarray(base, 'RGBA'); img.alpha_composite(text); rgba = np.array(img)
        else: rgba = np.array(text)
        rep = {'mode': 'overlay', 'font_size': size, 'lines': lines}
    # keys: pixels of the original tiles (unique, non-uniform) so the game's own drawing of this picture can be recognised
    use = G.tile_use_counts(info); keys = []
    for e in lay['entries']:
        t = e['tile']
        if t is None or use[t] != 1: continue
        px = G.tile_pixels(d, info, t)
        if len(np.unique(px)) < 2: continue
        if MIN_KEY_PIXELS and (px != np.bincount(px.ravel()).argmax()).sum() < MIN_KEY_PIXELS: continue     # near-uniform tiles also occur in other pictures
        keys.append((fnv64(d[info['pool_off'] + t * 512: info['pool_off'] + (t + 1) * 512]), e['x'], e['y'], 0, 0))
    return rgba, keys, rep

_CAPS = {}
def read_capture(path):
    if path in _CAPS: return _CAPS[path]
    b = open(path, 'rb').read(); assert b[:4] == b'G13C' and struct.unpack_from('<I', b, 4)[0] == 1, 'bad capture file'
    p = 8; quads = []
    while p + 40 <= len(b):
        x, y, w, h, tp, cx, cy, uni = struct.unpack_from('<8i', b, p); hs = struct.unpack_from('<Q', b, p + 32)[0]; p += 40
        rgba = np.frombuffer(b, np.uint8, w * h * 4, p).reshape(h, w, 4).copy(); p += w * h * 4
        quads.append({'x': x, 'y': y, 'w': w, 'h': h, 'tp': tp, 'clut': (cx, cy), 'uniform': bool(uni), 'hash': hs, 'rgba': rgba})
    _CAPS[path] = quads; return quads

def build_capture_overlay(spec, styles, lang_dir, trans_dir):
    """Overlay built from a recorded scene: selected sprite quads are recognised (by their pixels) and replaced by the recorded
    picture with the `erase` area cleared and the translated text drawn in it."""
    cap = capfile(trans_dir, spec['capture'])
    if not os.path.isfile(cap): raise SystemExit('missing capture %s (run: python tools/translation/capture_scene.py %s)' % (cap, spec['capture']))
    sel = spec['select']; rx0, ry0, rx1, ry1 = sel['rect']
    quads = [q for q in read_capture(cap) if q['x'] < rx1 and q['x'] + q['w'] > rx0 and q['y'] < ry1 and q['y'] + q['h'] > ry0
             and ('tp' not in sel or q['tp'] in sel['tp']) and ('clut_y' not in sel or q['clut'][1] in sel['clut_y'])]
    if not quads: raise SystemExit('no quad selected for %s' % spec['capture'])
    ox = min(q['x'] for q in quads); oy = min(q['y'] for q in quads); W = max(q['x'] + q['w'] for q in quads) - ox; H = max(q['y'] + q['h'] for q in quads) - oy
    base = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    for q in quads: base.alpha_composite(Image.fromarray(q['rgba'], 'RGBA'), (q['x'] - ox, q['y'] - oy))
    ex0, ey0, ex1, ey1 = spec.get('erase', sel['rect'])
    arr = np.array(base); arr[max(ey0 - oy, 0):max(ey1 - oy, 0), max(ex0 - ox, 0):max(ex1 - ox, 0)] = 0; base = Image.fromarray(arr, 'RGBA')
    style = dict(styles.get(spec.get('style', 'default'), {})); style.update(spec.get('style_override', {}))
    ax0, ay0, ax1, ay1 = spec.get('text_area', [ex0, ey0, ex1, ey1])
    font, lines, lh, size, ok = fit_text(spec, style, lang_dir, ax1 - ax0, ay1 - ay0)
    if not ok: print('  warning: %s does not fit (%d lines at size %d)' % (spec.get('text'), len(lines), size))
    if style.get('valign', 'top') == 'middle':
        asc, desc = font.getmetrics(); th = (len(lines) - 1) * lh + asc + desc; ty = ay0 + (ay1 - ay0 - th) // 2
    else: ty = ay0
    base.alpha_composite(paint_text(W, H, lines, font, ax0 - ox, ty - oy, ax1 - ax0, lh, style))
    keys = [(q['hash'], q['x'] - ox, q['y'] - oy, q['w'], q['h']) for q in quads if not q['uniform']]
    return np.array(base), keys, {'mode': 'capture-overlay', 'font_size': size, 'lines': lines, 'quads': len(quads), 'origin': [ox, oy]}

def crop_overlay(rgba, keys):
    """Crops the bitmap to its non-transparent bounding box (keys are relative to the bitmap origin, so they are shifted)."""
    ys, xs = np.where(rgba[:, :, 3] > 0)
    if len(xs) == 0: return rgba[:1, :1], keys, 0, 0
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    return rgba[y0:y1, x0:x1], [(h, x - int(x0), y - int(y0), kw, kh) for h, x, y, kw, kh in keys], int(x0), int(y0)

def read_overlay_pictures(path):
    """Raw picture records of a G13O file (bytes each), so that overlays of other games can be reused as they are."""
    d = open(path, 'rb').read()
    if d[:4] != b'G13O': raise ValueError(path)
    ver, n = struct.unpack_from('<II', d, 4); p = 12; out = []
    for _ in range(n):
        q = p; w, h, nk, fl = struct.unpack_from('<IIII', d, p); p += 16
        if fl & 2: p += 12
        p += nk * 20; cl = struct.unpack_from('<I', d, p)[0]; p += 4 + cl; out.append(d[q:p])
    return out

def append_inherited(path, files):
    """Appends the pictures of the given overlay files (pixel-recognised, so valid for any game that draws the same sprites) to `path`."""
    d = open(path, 'rb').read(); n = struct.unpack_from('<I', d, 8)[0]; extra = []
    for f in files:
        if os.path.isfile(f): extra += read_overlay_pictures(f)
        else: print('inherit: %s not found (skipped)' % f)
    open(path, 'wb').write(d[:8] + struct.pack('<I', n + len(extra)) + d[12:] + b''.join(extra))
    print('inherited %d picture(s) from %d file(s)' % (len(extra), len(files)))

def write_overlays(path, overlays):
    """overlays: list of (rgba, keys, paint, sample). Drops key hashes that are not unique across the whole file. Format "G13O" v4 (see runtime/gpu/gpu.c);
    bitmaps are cropped to their content and stored as raw deflate (flag bit2)."""
    # a sprite drawn twice one or two pixels apart (double draw) must still count as one key: keep the first copy
    dd = []
    for ov in overlays:
        seen = {}; ks = []
        for k in ov[1]:
            o = seen.get(k[0])
            if o is not None and abs(o[1] - k[1]) <= 8 and (abs(o[2] - k[2]) <= 8 or abs(abs(o[2] - k[2]) - 240) <= 8) and k[3]: continue   # double draw (shadow) or the same sprite in both 240-line buffers
            seen.setdefault(k[0], k); ks.append(k)
        dd.append((ov[0], ks) + tuple(ov[2:]))
    overlays = dd
    count = {}; owners = {}
    for i, ov in enumerate(overlays):
        sig = hash(np.ascontiguousarray(ov[0]).tobytes())
        for k in ov[1]: owners.setdefault(k[0], []).append((i, sig, k[1], k[2]))
    first = {}                       # a hash shared by overlays that are strictly identical (same bitmap, same key offset) is kept once, in the first owner
    for h, L in owners.items():
        if len({(sg, x, y) for _, sg, x, y in L}) == 1: first[h] = L[0][0]
        count[h] = 1 if h in first else len(L)
    kinds = {k[0]: (k[3] | (k[4] << 16)) if k[3] else 0 for ov in overlays for k in ov[1]}   # 0 = 32x32 tile key, else w | h << 16 (sprite rectangle key)
    # tile keys and sprite keys used by several pictures (or twice in one) cannot select a picture: they are only hidden (never for sprites a paint-over picture recognises the scene by)
    paint_of = [bool(ov[2]) if len(ov) > 2 else False for ov in overlays]
    shared = sorted(h for h, c in count.items() if c > 1 and not (kinds[h] and any(paint_of[i] for i, _, _, _ in owners[h])))   # tile keys used by several different pictures: cannot tell which one is shown, so only hide the tile (each picture draws its own text from its other tiles)
    with open(path, 'wb') as f:
        f.write(b'G13O' + struct.pack('<II', 4, len(overlays) + (1 if shared else 0)))
        for oi, ov in enumerate(overlays):
            paint = ov[2] if len(ov) > 2 else False; sample = ov[3] if len(ov) > 3 else None
            rgba, keys, cx, cy = crop_overlay(ov[0], [k for k in ov[1] if count[k[0]] == 1 and first.get(k[0], oi) == oi])
            if sample: sample = (sample[0] - cx, sample[1] - cy, sample[2])
            f.write(struct.pack('<IIII', rgba.shape[1], rgba.shape[0], len(keys), (1 if paint else 0) | (2 if sample else 0) | 4))
            if sample: f.write(struct.pack('<iiI', sample[0], sample[1], (sample[2][0] << 16) | (sample[2][1] << 8) | sample[2][2]))
            for h, x, y, kw, kh in keys: f.write(struct.pack('<IIiiI', h & 0xffffffff, h >> 32, x, y, (kw | (kh << 16)) if kw else 0))
            co = zlib.compressobj(9, zlib.DEFLATED, -15); comp = co.compress(np.ascontiguousarray(rgba).astype(np.uint8).tobytes()) + co.flush()
            f.write(struct.pack('<I', len(comp)) + comp)
        if shared:
            f.write(struct.pack('<IIII', 1, 1, len(shared), 4))
            for h in shared: f.write(struct.pack('<IIiiI', h & 0xffffffff, h >> 32, 0, 0, kinds[h]))
            co = zlib.compressobj(9, zlib.DEFLATED, -15); comp = co.compress(bytes(4)) + co.flush(); f.write(struct.pack('<I', len(comp)) + comp)
        print('overlay file: %d pictures, %d shared tile keys hidden only' % (len(overlays), len(shared)))

def dominant_color(quads, rect):
    """Most frequent opaque colour of the captured sprites inside rect (used to fill the area before writing the new text)."""
    x0, y0, x1, y1 = rect; cnt = {}
    for q in quads:
        sx0, sy0 = max(x0, q['x']), max(y0, q['y']); sx1, sy1 = min(x1, q['x'] + q['w']), min(y1, q['y'] + q['h'])
        if sx0 >= sx1 or sy0 >= sy1: continue
        reg = q['rgba'][sy0 - q['y']:sy1 - q['y'], sx0 - q['x']:sx1 - q['x']].reshape(-1, 4)
        for c in reg[reg[:, 3] > 0]: cnt[tuple(int(v) for v in c[:3])] = cnt.get(tuple(int(v) for v in c[:3]), 0) + 1
    return max(cnt, key=cnt.get) if cnt else (0, 0, 0)

def build_paint_overlay(spec, styles, lang_dir, trans_dir):
    """Paint-over overlay: the scene is recognised by the sprites of `select`, which keep being drawn by the game; at the end of the frame a rectangle
    (`paint.rect`, filled with `paint.fill`, "auto" = dominant colour of the captured sprites there) is painted over them and the translated text is written on it."""
    cap = capfile(trans_dir, spec['capture'])
    if not os.path.isfile(cap): raise SystemExit('missing capture %s (run: python tools/translation/capture_scene.py %s)' % (cap, spec['capture']))
    px0, py0, px1, py1 = spec['paint']['rect']; W, H = px1 - px0, py1 - py0
    allq = read_capture(cap); sel = spec.get('select', {'rect': spec['paint']['rect']}); rx0, ry0, rx1, ry1 = sel['rect']
    quads = [q for q in allq if q['x'] < rx1 and q['x'] + q['w'] > rx0 and q['y'] < ry1 and q['y'] + q['h'] > ry0
             and ('tp' not in sel or q['tp'] in sel['tp']) and ('clut_y' not in sel or q['clut'][1] in sel['clut_y'])]
    if sel.get('distinct_from'):        # keep only the sprites that other scenes do not draw (e.g. the text of one plate, not the frame shared by all plates)
        other = set()
        for sc in sel['distinct_from']:
            for q in read_capture(capfile(trans_dir, sc)): other.add(q['hash'])
        quads = [q for q in quads if q['hash'] not in other]
    fill = spec['paint'].get('fill', 'auto')
    col = dominant_color(allq, spec['paint']['rect']) if fill == 'auto' else parse_color(fill)
    base = Image.new('RGBA', (W, H), tuple(col) + (255,))
    style = dict(styles.get(spec.get('style', 'default'), {})); style.update(spec.get('style_override', {}))
    ax0, ay0, ax1, ay1 = spec.get('text_area', [0, 0, W, H])
    font, lines, lh, size, ok = fit_text(spec, style, lang_dir, ax1 - ax0, ay1 - ay0)
    if not ok: print('  warning: %s does not fit (%d lines at size %d)' % (spec.get('text'), len(lines), size))
    th = (len(lines) - 1) * lh + sum(font.getmetrics()); ty = ay0 + (ay1 - ay0 - th) // 2 if style.get('valign', 'middle') == 'middle' else ay0
    base.alpha_composite(paint_text(W, H, lines, font, ax0, ty, ax1 - ax0, lh, style))
    keys = [(q['hash'], q['x'] - px0, q['y'] - py0, q['w'], q['h']) for q in quads if not q['uniform']]
    return np.array(base), keys, {'mode': 'paint', 'font_size': size, 'lines': lines, 'quads': len(quads), 'fill': list(col), 'sample': ([spec['paint']['sample'][0], spec['paint']['sample'][1], [int(c) for c in col]] if spec['paint'].get('sample') else None)}

def tile_patches(d, info, lay, new_idx):
    """Compares the new index image with the current picture and returns {tile: new 32x32 index array} plus problems."""
    use = G.tile_use_counts(info); changes = {}; problems = []
    for e in lay['entries']:
        x, y, w, h = e['x'], e['y'], e['w'], e['h']
        cur = G.tile_pixels(d, info, e['tile'])[:h, :w] if e['tile'] is not None else None
        new = new_idx[y:y + h, x:x + w]
        pal = info['palettes'][lay['pal']]
        if cur is None:
            # no tile here: acceptable only if the new picture is also "nothing" (transparent)
            if not (pal[new][:, :, 3] == 0).all(): problems.append((e['x'], e['y'], 'position has no tile'))
            continue
        if (cur == new).all(): continue
        if use[e['tile']] > 1: problems.append((e['x'], e['y'], 'tile %d is shared by %d positions' % (e['tile'], use[e['tile']]))); continue
        t = changes.get(e['tile'], G.tile_pixels(d, info, e['tile']).copy()); t[:h, :w] = new; changes[e['tile']] = t
    return changes, problems

def capfile(trans_dir, name):
    """Scene capture file: the language's own translations dir first, else the parent's (game dirs share the golgo13 captures: sprites are recognised by pixels)."""
    p = os.path.join(trans_dir, 'captures', name + '.cap')
    return p if os.path.exists(p) else os.path.join(trans_dir, '..', 'captures', name + '.cap')

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--lang', required=True); ap.add_argument('--rom', default='roms/golgo13')
    ap.add_argument('--translations', default='translations'); ap.add_argument('--preview', action='store_true', help='also write PNG previews')
    a = ap.parse_args()
    lang_dir = os.path.join(a.translations, a.lang)
    strings = json.load(open(os.path.join(lang_dir, 'strings.json'), encoding='utf-8'))
    for extra in sorted(glob.glob(os.path.join(lang_dir, 'strings.*.json'))):          # more files: strings.<anything>.json (assets/styles are merged)
        x = json.load(open(extra, encoding='utf-8')); strings['assets'].update(x.get('assets', {})); strings.setdefault('styles', {}).update(x.get('styles', {}))
    styles = {}
    sp = os.path.join(a.translations, 'styles.json')
    if os.path.isfile(sp): styles.update(json.load(open(sp, encoding='utf-8')).get('styles', {}))
    styles.update(strings.get('styles', {}))
    try: d = G.load_bank(a.rom); packs = {'%08x' % p['base']: p for p in G.find_packs(d)}
    except (OSError, IndexError): d = b''; packs = {}          # games without a ROM bank (g13jnr, System 10): capture/paint overlays only
    global MIN_KEY_PIXELS
    if strings.get('meta', {}).get('force_overlay'): MIN_KEY_PIXELS = 48
    records = []; overlays = []; report = {'lang': a.lang, 'assets': {}}; failed = 0; skipped = 0
    for aid, spec in sorted(strings['assets'].items()):
        if 'paint' in spec:                                     # paint-over overlay (scene recognised, game keeps drawing, text painted on top)
            try: rgba, keys, rep = build_paint_overlay(spec, styles, lang_dir, a.translations)
            except SystemExit as e: print('%s: SKIPPED (%s)' % (aid, e)); skipped += 1; continue
            overlays.append((rgba, keys, True, tuple(rep['sample']) if rep['sample'] else None)); report['assets'][aid] = dict(rep, ok=True, keys=len(keys))
            print('%s: %s -> paint overlay (%d recognition keys, fill %s)' % (aid, rep['lines'], len(keys), rep['fill']))
            if a.preview:
                pv = os.path.join(lang_dir, 'preview'); os.makedirs(pv, exist_ok=True); Image.fromarray(rgba, 'RGBA').save(os.path.join(pv, aid.replace('/', '_').replace(':', '_') + '.png'))
            continue
        if 'capture' in spec:                                   # asset built from a recorded scene (id is free-form, e.g. "cap:card_m1:ammo")
            try: rgba, keys, rep = build_capture_overlay(spec, styles, lang_dir, a.translations)
            except SystemExit as e: print('%s: SKIPPED (%s)' % (aid, e)); skipped += 1; continue       # a recorded scene that cannot be reproduced here: that text stays in Japanese
            overlays.append((rgba, keys)); report['assets'][aid] = dict(rep, ok=True, keys=len(keys))
            print('%s: %s -> overlay from capture (%d quads, %d recognition keys)' % (aid, rep['lines'], rep['quads'], len(keys)))
            if a.preview:
                pv = os.path.join(lang_dir, 'preview'); os.makedirs(pv, exist_ok=True)
                bg = np.zeros_like(rgba); bg[:, :, 3] = 255; bg[:, :, :3] = (40, 50, 90)
                img = Image.fromarray(bg, 'RGBA'); img.alpha_composite(Image.fromarray(rgba, 'RGBA')); img.save(os.path.join(pv, aid.replace('/', '_').replace(':', '_') + '.png'))
            continue
        pk, li = aid.split('/'); info = packs.get(pk)
        if info is None or int(li) >= len(info['layouts']): print('%s: unknown asset id' % aid); failed += 1; continue
        lay = info['layouts'][int(li)]
        if strings.get('meta', {}).get('force_overlay') and spec.get('style') == 'plate' and 'plate_ov' in styles: spec = dict(spec, style='plate_ov')
        mode = spec.get('mode') or styles.get(spec.get('style', 'default'), {}).get('mode', 'tiles')
        if mode == 'tiles' and strings.get('meta', {}).get('force_overlay'): mode = 'overlay'      # games whose packs live in RAM (g13jnr): nothing to rewrite in a ROM, always recognise + draw
        if mode == 'overlay':
            rgba, keys, rep = build_overlay(d, info, lay, spec, styles, lang_dir)
            overlays.append((rgba, keys)); report['assets'][aid] = dict(rep, ok=True, keys=len(keys))
            print('%s: %s -> overlay (%d recognition tiles)' % (aid, rep.get('lines', spec.get('image')), len(keys)))
            if a.preview:
                pv = os.path.join(lang_dir, 'preview'); os.makedirs(pv, exist_ok=True)
                bg = np.zeros_like(rgba); bg[:, :, 3] = 255; bg[:, :, :3] = 40
                img = Image.fromarray(bg, 'RGBA'); img.alpha_composite(Image.fromarray(rgba, 'RGBA')); img.save(os.path.join(pv, aid.replace('/', '_') + '.png'))
            continue
        new_idx, rep = build_asset(d, info, lay, spec, styles, lang_dir)
        changes, problems = tile_patches(d, info, lay, new_idx)
        if problems:
            print('%s: CANNOT APPLY (%s) first problems: %s' % (aid, spec.get('text', spec.get('image')), problems[:4])); failed += 1
            report['assets'][aid] = {'ok': False, 'problems': problems[:20]}; continue
        for t, px in sorted(changes.items()):
            off = info['pool_off'] + t * 512; new = G.pack_tile_bytes(px)
            records.append((0, off, d[off:off + 512], new))
        report['assets'][aid] = dict(rep, ok=True, tiles_changed=len(changes))
        print('%s: %s -> %d tiles rewritten' % (aid, rep.get('lines', spec.get('image')), len(changes)))
        if a.preview:
            pv = os.path.join(lang_dir, 'preview'); os.makedirs(pv, exist_ok=True)
            pal = info['palettes'][lay['pal']]; G.write_png(os.path.join(pv, aid.replace('/', '_') + '.png'), pal[new_idx])
    # merge records that touch the same tile twice (last one wins) and write the patch
    merged = {}
    for region, off, orig, new in records: merged[(region, off)] = (orig, new)
    out = os.path.join(lang_dir, 'patch.g13t')
    inherit = strings.get('meta', {}).get('inherit', [])
    ovp = os.path.join(lang_dir, 'overlay.g13o')
    if overlays: write_overlays(ovp, overlays); print('%d overlay picture(s) -> %s' % (len(overlays), ovp))
    elif inherit: open(ovp, 'wb').write(b'G13O' + struct.pack('<II', 4, 0))
    if inherit: append_inherited(ovp, inherit)
    with open(out, 'wb') as f:
        f.write(b'G13T' + struct.pack('<II', 1, len(merged)))
        for (region, off), (orig, new) in sorted(merged.items()):
            f.write(struct.pack('<IIII', region, off, len(new), zlib.crc32(orig) & 0xffffffff) + new)
    json.dump(report, open(os.path.join(lang_dir, 'patch.report.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    import unicodedata
    nm = unicodedata.normalize('NFKD', strings.get('meta', {}).get('name', a.lang)).encode('ascii', 'ignore').decode() or a.lang
    open(os.path.join(lang_dir, 'info.txt'), 'w', encoding='ascii').write(nm + '\n')
    print('%d records (%d bytes of tile data) -> %s%s' % (len(merged), 512 * len(merged), out, '' if not failed else '   [%d asset(s) skipped]' % failed))
    if skipped: print('%d scene-based text(s) skipped: their recorded scene could not be reproduced on this machine (those texts stay in the original language)' % skipped)
    return 1 if failed else 0

if __name__ == '__main__':
    sys.exit(main())
