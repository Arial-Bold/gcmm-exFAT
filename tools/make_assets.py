#!/usr/bin/env python3
"""Makes source/ui_assets.c and .h: the text font (M PLUS Rounded 1c, OFL,
see tools/fonts/OFL.txt) as two texture atlases, the GCMM exFAT logo (the card is the logo) at two sizes,
and the memory card and SD card pictures, all in the GameCube's own texture
formats. Run it again after changing anything here:

    python3 tools/make_assets.py

Needs Python 3 with Pillow and NumPy. The logo and the cards are drawn here
from scratch (no outside artwork)."""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT = os.path.join(HERE, 'fonts', 'MPLUSRounded1c-Bold-latin.ttf')
FONT_X = os.path.join(HERE, 'fonts', 'MPLUSRounded1c-ExtraBold-latin.ttf')

GOLD = (232, 207, 74); GOLD_HI = (255, 244, 150); INDIGO_D = (42, 34, 140)

# ------------------------------------------------------------ texture formats

def tile_i8(a):
    """8-bit intensity, 8x4 tiles"""
    h, w = a.shape
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 8):
            out += a[ty:ty + 4, tx:tx + 8].astype(np.uint8).tobytes()
    return bytes(out)

def tile_rgba8(img):
    """32-bit colour, 4x4 tiles: 32 bytes of alpha+red, then 32 of green+blue"""
    a = np.array(img.convert('RGBA'))
    h, w = a.shape[:2]
    out = bytearray()
    for ty in range(0, h, 4):
        for tx in range(0, w, 4):
            t = a[ty:ty + 4, tx:tx + 4]
            ar = np.stack([t[..., 3], t[..., 0]], -1).reshape(-1)
            gb = np.stack([t[..., 1], t[..., 2]], -1).reshape(-1)
            out += ar.astype(np.uint8).tobytes() + gb.astype(np.uint8).tobytes()
    return bytes(out)

def pad_to(img, mw, mh):
    w = (img.width + mw - 1) // mw * mw
    h = (img.height + mh - 1) // mh * mh
    c = Image.new('RGBA', (w, h), (0, 0, 0, 0)); c.alpha_composite(img.convert('RGBA'))
    return c

# --------------------------------------------------------------------- font

def glyph_chars():
    """byte value -> unicode character (ASCII, Latin-1, and the ellipsis at 0x85 as in Windows-1252)"""
    m = {c: chr(c) for c in range(32, 127)}
    m.update({c: chr(c) for c in range(160, 256)})
    m[0x85] = '…'
    m[0x95] = '·'   # (spare: a middle dot)
    return m

def make_atlas(px, width):
    f = ImageFont.truetype(FONT, px)
    ascent, descent = f.getmetrics()
    chars = glyph_chars()
    pad = 2
    x = y = 0; row_h = 0
    places = {}
    for code, ch in sorted(chars.items()):
        l, t, r, b = f.getbbox(ch)
        gw, gh = max(1, r - l), max(1, b - t)
        if x + gw + pad > width: x = 0; y += row_h + pad; row_h = 0
        places[code] = (x, y, l, t, gw, gh, f.getlength(ch))
        x += gw + pad; row_h = max(row_h, gh)
    height = (y + row_h + pad + 3) // 4 * 4
    canvas = Image.new('L', (width, height), 0)
    d = ImageDraw.Draw(canvas)
    table = []
    for code in range(256):
        if code not in places:
            table.append((0, 0, 0, 0, 0, 0, 0)); continue
        x, y, l, t, gw, gh, adv = places[code]
        d.text((x - l, y - t), chars[code], font=f, fill=255)
        # offsets from the pen: left, and top relative to the baseline
        table.append((x, y, gw, gh, l, t - ascent, int(round(adv * 16))))
    return canvas, table, ascent, descent

# --------------------------------------------------------------- the pictures

def vgrad(w, h, top, bot):
    t = np.linspace(0, 1, h)[:, None, None]
    a = np.array(tuple(top) + (255,), float); b = np.array(tuple(bot) + (255,), float)
    return Image.fromarray((a * (1 - t) + b * t).repeat(w, 1).astype(np.uint8), 'RGBA')

