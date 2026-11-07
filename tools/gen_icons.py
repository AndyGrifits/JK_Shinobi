#!/usr/bin/env python3
"""JK_Shinobi: the state signs drawn over a player's head (and under your own crosshair) - small
white pictograms on a coloured disc, outlined in black so they read on any background.

    python3 tools/gen_icons.py      -> assets/gfx/shinobi/st_*.tga (64x64, alpha)
"""
import math
import os

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "gfx", "shinobi")
S = 64
C = S / 2


def disc(col):
    im = Image.new("RGBA", (S * 4, S * 4), (0, 0, 0, 0))     # drawn 4x and scaled down (smooth edges)
    d = ImageDraw.Draw(im)
    d.ellipse((6, 6, S * 4 - 6, S * 4 - 6), fill=(0, 0, 0, 230))
    d.ellipse((18, 18, S * 4 - 18, S * 4 - 18), fill=col + (255,))
    return im, d


def done(im, name):
    im = im.resize((S, S), Image.LANCZOS)
    os.makedirs(OUT, exist_ok=True)
    im.save(os.path.join(OUT, name + ".tga"))


W = (255, 255, 255, 255)
K = (0, 0, 0, 255)


def poly(d, pts, fill=W, outline=K, width=10):
    pts = [(x * 4, y * 4) for x, y in pts]
    d.polygon(pts, fill=fill)
    d.line(pts + [pts[0]], fill=outline, width=width, joint="curve")


def prison():   # a drop of water
    im, d = disc((40, 110, 230))
    pts = [(C, 12)] + [(C + 13 * math.cos(math.radians(a)), 37 + 13 * math.sin(math.radians(a))) for a in range(-30, 211, 10)]
    poly(d, pts)
    done(im, "st_prison")


def bound():    # two links of a chain
    im, d = disc((120, 60, 170))
    for (x, y) in ((23, 25), (41, 39)):
        d.ellipse(((x - 11) * 4, (y - 8) * 4, (x + 11) * 4, (y + 8) * 4), outline=K, width=26)
        d.ellipse(((x - 11) * 4, (y - 8) * 4, (x + 11) * 4, (y + 8) * 4), outline=W, width=14)
    done(im, "st_bound")


def star(d, cx, cy, r):
    pts = []
    for i in range(10):
        a = math.radians(-90 + i * 36)
        rr = r if i % 2 == 0 else r * 0.45
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    poly(d, pts, width=8)


def stun():     # stars going round
    im, d = disc((230, 190, 30))
    star(d, 21, 26, 10)
    star(d, 43, 24, 8)
    star(d, 32, 43, 9)
    done(im, "st_stun")


def arrow(up, col, name):
    im, d = disc(col)
    s = -1 if up else 1
    tip = 12 if up else 52
    pts = [(C, tip), (C + 16, tip - s * 18), (C + 6, tip - s * 18), (C + 6, tip - s * 38),
           (C - 6, tip - s * 38), (C - 6, tip - s * 18), (C - 16, tip - s * 18)]
    poly(d, pts)
    done(im, name)


def flinch():   # an impact burst
    im, d = disc((210, 210, 220))
    pts = []
    for i in range(16):
        a = math.radians(i * 22.5)
        r = 20 if i % 2 == 0 else 9
        pts.append((C + r * math.cos(a), C + r * math.sin(a)))
    poly(d, pts, fill=(255, 90, 60, 255))
    done(im, "st_flinch")


def stagger():  # a wobble
    im, d = disc((240, 140, 40))
    for k, y in enumerate((22, 32, 42)):
        pts = [((C - 18 + x) * 4, (y + 5 * math.sin(x / 36 * 2 * math.pi * 1.5 + k)) * 4) for x in range(0, 37, 2)]
        d.line(pts, fill=K, width=22, joint="curve")
        d.line(pts, fill=W, width=10, joint="curve")
    done(im, "st_stagger")


def immune():   # a shield
    im, d = disc((140, 150, 160))
    pts = [(C, 11), (C + 17, 17), (C + 15, 36), (C, 53), (C - 15, 36), (C - 17, 17)]
    poly(d, pts)
    done(im, "st_immune")


def stunstar():  # one star of those circling over a stunned head (a sprite: no disc)
    im = Image.new("RGBA", (S * 4, S * 4), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    star(d, C, C + 2, 27)
    im2 = Image.new("RGBA", (S * 4, S * 4), (0, 0, 0, 0))
    d2 = ImageDraw.Draw(im2)
    pts = []
    for i in range(10):
        a = math.radians(-90 + i * 36)
        rr = 27 if i % 2 == 0 else 27 * 0.45
        pts.append(((C + rr * math.cos(a)) * 4, (C + 2 + rr * math.sin(a)) * 4))
    d2.polygon(pts, fill=(255, 215, 40, 255))
    d2.line(pts + [pts[0]], fill=K, width=14, joint="curve")
    done(im2, "stunstar")


if __name__ == "__main__":
    stunstar()
    prison(); bound(); stun(); arrow(True, (240, 120, 30), "st_launch"); arrow(False, (200, 40, 40), "st_down")
    stagger(); flinch(); immune()
    print("wrote", os.path.normpath(OUT))
