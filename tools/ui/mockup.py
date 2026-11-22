#!/usr/bin/env python3
"""Maquettes du futur menu (PNG 1280x720) : sélecteur de jeu, options, commandes, pause. Les planches de manga viennent du dossier
personnel de l'utilisateur (Y:/Scan/Golgo 13) : les images produites restent dans work/ui (non versionnées).
usage: python tools/ui/mockup.py [out_dir]"""
import sys, os, math, random
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops, ImageOps
import numpy as np

W, H = 1280, 720
OUT = sys.argv[1] if len(sys.argv) > 1 else 'work/ui/mock'
os.makedirs(OUT, exist_ok=True)
BOOK = "Y:/Scan/Golgo 13/Golgo 13 - Le choix de l'auteur/%03d.jpg"
FD = 'C:/Windows/Fonts/'
def font(name, size): return ImageFont.truetype(FD + name, size)
IMP = lambda s: font('impact.ttf', s)
CMB = lambda s: font('comicbd.ttf', s)

INK = (16, 14, 18); PAPER = (244, 238, 220); RED = (214, 28, 36); YEL = (255, 208, 40); NAVY = (14, 20, 46); CYAN = (60, 200, 220)

def panel_crop(page, box, size, tint=None, contrast=1.25):
    """Crop (box in 400x560 thumbnail coordinates) of a manga page, scaled to `size`, optional duotone tint (dark, light)."""
    im = Image.open(BOOK % page).convert('L'); k = im.width / 400.0
    b = tuple(int(v * k) for v in box); im = im.crop(b)
    im = ImageOps.fit(im, size, Image.LANCZOS, centering=(0.5, 0.4))
    im = ImageOps.autocontrast(im, cutoff=1)
    if contrast != 1: im = im.point(lambda v: max(0, min(255, int((v - 128) * contrast + 128))))
    if tint: return ImageOps.colorize(im, black=tint[0], white=tint[1]).convert('RGBA')
    return im.convert('RGBA')

def halftone(size, color, spacing=9, angle=45, gradient='v', lo=0.0, hi=1.0, bg=None):
    """Comic dot screen: dots grow along a gradient. Returns RGBA."""
    w, h = size; img = Image.new('RGBA', size, bg or (0, 0, 0, 0)); d = ImageDraw.Draw(img)
    ca, sa = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    R = int(math.hypot(w, h) / spacing) + 2
    for i in range(-R, R):
        for j in range(-R, R):
            x = (i * ca - j * sa) * spacing + w / 2; y = (i * sa + j * ca) * spacing + h / 2
            if not (-spacing < x < w + spacing and -spacing < y < h + spacing): continue
            t = {'v': y / h, 'h': x / w, 'r': math.hypot(x - w / 2, y - h / 2) / (math.hypot(w, h) / 2), 'flat': 1.0}[gradient]
            t = lo + (hi - lo) * max(0, min(1, t)); r = spacing * 0.62 * math.sqrt(t)
            if r > 0.6: d.ellipse((x - r, y - r, x + r, y + r), fill=color)
    return img

def speed_lines(size, center, color=(255, 255, 255, 255), n=90, inner=0.18, seed=3):
    rnd = random.Random(seed); w, h = size; img = Image.new('RGBA', size, (0, 0, 0, 0)); d = ImageDraw.Draw(img)
    cx, cy = center; far = math.hypot(w, h)
    for _ in range(n):
        a = rnd.uniform(0, 2 * math.pi); wid = rnd.uniform(0.004, 0.02); r0 = far * rnd.uniform(inner, inner + 0.25)
        d.polygon([(cx + math.cos(a) * r0, cy + math.sin(a) * r0), (cx + math.cos(a - wid) * far, cy + math.sin(a - wid) * far), (cx + math.cos(a + wid) * far, cy + math.sin(a + wid) * far)], fill=color)
    return img

def skew_mask(size, skew, pad=0):
    """Parallelogram mask (top edge shifted right by `skew`)."""
    w, h = size; m = Image.new('L', size, 0)
    ImageDraw.Draw(m).polygon([(skew + pad, pad), (w - pad, pad), (w - skew - pad, h - pad), (pad, h - pad)], fill=255)
    return m

