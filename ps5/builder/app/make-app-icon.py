#!/usr/bin/env python3
# Luigi's Mansion for the PlayStation 5 - the builder app's icon (Windows .ico, Linux .png).
#
#   python3 make-app-icon.py OUT.ico|OUT.png
#
# The title art's ghost (ps5/art/make-title-art.py) on a night-blue tile. Needs Pillow.
#
# SPDX-License-Identifier: MIT
import sys
from PIL import Image, ImageDraw, ImageFilter

size = 256
tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))
mask = Image.new("L", (size, size), 0)
ImageDraw.Draw(mask).rounded_rectangle([8, 8, size - 8, size - 8], radius=48, fill=255)
sky = Image.new("RGBA", (size, size))
d = ImageDraw.Draw(sky)
for y in range(size):
    t = y / (size - 1)
    d.line([(0, y), (size, y)], fill=(int(12 + 40 * t), int(16 + 6 * t), int(52 + 40 * t), 255))
tile.paste(sky, (0, 0), mask)

ghost = Image.new("L", (size, size), 0)
g = ImageDraw.Draw(ghost)
cx, top, width = 128, 46, 150
bottom = top + width * 1.13
g.ellipse([cx - width / 2, top, cx + width / 2, top + width], fill=255)
g.rectangle([cx - width / 2, top + width / 2, cx + width / 2, bottom - width * 0.09], fill=255)
for i in range(4):
    x0 = cx - width / 2 + i * width / 4
    g.ellipse([x0, bottom - width * 0.2, x0 + width / 4, bottom], fill=255)
glow = ghost.filter(ImageFilter.GaussianBlur(10)).point(lambda v: int(v * 0.6))
tile.paste((120, 220, 170, 255), (0, 0), Image.composite(glow, Image.new("L", (size, size), 0), mask))
tile.paste((214, 248, 226, 255), (0, 0), ghost)
face = ImageDraw.Draw(tile)
for ex in (cx - 27, cx + 27):
    face.ellipse([ex - 10, top + 52, ex + 10, top + 80], fill=(30, 34, 60, 255))
face.ellipse([cx - 14, top + 92, cx + 14, top + 116], fill=(30, 34, 60, 255))

out = sys.argv[1]
if out.endswith(".ico"):
    tile.save(out, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
else:
    tile.save(out)
print(f"wrote {out}")
