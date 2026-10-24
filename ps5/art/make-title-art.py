#!/usr/bin/env python3
# Luigi's Mansion for the PS5 - the home screen's art, drawn here.
#
#   python3 make-title-art.py OUT_DIR
#
# Writes OUT_DIR/icon0.png (512x512, the icon), OUT_DIR/pic0.png (3840x2160, the home screen's
# background) and OUT_DIR/pic1.png (3840x2160, the splash while the title starts).
# ps5-lm/tools/build-lm.sh turns the two pictures into sce_sys/pic0.dds and pic1.dds with
# ps5/tools/png-to-title-dds.py.
#
# A cartoon ghost, a night sky and a mansion's silhouette, and the title's lettering: nothing in
# them is from the game. Players can put their own art in with an art-swapping app.
# Needs Pillow (python3-pil) and the DejaVu fonts (fonts-dejavu-core).
#
# SPDX-License-Identifier: MIT
import math
import pathlib
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

GHOST_BODY = (214, 248, 226)
GHOST_GLOW = (120, 220, 170)
FACE = (30, 34, 60)


def font(size_px):
    for name in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, size_px)
        except OSError:
            pass
    return ImageFont.load_default()


def sky(width, height):
    """Deep blue to violet, top to bottom."""
    image = Image.new("RGB", (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / (height - 1)
        draw.line([(0, y), (width, y)], fill=(int(12 + 40 * t), int(16 + 6 * t), int(52 + 40 * t)))
    return image


def stars(image, count, seed, max_y):
    rng = random.Random(seed)
    draw = ImageDraw.Draw(image)
    for _ in range(count):
        x, y = rng.uniform(0, image.width), rng.uniform(0, max_y)
        r = rng.choice((1, 1, 1, 2, 2, 3)) * image.width / 3840
        level = rng.randint(140, 255)
        draw.ellipse([x - r, y - r, x + r, y + r], fill=(level, level, min(255, level + 20)))


def moon(image, cx, cy, r):
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    halo = mask.filter(ImageFilter.GaussianBlur(r * 0.6))
    image.paste((150, 150, 190), (0, 0), halo.point(lambda v: int(v * 0.35)))
    image.paste((236, 232, 190), (0, 0), mask.filter(ImageFilter.GaussianBlur(max(1, r / 60))))


def ghost(image, cx, top, width, alpha=1.0, eyes=True):
    """A cartoon ghost: a round head, a wavy hem, a soft glow; alpha fades it."""
    height = width * 1.13
    bottom = top + height
    shape = Image.new("L", image.size, 0)
    g = ImageDraw.Draw(shape)
    g.ellipse([cx - width / 2, top, cx + width / 2, top + width], fill=255)
    g.rectangle([cx - width / 2, top + width / 2, cx + width / 2, bottom - width * 0.09], fill=255)
    waves = 4
    for i in range(waves):
        x0 = cx - width / 2 + i * width / waves
        g.ellipse([x0, bottom - width * 0.2, x0 + width / waves, bottom], fill=255)
    glow = shape.filter(ImageFilter.GaussianBlur(width * 0.08))
    image.paste(GHOST_GLOW, (0, 0), glow.point(lambda v: int(v * 0.55 * alpha)))
    image.paste(GHOST_BODY, (0, 0), shape.point(lambda v: int(v * alpha)))
    if eyes:
        face = Image.new("L", image.size, 0)
        f = ImageDraw.Draw(face)
        e = width * 0.07
        for ex in (cx - width * 0.18, cx + width * 0.18):
            f.ellipse([ex - e, top + width * 0.35, ex + e, top + width * 0.35 + e * 2.7], fill=255)
        m = width * 0.095
        f.ellipse([cx - m, top + width * 0.6, cx + m, top + width * 0.6 + m * 1.7], fill=255)
        image.paste(FACE, (0, 0), face.point(lambda v: int(v * alpha)))


def mansion(image, x, ground, scale):
    """A silhouette on a hill: a hall with gables, two towers, a chimney and lit windows."""
    s = scale
    dark = (14, 12, 30)
    d = ImageDraw.Draw(image)
    # The hill
    d.ellipse([x - 900 * s, ground - 120 * s, x + 900 * s, ground + 700 * s], fill=(18, 16, 38))
    # The main hall and its roof
    d.rectangle([x - 420 * s, ground - 420 * s, x + 420 * s, ground], fill=dark)
    d.polygon([(x - 470 * s, ground - 420 * s), (x, ground - 700 * s), (x + 470 * s, ground - 420 * s)], fill=dark)
    # Wings with their gables
    for side in (-1, 1):
        wx = x + side * 560 * s
        d.rectangle([wx - 180 * s, ground - 300 * s, wx + 180 * s, ground], fill=dark)
        d.polygon([(wx - 210 * s, ground - 300 * s), (wx, ground - 470 * s), (wx + 210 * s, ground - 300 * s)], fill=dark)
    # Towers with pointed roofs
    for tx, th in ((x - 300 * s, 820), (x + 330 * s, 720)):
        d.rectangle([tx - 70 * s, ground - th * s, tx + 70 * s, ground - 300 * s], fill=dark)
        d.polygon([(tx - 95 * s, ground - th * s), (tx, ground - (th + 230) * s), (tx + 95 * s, ground - th * s)], fill=dark)
    # A chimney
    d.rectangle([x + 140 * s, ground - 680 * s, x + 200 * s, ground - 560 * s], fill=dark)
    # Lit windows, a few dark
    rng = random.Random(7)
    lit = (255, 214, 120)
    glow = Image.new("L", image.size, 0)
    gd = ImageDraw.Draw(glow)
    windows = []
    for row in range(3):
        for col in range(-3, 4):
            windows.append((x + col * 110 * s, ground - (110 + row * 110) * s))
    for side in (-1, 1):
        for row in range(2):
            for col in (-1, 1):
                windows.append((x + side * 560 * s + col * 80 * s, ground - (100 + row * 100) * s))
    for tx, th in ((x - 300 * s, 820), (x + 330 * s, 720)):
        windows.append((tx, ground - (th - 90) * s))
    for wx, wy in windows:
        box = [wx - 26 * s, wy - 38 * s, wx + 26 * s, wy + 38 * s]
        if rng.random() < 0.72:
            d.rectangle(box, fill=lit)
            gd.rectangle(box, fill=255)
        else:
            d.rectangle(box, fill=(34, 30, 56))
    image.paste((255, 190, 90), (0, 0), glow.filter(ImageFilter.GaussianBlur(30 * s)).point(lambda v: int(v * 0.5)))
    # The door
    d.rectangle([x - 50 * s, ground - 150 * s, x + 50 * s, ground], fill=(40, 26, 20))
    d.ellipse([x - 50 * s, ground - 200 * s, x + 50 * s, ground - 100 * s], fill=(40, 26, 20))


def lettering(image, text, sub, cy, size, cx=None):
    """Text centred on cx (default the middle), its top at cy, with a shadow."""
    d = ImageDraw.Draw(image)
    f, f2 = font(size), font(int(size * 0.55))
    cx = image.width / 2 if cx is None else cx
    w = d.textlength(text, font=f)
    shadow = max(2, size // 22)
    d.text((cx - w / 2 + shadow, cy + shadow), text, font=f, fill=(0, 0, 0))
    d.text((cx - w / 2, cy), text, font=f, fill=(250, 236, 150))
    if sub:
        w2 = d.textlength(sub, font=f2)
        d.text((cx - w2 / 2, cy + size * 1.2), sub, font=f2, fill=(200, 210, 240))


def icon(out):
    image = sky(512, 512)
    moon(image, 410, 90, 50)
    ghost(image, 236, 120, 230)
    lettering(image, "LUIGI'S MANSION", "PS5", 414, 44)
    image.save(out / "icon0.png")


def background(width=3840, height=2160, title=False):
    image = sky(width, height)
    stars(image, 900, 3, height * 0.7)
    moon(image, 3150, 420, 190)
    # The mansion on the right; the home screen's text sits on the left
    mansion(image, 2700, 1880, 1.15)
    # A few ghosts drifting about, faint (the splash has its own in the left half)
    ghost(image, 3500, 1050, 180, alpha=0.4)
    if not title:
        ghost(image, 1950, 620, 260, alpha=0.55)
        ghost(image, 1500, 1150, 150, alpha=0.3)
    # Mist along the ground
    mist = Image.new("L", image.size, 0)
    ImageDraw.Draw(mist).rectangle([0, int(height * 0.86), width, height], fill=255)
    image.paste((70, 70, 110), (0, 0), mist.filter(ImageFilter.GaussianBlur(120)).point(lambda v: int(v * 0.6)))
    if title:
        # The left half, clear of the mansion: a ghost over the title
        ghost(image, 1000, 420, 560)
        lettering(image, "LUIGI'S MANSION", None, 1150, 190, cx=1000)
        ghost(image, 1750, 300, 170, alpha=0.4)
    return image


def main():
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    icon(out)
    background().save(out / "pic0.png")
    background(title=True).save(out / "pic1.png")
    for name in ("icon0.png", "pic0.png", "pic1.png"):
        print(f"wrote {out / name}")


if __name__ == "__main__":
    main()