def paste_panel(canvas, img, pos, skew=22, border=7, shadow=(10, 10), angle=0.0, shadow_col=INK):
    """Pastes `img` as a comic panel: skewed, thick black border, offset hard shadow."""
    w, h = img.size; m = skew_mask((w, h), skew)
    border_l = Image.new('RGBA', (w + 2 * border, h + 2 * border), (0, 0, 0, 0))
    edge = m.filter(ImageFilter.MaxFilter(2 * border + 1)); big = Image.new('L', (w + 2 * border, h + 2 * border), 0); big.paste(edge, (border, border))
    big = big.filter(ImageFilter.MaxFilter(3)) if False else big
    layer = Image.new('RGBA', big.size, INK + (255,)); layer.putalpha(big)
    pic = Image.new('RGBA', big.size, (0, 0, 0, 0)); pic.paste(img, (border, border), m)
    layer.alpha_composite(pic)
    sh = Image.new('RGBA', big.size, shadow_col + (255,)); sh.putalpha(big)
    if angle: layer = layer.rotate(angle, expand=True, resample=Image.BICUBIC); sh = sh.rotate(angle, expand=True, resample=Image.BICUBIC)
    canvas.alpha_composite(sh, (pos[0] + shadow[0] - border, pos[1] + shadow[1] - border))
    canvas.alpha_composite(layer, (pos[0] - border, pos[1] - border))

def text_outlined(d, xy, s, f, fill, outline=INK, w=3, anchor='la'):
    x, y = xy
    for dx in range(-w, w + 1):
        for dy in range(-w, w + 1):
            if dx * dx + dy * dy <= w * w + 1: d.text((x + dx, y + dy), s, font=f, fill=outline, anchor=anchor)
    d.text(xy, s, font=f, fill=fill, anchor=anchor)

def bubble(canvas, box, text_lines, tail=(0, 0), font_=None, fill=PAPER):
    d = ImageDraw.Draw(canvas); x0, y0, x1, y1 = box; f = font_ or CMB(20)
    d.rounded_rectangle((x0 + 5, y0 + 5, x1 + 5, y1 + 5), 26, fill=INK)
    tx, ty = tail
    d.polygon([(x0 + 40, y1 - 4), (x0 + 90, y1 - 4), (tx, ty)], fill=INK)
    d.rounded_rectangle(box, 26, fill=fill, outline=INK, width=4)
    d.polygon([(x0 + 48, y1 - 3), (x0 + 82, y1 - 3), (tx + 2, ty - 2)], fill=fill)
    for i, ln in enumerate(text_lines): d.text((x0 + 20, y0 + 14 + i * 26), ln, font=f, fill=INK)

def background(kind='navy'):
    base = Image.new('RGBA', (W, H), NAVY + (255,))
    base.alpha_composite(halftone((W, H), (30, 44, 96, 255), spacing=14, angle=45, gradient='r', lo=0.15, hi=0.95))
    base.alpha_composite(speed_lines((W, H), (W * 0.5, H * 0.45), (255, 255, 255, 14), n=70, inner=0.12))
    # night skyline silhouette (like the game's story select)
    d = ImageDraw.Draw(base); rnd = random.Random(11); x = 0
    while x < W:
        bw = rnd.randint(28, 70); bh = rnd.randint(30, 120); d.rectangle((x, H - bh, x + bw, H), fill=(8, 10, 24, 255))
        for yy in range(H - bh + 8, H - 6, 14):
            for xx in range(x + 5, x + bw - 6, 11):
                if rnd.random() < 0.16: d.rectangle((xx, yy, xx + 4, yy + 6), fill=(255, 214, 90, 255))
        x += bw + rnd.randint(0, 6)
    return base

def header(canvas, title, sub=None):
    d = ImageDraw.Draw(canvas)
    # red slash banner with the title, like the in-game "STORY SELECT" tab
    d.polygon([(30, 22), (620, 22), (580, 96), (30, 96)], fill=INK); d.polygon([(26, 18), (610, 18), (570, 90), (26, 90)], fill=RED)
    text_outlined(d, (52, 24), title, IMP(54), PAPER, INK, 3)
    if sub: d.text((640, 60), sub, font=CMB(22), fill=PAPER, anchor='lm')
    # GOLGO 13 logo corner (stylised text; the real logo is cut from the game's own title screen at runtime)
    text_outlined(d, (W - 40, 20), '13', IMP(76), YEL, INK, 4, 'ra')
    text_outlined(d, (W - 130, 26), 'GOLGO', IMP(44), PAPER, INK, 3, 'ra')

def hints(canvas, items):
    d = ImageDraw.Draw(canvas); d.rectangle((0, H - 44, W, H), fill=INK); x = 24
    for key, lab in items:
        w = d.textlength(key, font=IMP(20)) + 20; d.rounded_rectangle((x, H - 36, x + w, H - 8), 6, fill=YEL); d.text((x + 10, H - 22), key, font=IMP(20), fill=INK, anchor='lm')
        d.text((x + w + 8, H - 22), lab, font=CMB(18), fill=PAPER, anchor='lm'); x += w + 12 + d.textlength(lab, font=CMB(18)) + 26