def card(w, h, body_top, body_bot, contacts=GOLD):
    """a generic memory card: a slab with one clipped corner, a label and contacts"""
    m = Image.new('L', (w, h), 0); d = ImageDraw.Draw(m)
    r = w // 10; c = w // 4
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=r, fill=255)
    d.polygon([(w - c, 0), (w, 0), (w, c)], fill=0)
    body = vgrad(w, h, body_top, body_bot); body.putalpha(m)
    lab = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(lab).rounded_rectangle([w * 0.14, h * 0.16, w * 0.86 - c * 0.5, h * 0.62], radius=max(1, r // 2), fill=(255, 255, 255, 52))
    lab.putalpha(Image.fromarray(np.minimum(np.array(lab.split()[3]), np.array(m))))
    body.alpha_composite(lab)
    dd = ImageDraw.Draw(body)
    n = 7
    for i in range(n):
        x = w * 0.16 + i * (w * 0.68 / n)
        dd.rectangle([x, h * 0.74, x + w * 0.68 / n * 0.6, h * 0.92], fill=contacts + (255,))
    return body

def memory_card(w, h, top, bot):
    """a GameCube-style memory card, kept simple so it reads at any size:
    nearly square with rounded corners, the little triangle at the top, a
    big plain label and one bar along the bottom. Drawn big and scaled down;
    it sits in a w x h picture (centred, as wide as it is) so it lines up
    with the SD card picture."""
    S = 4
    W = w * S; H = round(W * 1.06)
    body = vgrad(W, H, top, bot)
    m = Image.new('L', (W, H), 0); ImageDraw.Draw(m).rounded_rectangle([0, 0, W - 1, H - 1], radius=W // 10, fill=255)
    body.putalpha(m)
    d = ImageDraw.Draw(body)
    ridge = round(H * 0.12); tw = W * 0.06
    d.polygon([(W / 2 - tw, ridge * 0.72), (W / 2 + tw, ridge * 0.72), (W / 2, ridge * 0.3)], fill=(255, 255, 255, 190))
    d.rounded_rectangle([W * 0.1, H * 0.18, W * 0.9, H * 0.68], radius=W // 20, fill=(240, 243, 252, 245))
    d.rounded_rectangle([W * 0.16, H * 0.79, W * 0.84, H * 0.88], radius=S * 4, fill=(255, 255, 255, 60))
    small = body.resize((w, round(H / S)), Image.LANCZOS)
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    out.alpha_composite(small, (0, (h - small.height) // 2))
    return out

def text_img(s, font, size, sp=0):
    f = ImageFont.truetype(font, size)
    tmp = ImageDraw.Draw(Image.new('L', (1, 1)))
    w = int(sum(tmp.textlength(ch, font=f) for ch in s) + sp * (len(s) - 1)) + 4
    im = Image.new('RGBA', (w, int(size * 1.5)), (0, 0, 0, 0)); d = ImageDraw.Draw(im); x = 2
    for ch in s:
        d.text((x, size * 0.15), ch, font=f, fill=(255, 255, 255, 255)); x += tmp.textlength(ch, font=f) + sp
    return im.crop(im.getbbox())

def tinted(mask_img, top, bot):
    g = vgrad(mask_img.width, mask_img.height, top, bot); g.putalpha(mask_img.split()[3]); return g

def shadowed(im, r, off, a=150):
    pad = 2 * r + max(abs(off[0]), abs(off[1]))
    c = Image.new('RGBA', (im.width + 2 * pad, im.height + 2 * pad), (0, 0, 0, 0))
    sh = Image.new('RGBA', im.size, (0, 0, 0, 0))
    sh.putalpha(im.split()[3].point(lambda v: v * a // 255))
    c.alpha_composite(sh, (pad + off[0], pad + off[1]))
    c = c.filter(ImageFilter.GaussianBlur(r))
    c.alpha_composite(im, (pad, pad))
    return c

def badge(label, h):
    """the gold exFAT badge: a rounded gold block with the word cut in dark"""
    f = ImageFont.truetype(FONT_X, int(h * 0.62))
    tmp = ImageDraw.Draw(Image.new('L', (1, 1)))
    tw = tmp.textlength(label, font=f)
    w = int(tw + h * 0.7)
    b = vgrad(w, h, (255, 222, 110), (214, 168, 40))
    m = Image.new('L', (w, h), 0); ImageDraw.Draw(m).rounded_rectangle([0, 0, w - 1, h - 1], radius=h // 5, fill=255)
    b.putalpha(m)
    d = ImageDraw.Draw(b)
    d.text((w / 2, h / 2), label, font=f, fill=(40, 30, 8, 255), anchor='mm')
    return b

def spaced_to(s, font, target, track, top, bot):
    """a line of text sized (with light letterspacing) to be exactly `target` wide"""
    size = 60
    for _ in range(5):
        size = size * target / text_img(s, font, size, size * track).width
    im = text_img(s, font, int(size), int(size) * track)
    return tinted(im.resize((target, max(1, round(im.height * target / im.width))), Image.LANCZOS), top, bot)

def logo(tagline=True, shadow=True):
    """the card is the logo: a wide indigo memory card with GCMM on its label,
    gold contacts and exFAT along the bottom edge, MEMORY MANAGER under it
    (the splash screen's). The header's has fewer contacts and a bigger exFAT."""
    word = tinted(text_img('GCMM', FONT_X, 190, sp=8), (255, 255, 255), (225, 230, 245))
    cut = 110
    CW = word.width + 200
    row = 96 if tagline else 130
    lx0, ly0, lx1 = 60, 46, CW - 60 - cut // 2
    ly1 = ly0 + word.height + 70
    CH = ly1 + 40 + row + 44
    body = vgrad(CW, CH, (110, 96, 236), (44, 34, 150))
    m = Image.new('L', (CW, CH), 0); d = ImageDraw.Draw(m)
    d.rounded_rectangle([0, 0, CW - 1, CH - 1], radius=40, fill=255)
    d.polygon([(CW - cut, 0), (CW, 0), (CW, cut)], fill=0)      # the clipped corner
    body.putalpha(m)
    lab = Image.new('RGBA', (CW, CH), (0, 0, 0, 0))
    ImageDraw.Draw(lab).rounded_rectangle([lx0, ly0, lx1, ly1], radius=22, fill=(255, 255, 255, 40))
    body.alpha_composite(lab)
    body.alpha_composite(word, ((lx0 + lx1) // 2 - word.width // 2, (ly0 + ly1) // 2 - word.height // 2))
    dd = ImageDraw.Draw(body)
    cy0, cy1 = ly1 + 40, CH - 44
    for i in range(7 if tagline else 5):
        x = 60 + i * 52
        dd.rounded_rectangle([x, cy0, x + 32, cy1], radius=5, fill=(255, 226, 120, 255))
    dd.text((CW - 64, (cy0 + cy1) / 2), 'exFAT', font=ImageFont.truetype(FONT_X, int((cy1 - cy0) * 1.05)), fill=(255, 226, 120, 255), anchor='rm')
    out = shadowed(body, 10 if tagline else 6, (0, 8 if tagline else 4), 150) if shadow else body
    if not tagline:
        return out.crop(out.getbbox())
    mm = spaced_to('MEMORY MANAGER', FONT, CW - 40, 0.3, (200, 210, 230), (150, 160, 185))
    c = Image.new('RGBA', (out.width, out.height + 20 + mm.height), (0, 0, 0, 0))
    c.alpha_composite(out, (0, 0))
    c.alpha_composite(mm, (out.width // 2 - mm.width // 2, out.height + 14))
    return c.crop(c.getbbox())

def cheese_card(w, h):
    """the easter egg's virtual card: the simple memory card, made of Swiss
    cheese (holes in it, a rind-coloured label edge). Drawn big, scaled down,
    centred in a w x h picture like memory_card()."""
    S = 4
    W = w * S; H = round(W * 1.06)
    body = vgrad(W, H, (255, 220, 110), (232, 170, 50))
    m = Image.new('L', (W, H), 0); ImageDraw.Draw(m).rounded_rectangle([0, 0, W - 1, H - 1], radius=W // 10, fill=255)
    body.putalpha(m)
    d = ImageDraw.Draw(body)
    for (x, y, r) in [(0.22, 0.30, 0.10), (0.62, 0.22, 0.07), (0.78, 0.55, 0.11), (0.35, 0.62, 0.13), (0.15, 0.85, 0.06),
                      (0.58, 0.83, 0.08), (0.88, 0.88, 0.05), (0.48, 0.42, 0.05)]:
        rr = r * W; cx, cy = x * W, y * H
        d.ellipse([cx - rr, cy - rr + rr * 0.18, cx + rr, cy + rr + rr * 0.18], fill=(170, 105, 15, 255))
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=(196, 128, 24, 255))
    d.rounded_rectangle([W * 0.1, H * 0.18, W * 0.9, H * 0.66], radius=W // 20, outline=(255, 245, 200, 200), width=S * 2)
    ridge = round(H * 0.12); tw = W * 0.06
    d.polygon([(W / 2 - tw, ridge * 0.72), (W / 2 + tw, ridge * 0.72), (W / 2, ridge * 0.3)], fill=(255, 255, 255, 170))
    small = body.resize((w, round(H / S)), Image.LANCZOS)
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    out.alpha_composite(small, (0, (h - small.height) // 2))
    return out

def sized(img, w=None, h=None):
    if w is None: w = round(img.width * h / img.height)
    if h is None: h = round(img.height * w / img.width)
    return img.resize((w, h), Image.LANCZOS)

# ----------------------------------------------------------------- output

def c_array(name, data):
    lines = [f'const u8 {name}[{len(data)}] __attribute__((aligned(32))) = {{']
    for i in range(0, len(data), 24):
        lines.append('\t' + ','.join(str(b) for b in data[i:i + 24]) + ',')
    lines.append('};\n')
    return '\n'.join(lines)

def main():
    c = ['/* ui_assets.c - made by tools/make_assets.py: do not edit by hand.',
         ' * The font is M PLUS Rounded 1c (SIL Open Font License, see tools/fonts/OFL.txt).',
         ' * The logo and the card pictures are drawn by the script itself. */',
         '#include "ui_assets.h"', '']
    h = ['/* ui_assets.h - made by tools/make_assets.py: do not edit by hand. */',
         '#ifndef UI_ASSETS_H', '#define UI_ASSETS_H', '#include <gctypes.h>', '',
         '/* one character of a font atlas: where it is, its size, where it sits',
         ' * against the pen (left, top from the baseline) and how far it moves the',
         ' * pen (in 1/16 pixels) */',
         'typedef struct { u16 x, y; u8 w, h; s8 left, top; u16 adv16; } UiGlyph;',
         'typedef struct { const u8 *tex; u16 w, h; u8 px, ascent, descent; const UiGlyph *glyphs; } UiFont;', '']
    for name, px, width in (('small', 14, 256), ('large', 24, 512)):
        atlas, table, asc, desc = make_atlas(px, width)
        c.append(c_array(f'font_{name}_tex', tile_i8(np.array(atlas))))
        c.append(f'static const UiGlyph font_{name}_glyphs[256] = {{')
        for (x, y, gw, gh, l, t, adv) in table:
            c.append(f'\t{{{x},{y},{gw},{gh},{l},{t},{adv}}},')
        c.append('};')
        c.append(f'const UiFont font_{name} = {{ font_{name}_tex, {atlas.width}, {atlas.height}, {px}, {asc}, {desc}, font_{name}_glyphs }};\n')
        h.append(f'extern const UiFont font_{name};   /* {px} px */')
    pics = [('logo_title', pad_to(sized(logo(False), h=44), 4, 4)),
            ('logo_splash', pad_to(sized(logo(True), h=216), 4, 4)),
            ('card_mc', pad_to(memory_card(64, 84, (122, 108, 240), (52, 42, 160)), 4, 4)),
            ('card_sd', pad_to(card(64, 84, (235, 238, 245), (170, 180, 195)), 4, 4)),
            ('card_file', pad_to(card(16, 20, (210, 214, 222), (150, 160, 175)), 4, 4)),
            ('card_cheese', pad_to(cheese_card(64, 84), 4, 4)),
            # the opening: the logo card on its own (it flips; its back is drawn
            # from its shape) and MEMORY MANAGER (it folds into the slot)
            ('intro_card', pad_to(sized(logo(False, shadow=False), w=328), 4, 4)),
            ('intro_words', pad_to(spaced_to('MEMORY MANAGER', FONT, 300, 0.3, (215, 222, 240), (170, 180, 205)), 4, 4))]
    h.append('')
    for name, img in pics:
        c.append(c_array(f'{name}_tex', tile_rgba8(img)))
        h.append(f'#define {name.upper()}_W {img.width}')
        h.append(f'#define {name.upper()}_H {img.height}')
        h.append(f'extern const u8 {name}_tex[];   /* RGBA8 */')
        img.save(os.path.join(HERE, f'preview_{name}.png'))
    h += ['', '#endif', '']
    open(os.path.join(ROOT, 'source', 'ui_assets.c'), 'w').write('\n'.join(c))
    open(os.path.join(ROOT, 'source', 'ui_assets.h'), 'w').write('\n'.join(h))
    print('wrote source/ui_assets.c and .h')

if __name__ == '__main__':
    main()
