from PIL import Image, ImageDraw, ImageFont, ImageOps, ImageFilter
import math, sys
S = "Y:/Scan/Golgo 13/Golgo 13 - Le choix de l'auteur/1007.jpg"
W, H = 1600, 620
im = Image.open(S).convert('L')
k = im.width / 493.0
eyes = im.crop((int(18*k), int(32*k), int(450*k), int(118*k)))          # the eyes strip of the title page
eyes = ImageOps.autocontrast(eyes, cutoff=2)
eyes = ImageOps.fit(eyes, (W, 330), Image.LANCZOS, centering=(0.5, 0.5))
eyes = eyes.point(lambda v: 0 if v < 110 else (255 if v > 190 else int((v-110)*255/80)))
red = Image.new('RGB', (W, H), (14, 14, 22))
d = ImageDraw.Draw(red)
for y in range(0, H, 9):                                                 # halftone dots background
    for x in range(0, W, 9):
        r = 3.4 * (1 - abs(x - W/2) / (W/2)) ** 0.6
        if r > 0.5: d.ellipse((x-r, y-r, x+r, y+r), fill=(214, 28, 36))
band = Image.merge('RGB', (eyes.point(lambda v: 255 if v > 128 else 255), eyes, eyes))
band = ImageOps.colorize(eyes, black=(8, 8, 12), white=(246, 238, 218)).convert('RGB')
red.paste(band, (0, 0))
d.rectangle((0, 326, W, 334), fill=(12, 12, 16))
d.rectangle((0, 0, W-1, H-1), outline=(12, 12, 16), width=10)
def font(sz): return ImageFont.truetype('C:/Windows/Fonts/impact.ttf', sz)
def outlined(xy, text, f, fill, stroke=9, anchor='lm'):
    d.text((xy[0]+8, xy[1]+8), text, font=f, fill=(0, 0, 0), anchor=anchor, stroke_width=stroke, stroke_fill=(0, 0, 0))
    d.text(xy, text, font=f, fill=fill, anchor=anchor, stroke_width=stroke, stroke_fill=(12, 12, 16))
outlined((60, 475), 'GOLGO', font(190), (246, 238, 218))
tw = d.textlength('GOLGO', font=font(190))
outlined((60 + tw + 36, 470), '13', font(235), (255, 208, 32))
# crosshair on the "13"
cx, cy, R = int(60 + tw + 36 + 140), 478, 125
for w_, col in ((14, (12, 12, 16)), (6, (255, 255, 255))):
    d.ellipse((cx-R, cy-R, cx+R, cy+R), outline=col, width=w_)
    d.line((cx-R-60, cy, cx+R+60, cy), fill=col, width=w_); d.line((cx, cy-R-60, cx, cy+R+60), fill=col, width=w_)
d.ellipse((cx-8, cy-8, cx+8, cy+8), fill=(214, 28, 36))
f2 = font(44)
d.text((W-50, 440), 'NATIVE ARCADE PORT', font=f2, fill=(246, 238, 218), anchor='rm', stroke_width=5, stroke_fill=(12, 12, 16))
d.text((W-50, 494), 'STATIC RECOMPILATION  -  NO EMULATOR', font=font(30), fill=(255, 208, 32), anchor='rm', stroke_width=4, stroke_fill=(12, 12, 16))
d.text((W-50, 538), 'one game, one target, one shot', font=font(26), fill=(200, 200, 210), anchor='rm', stroke_width=3, stroke_fill=(12, 12, 16))
red.save('docs/img/logo.png')
red.resize((W//2, H//2), Image.LANCZOS).save('docs/img/logo_small.png')