# ---------------------------------------------------------------------------------------------------------------- 1. game select
GAMES = [
    dict(t='GOLGO 13', sub='Premier mission', year='2000', board='NAMCO SYSTEM 12', tint=((40, 0, 8), (255, 214, 190)), accent=RED, page=696, box=(0, 0, 290, 215), st='ROMS OK', ok=True),
    dict(t='LE MIRACLE', sub='Kiseki no Dandou', year='2001', board='NAMCO SYSTEM 12', tint=((0, 24, 28), (200, 255, 240)), accent=(20, 160, 140), page=776, box=(35, 128, 300, 245), st='ROMS OK', ok=True),
    dict(t='REQUIEM', sub='Juusei no Requiem', year='2001', board='NAMCO SYSTEM 10', tint=((6, 10, 40), (255, 226, 160)), accent=(230, 140, 20), page=156, box=(30, 290, 370, 530), st='ROMS MANQUANTES', ok=False),
]

def select_screen(sel=1):
    c = background(); header(c, 'CHOISIS TA MISSION', 'Un jeu, une cible, un seul tir.')
    d = ImageDraw.Draw(c)
    cw, ch = 360, 470; xs = [60, 460, 860]; y0 = 130
    for i, g in enumerate(GAMES):
        focus = i == sel; scale = 1.0; x = xs[i]; y = y0 - (14 if focus else 0)
        pic = panel_crop(g['page'], g['box'], (cw, 300), g['tint'])
        if not g['ok']: pic = ImageOps.grayscale(pic.convert('RGB')).convert('RGBA'); pic = Image.blend(pic, Image.new('RGBA', pic.size, (20, 20, 30, 255)), 0.35)
        card = Image.new('RGBA', (cw, ch), PAPER + (255,))
        card.alpha_composite(halftone((cw, ch), g['accent'] + (255,), spacing=10, angle=30, gradient='v', lo=0.0, hi=0.55))
        card.alpha_composite(pic, (0, 0))
        cd = ImageDraw.Draw(card)
        cd.rectangle((0, 300, cw, 306), fill=INK); cd.rectangle((0, 306, cw, 436), fill=PAPER)
        cd.polygon([(0, 436), (cw, 420), (cw, ch), (0, ch)], fill=g['accent'])
        text_outlined(cd, (18, 316), g['t'], IMP(46), PAPER, INK, 3)
        cd.text((20, 372), g['sub'], font=CMB(20), fill=INK)
        cd.text((20, 402), g['board'] + '  -  ' + g['year'], font=CMB(16), fill=(60, 60, 70))
        paste_panel(c, card, (x, y), skew=26, border=7, shadow=(12, 12) if focus else (8, 8), angle=(-1.2 if i == 0 else 1.2 if i == 2 else 0.0))
        d = ImageDraw.Draw(c)
        # stamp
        stamp = Image.new('RGBA', (250, 56), (0, 0, 0, 0)); sd = ImageDraw.Draw(stamp)
        col = (20, 140, 60) if g['ok'] else RED
        sd.rounded_rectangle((2, 2, 247, 53), 8, outline=col + (255,), width=5); sd.text((125, 28), g['st'], font=IMP(30), fill=col + (255,), anchor='mm')
        stamp = stamp.rotate(8 if i != 1 else -6, expand=True, resample=Image.BICUBIC); c.alpha_composite(stamp, (x + 70, y + 250))
    # focus decoration: burst + "JOUER" bubble
    fx = xs[sel] + cw // 2
    bubble(c, (fx - 150, 616, fx + 150, 662), ['  Entrée / A : JOUER'], tail=(fx - 40, 600), font_=IMP(22))
    d = ImageDraw.Draw(c)
    # options gear tab, language chip
    d.polygon([(W - 330, 112), (W - 36, 112), (W - 36, 150), (W - 346, 150)], fill=INK)
    d.text((W - 190, 131), 'F1  OPTIONS     F4  FR', font=IMP(22), fill=YEL, anchor='mm')
    hints(c, [('<  >', 'choisir'), ('Entrée', 'jouer'), ('F1', 'options'), ('Échap', 'quitter')])
    return c.convert('RGB')

# ---------------------------------------------------------------------------------------------------------------- 2. options
TABS = ['IMAGE', 'SON', 'COMMANDES', 'WIIMOTE', 'LANGUE', 'JEU']

