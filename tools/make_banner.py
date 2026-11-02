#!/usr/bin/env python3
"""Makes dist/opening.bnr: the 96x32 banner your Launcher shows for GCMM exFAT
(the card logo on the dark grid) and its name and description.

    python3 tools/make_banner.py

Needs Python 3 with Pillow and NumPy (and make_assets.py beside it)."""
import os, struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont
import make_assets as M

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SHORT_NAME = 'GCMM exFAT'
SHORT_MAKER = 'Arial-Bold'
LONG_NAME = 'GCMM exFAT'
LONG_MAKER = 'Arial-Bold'
DESCRIPTION = 'Back up and restore GameCube memory card saves on an SD card (FAT32 or exFAT).'

def banner_image():
    """96x32: the dark grid, the card logo at the left, MEMORY MANAGER beside it"""
    S = 8
    W, H = 96 * S, 32 * S
    img = Image.new('RGBA', (W, H), (6, 6, 8, 255))
    d = ImageDraw.Draw(img)
    for x in range(-S, W, 8 * S): d.rectangle([x, 0, x + S * 0.6, H], fill=(0x25, 0x25, 0x2b, 255))
    for y in range(-S, H, 8 * S): d.rectangle([0, y, W, y + S * 0.6], fill=(0x25, 0x25, 0x2b, 255))
    logo = M.logo(False)
    lh = H - 4 * S
    lg = logo.resize((round(logo.width * lh / logo.height), lh), Image.LANCZOS)
    img.alpha_composite(lg, (2 * S, 2 * S))
    x0 = 2 * S + lg.width + 4 * S
    room = W - x0 - 3 * S
    for i, word in enumerate(('MEMORY', 'MANAGER')):
        t = M.tinted(M.text_img(word, M.FONT_X, 120, sp=10), (220, 226, 240), (170, 178, 200))
        t = t.resize((room, round(t.height * room / t.width)), Image.LANCZOS) if t.width > room else t
        img.alpha_composite(t, (x0, round(H * (0.24 + i * 0.36))))
    return img.resize((96, 32), Image.LANCZOS)

def rgb5a3(img):
    """the GameCube's RGB5A3 texture format, in 4x4 tiles"""
    a = np.array(img.convert('RGBA'))
    out = bytearray()
    for ty in range(0, 32, 4):
        for tx in range(0, 96, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    r, g, b, al = (int(v) for v in a[y, x])
                    if al >= 224:
                        v = 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
                    else:
                        v = (al >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)
                    out += struct.pack('>H', v)
    return bytes(out)

def field(s, n):
    b = s.encode('ascii')[:n - 1]
    return b + b'\0' * (n - len(b))

def main():
    img = banner_image()
    data = b'BNR1' + b'\0' * 0x1C + rgb5a3(img)
    data += field(SHORT_NAME, 0x20) + field(SHORT_MAKER, 0x20) + field(LONG_NAME, 0x40) + field(LONG_MAKER, 0x40) + field(DESCRIPTION, 0x80)
    assert len(data) == 0x1960
    open(os.path.join(ROOT, 'dist', 'opening.bnr'), 'wb').write(data)
    img.save(os.path.join(HERE, 'preview_banner.png'))
    print('wrote dist/opening.bnr')

if __name__ == '__main__':
    main()