def slider(d, x, y, w, v, col=YEL):
    d.rounded_rectangle((x, y - 7, x + w, y + 7), 7, fill=INK); d.rounded_rectangle((x + 3, y - 4, x + 3 + int((w - 6) * v), y + 4), 4, fill=col)
    kx = x + int(w * v); d.ellipse((kx - 13, y - 13, kx + 13, y + 13), fill=PAPER, outline=INK, width=4)

def check(d, x, y, on):
    d.rounded_rectangle((x, y, x + 30, y + 30), 6, fill=PAPER, outline=INK, width=4)
    if on: d.line((x + 6, y + 16, x + 13, y + 24, x + 25, y + 6), fill=RED, width=6)

def options_screen(tab=0):
    c = background(); header(c, 'OPTIONS', 'Réglages du fusil… et du reste.')
    d = ImageDraw.Draw(c)
    # tab rail : stacked slanted panels
    for i, t in enumerate(TABS):
        on = i == tab; x = 34 + (22 if on else 0); y = 122 + i * 78
        d.polygon([(x + 8, y + 8), (x + 262, y + 8), (x + 238, y + 66), (x - 16 + 8, y + 66)], fill=INK)
        d.polygon([(x, y), (x + 254, y), (x + 230, y + 58), (x - 24, y + 58)], fill=YEL if on else PAPER, outline=INK)
        d.line([(x, y), (x + 254, y), (x + 230, y + 58), (x - 24, y + 58), (x, y)], fill=INK, width=4)
        d.text((x + 118, y + 30), t, font=IMP(30), fill=RED if on else INK, anchor='mm')
    # main sheet
    sheet = Image.new('RGBA', (840, 500), PAPER + (255,)); sheet.alpha_composite(halftone((840, 500), (190, 175, 140, 255), spacing=12, gradient='h', lo=0.0, hi=0.35))
    paste_panel(c, sheet, (360, 124), skew=18, border=7, shadow=(12, 12)); d = ImageDraw.Draw(c)
    rows = [('Plein écran', 'check', True), ('Mise à l\'échelle entière', 'check', False), ('Format', 'choice', '4:3 (original)'), ('Filtre', 'choice', 'Lissé (net)'),
            ('Lignes de balayage (CRT)', 'slider', 0.35), ('Afficher les images / seconde', 'check', False), ('Synchronisation verticale', 'check', True)]
    y = 150
    for i, (lab, kind, val) in enumerate(rows):
        foc = i == 4
        if foc: d.polygon([(392, y - 6), (1170, y - 6), (1158, y + 44), (380, y + 44)], fill=YEL, outline=INK)
        d.text((410, y + 19), lab, font=CMB(24), fill=INK, anchor='lm')
        if kind == 'check': check(d, 1090, y + 4, val)
        elif kind == 'slider': slider(d, 840, y + 19, 230, val, RED)
        else:
            d.text((1130, y + 19), val, font=IMP(24), fill=RED, anchor='rm'); d.text((1148, y + 19), '>', font=IMP(24), fill=INK, anchor='rm'); d.text((1000 - 0, y + 19), '', font=IMP(1), fill=INK)
        y += 56
    bubble(c, (400, 548, 1130, 602), ['Intensité des lignes du tube cathodique (0 = aucune).'], tail=(430, 628), font_=CMB(22))
    hints(c, [('Haut / Bas', 'choisir'), ('Gauche / Droite', 'changer'), ('L1 / R1', 'onglet'), ('Échap', 'retour')])
    return c.convert('RGB')

def controls_screen():
    c = background(); header(c, 'COMMANDES', 'Souris, clavier, manette ou Wiimote.')
    d = ImageDraw.Draw(c)
    sheet = Image.new('RGBA', (1180, 500), PAPER + (255,)); sheet.alpha_composite(halftone((1180, 500), (190, 175, 140, 255), spacing=12, gradient='h', lo=0.0, hi=0.3))
    paste_panel(c, sheet, (50, 124), skew=18, border=7, shadow=(12, 12)); d = ImageDraw.Draw(c)
    # left : keyboard map
    d.text((84, 152), 'ACTION', font=IMP(24), fill=RED); d.text((420, 152), 'CLAVIER', font=IMP(24), fill=RED); d.text((620, 152), 'MANETTE', font=IMP(24), fill=RED)
    rows = [('Tir (gâchette)', 'Clic gauche', 'RT / A'), ('Lunette (sensor)', 'Clic droit', 'LT'), ('Valider', 'Entrée', 'A'), ('Haut / Bas', 'Flèches', 'Croix'), ('Start', 'Espace', 'Start'), ('Pièce', 'C', 'Select')]
    y = 192
    for i, (a, k, p) in enumerate(rows):
        foc = i == 0
        if foc: d.polygon([(76, y - 4), (790, y - 4), (780, y + 38), (66, y + 38)], fill=YEL, outline=INK)
        d.text((84, y + 17), a, font=CMB(22), fill=INK, anchor='lm')
        for xx, v in ((420, k), (620, p)):
            w = int(d.textlength(v, font=IMP(20))) + 24; d.rounded_rectangle((xx, y + 2, xx + w, y + 32), 7, fill=PAPER, outline=INK, width=3); d.text((xx + w // 2, y + 18), v, font=IMP(20), fill=INK, anchor='mm')
        y += 50
    d.text((84, y + 12), '[ Réinitialiser les touches ]', font=IMP(22), fill=RED)
    # right : Wiimote card
    card = Image.new('RGBA', (340, 420), (255, 255, 255, 255)); card.alpha_composite(halftone((340, 420), (210, 40, 50, 255), spacing=11, gradient='v', lo=0.1, hi=0.6))
    paste_panel(c, card, (850, 160), skew=0, border=6, shadow=(9, 9), angle=1.6); d = ImageDraw.Draw(c)
    d.rectangle((868, 252, 1172, 412), fill=PAPER, outline=INK, width=3)
    text_outlined(d, (1020, 190), 'WIIMOTE', IMP(40), PAPER, INK, 3, 'ma')
    lines = ['État : appairée', 'Barre : détectée', 'Lissage :  ||||||....', 'Zone morte : 2 %']
    for i, ln in enumerate(lines): d.text((880, 262 + i * 40), ln, font=CMB(22), fill=INK)
    for i, lab in enumerate(['CALIBRER', 'APPAIRER']):
        d.polygon([(872, 430 + i * 66), (1166, 430 + i * 66), (1152, 480 + i * 66), (858, 480 + i * 66)], fill=INK)
        d.polygon([(866, 424 + i * 66), (1160, 424 + i * 66), (1146, 474 + i * 66), (852, 474 + i * 66)], fill=YEL if i == 0 else PAPER, outline=INK)
        d.text((1006, 450 + i * 66), lab, font=IMP(28), fill=INK, anchor='mm')
    hints(c, [('Entrée', 'changer une touche'), ('Suppr', 'effacer'), ('Échap', 'retour')])
    return c.convert('RGB')

def pause_screen():
    # the paused game, darkened, with the menu over it
    c = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    g = Image.open('work/g13jnr/w02600.ppm').convert('RGBA') if os.path.exists('work/g13jnr/w02600.ppm') else Image.new('RGBA', (512, 480), (40, 60, 40, 255))
    g = ImageOps.fit(g, (W, H)); c.alpha_composite(g); c.alpha_composite(Image.new('RGBA', (W, H), (0, 0, 20, 170)))
    c.alpha_composite(speed_lines((W, H), (W * 0.5, H * 0.5), (255, 255, 255, 20), n=80, inner=0.25))
    d = ImageDraw.Draw(c)
    box = Image.new('RGBA', (520, 440), PAPER + (255,)); box.alpha_composite(halftone((520, 440), (190, 175, 140, 255), spacing=12, gradient='v', lo=0.0, hi=0.4))
    paste_panel(c, box, (380, 150), skew=20, border=8, shadow=(14, 14)); d = ImageDraw.Draw(c)
    d.polygon([(380, 150), (900, 150), (884, 232), (364, 232)], fill=RED); d.line([(380, 150), (900, 150), (884, 232), (364, 232), (380, 150)], fill=INK, width=5)
    text_outlined(d, (640, 162), 'PAUSE', IMP(54), PAPER, INK, 3, 'ma')
    items = ['REPRENDRE', 'OPTIONS', 'CHANGER DE JEU', 'RÉINITIALISER', 'QUITTER']
    for i, t in enumerate(items):
        y = 262 + i * 60; foc = i == 0
        if foc: d.polygon([(430, y), (860, y), (846, y + 50), (416, y + 50)], fill=YEL, outline=INK); d.line([(430, y), (860, y), (846, y + 50), (416, y + 50), (430, y)], fill=INK, width=4)
        d.text((640, y + 27), t, font=IMP(32), fill=RED if t == 'QUITTER' else INK, anchor='mm')
    return c.convert('RGB')

if __name__ == '__main__':
    select_screen(1).save(os.path.join(OUT, '1_selection.png'))
    options_screen(0).save(os.path.join(OUT, '2_options_image.png'))
    controls_screen().save(os.path.join(OUT, '3_commandes_wiimote.png'))
    pause_screen().save(os.path.join(OUT, '4_pause.png'))
    print('ok ->', OUT)
