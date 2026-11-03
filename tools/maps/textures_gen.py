#!/usr/bin/env python3
"""Procedural, tileable painted-look textures for maps (numpy + Pillow).

    python3 tools/maps/textures_gen.py <kind> <out.jpg> [size]     kinds: concrete, rock, bark, wood
    from textures_gen import make; make("rock", path)

concrete - a plain poured wall: horizontally seamless (walls wrap in u), soft blotches, rain streaks,
           grass + dirt at the bottom (image bottom = foot of the wall), like the original wall textures.
rock     - seamless both ways: warm grey stone, cracks, posterised like the map's hand-painted art.
bark     - seamless: vertical brown bark with grooves (logs, stumps).
wood     - seamless: light dressed wood with grain (training posts).
"""
import sys
import numpy as np
from PIL import Image, ImageFilter


ALPHA = {"dust"}          # kinds with an alpha channel (saved as PNG, packed as TGA)


def pnoise(n, k, rng, aspect=(1, 1)):
    """periodic value noise, k cells across (x and y wrap)"""
    kx, ky = k * aspect[0], k * aspect[1]
    g = rng.random((ky, kx))
    x = np.linspace(0, kx, n, endpoint=False); y = np.linspace(0, ky, n, endpoint=False)
    xi = x.astype(int); yi = y.astype(int); fx = x - xi; fy = y - yi
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy)
    x1 = (xi + 1) % kx; y1 = (yi + 1) % ky
    a = g[yi][:, xi]; b = g[yi][:, x1]; c = g[y1][:, xi]; d = g[y1][:, x1]
    top = a * (1 - fx) + b * fx; bot = c * (1 - fx) + d * fx
    return top * (1 - fy[:, None]) + bot * fy[:, None]


def fbm(n, k, rng, oct=5, aspect=(1, 1), gain=0.5):
    out = np.zeros((n, n)); amp = 1; tot = 0
    for o in range(oct):
        out += amp * pnoise(n, k * 2 ** o, rng, aspect); tot += amp; amp *= gain
    return out / tot


def col(*c):
    return np.array(c, float)


def make(kind, path, n=None):
    rng = np.random.default_rng({"concrete": 11, "rock": 23, "bark": 37, "wood": 41, "adobe": 53, "sandrock": 59,
                                 "dark": 61, "crate": 67, "pooltile": 71, "water": 73, "dust": 79, "paper": 83}[kind])
    alpha = None
    if kind == "concrete":
        n = n or 1024
        v = np.linspace(1, 0, n)[:, None]                        # 1 at the image top, 0 at the bottom
        base = col(196, 191, 180)
        blot = fbm(n, 3, rng, 5) - 0.5
        grain = fbm(n, 64, rng, 2) - 0.5
        streak = fbm(n, 24, rng, 3, aspect=(1, 1)) - 0.5
        streak = np.repeat(streak[:1], n, 0) * 0.6 + (fbm(n, 4, rng, 3, aspect=(8, 1)) - 0.5)  # vertical streaks
        img = base + blot[..., None] * col(46, 44, 40) + grain[..., None] * 18
        img += (streak[..., None] * col(-30, -30, -28)) * (0.3 + 0.7 * v[..., None])  # rain streaks from the top
        img += ((fbm(n, 6, rng, 4) > 0.62) * -10)[..., None]       # damp patches
        # foot of the wall: dirt, then grass (matches the field ground)
        edge = 0.10 + (fbm(n, 12, rng, 3) - 0.5)[:1] * 0.06
        dirt = np.clip((edge + 0.08 - v) / 0.08, 0, 1)              # 1 at the bottom (v = 0)
        grass = np.clip((edge - v) / 0.03, 0, 1) * (fbm(n, 40, rng, 3) > 0.35)
        img = img * (1 - dirt[..., None] * 0.35) + dirt[..., None] * col(120, 105, 80) * 0.35
        g = col(96, 150, 66) + (fbm(n, 50, rng, 2) - 0.5)[..., None] * 40
        img = img * (1 - grass[..., None]) + g * grass[..., None]
        img += np.clip(v - 0.985, 0, 1)[..., None] * -900          # slight dark lip at the top edge
    elif kind == "rock":
        n = n or 512
        base = col(168, 156, 136)                                     # warm sandstone grey
        big = fbm(n, 3, rng, 4); mid = fbm(n, 9, rng, 4)
        lum = (big - 0.5) * 60 + (mid - 0.5) * 36
        lum = np.round(lum / 12) * 12                                 # posterise: painted value bands
        img = base + lum[..., None] * col(1.0, 0.95, 0.85)
        cr = np.abs(fbm(n, 4, rng, 3) - 0.5)
        img -= (cr < 0.012)[..., None] * col(48, 46, 42)              # a few thin cracks
        img += (fbm(n, 64, rng, 2) - 0.5)[..., None] * 12             # grain
        img += ((mid > 0.68) * 14)[..., None]                         # light ledges
    elif kind == "bark":
        n = n or 512
        f = fbm(n, 4, rng, 4, aspect=(4, 1))
        grooves = np.abs(fbm(n, 4, rng, 3, aspect=(4, 1)) - 0.5)     # long along v (the log's length)
        img = col(112, 82, 58) + (f - 0.5)[..., None] * col(50, 36, 26)
        img -= (grooves < 0.06)[..., None] * col(45, 34, 26)
        img = np.round(img / 10) * 10
    elif kind == "wood":
        n = n or 512
        grain = np.sin((fbm(n, 4, rng, 3, aspect=(1, 6)) * 40 + np.linspace(0, 40 * np.pi, n)[None, :]))
        img = col(176, 134, 88) + grain[..., None] * col(16, 13, 10) + (fbm(n, 6, rng, 3) - 0.5)[..., None] * 30
        img = np.round(img / 8) * 8
    elif kind == "adobe":                                             # warm desert plaster
        n = n or 512
        base = col(206, 176, 132)
        blot = fbm(n, 3, rng, 5) - 0.5; mid = fbm(n, 12, rng, 3) - 0.5
        img = base + blot[..., None] * col(44, 38, 30) + mid[..., None] * 16
        img = np.round(img / 6) * 6
        cr = np.abs(fbm(n, 5, rng, 3) - 0.5)
        img -= (cr < 0.004)[..., None] * col(16, 14, 12)               # faint hairline cracks
        img += (fbm(n, 90, rng, 2) - 0.5)[..., None] * 10              # grain
        img -= ((fbm(n, 4, rng, 3) > 0.64) * 12)[..., None]            # weathered patches
    elif kind == "sandrock":                                          # sandstone for desert rocks
        n = n or 512
        base = col(196, 158, 108)
        big = fbm(n, 3, rng, 4); mid = fbm(n, 9, rng, 4)
        strata = np.sin(np.linspace(0, 18 * np.pi, n))[:, None] * 0.5 + (fbm(n, 6, rng, 3) - 0.5)
        lum = (big - 0.5) * 50 + (mid - 0.5) * 30 + strata * 14
        lum = np.round(lum / 10) * 10
        img = base + lum[..., None] * col(1.0, 0.9, 0.75)
        img += (fbm(n, 64, rng, 2) - 0.5)[..., None] * 12
    elif kind == "dark":                                              # window / door holes
        n = n or 64
        img = col(46, 34, 26) + (fbm(n, 2, rng, 2) - 0.5)[..., None] * 10
    elif kind == "crate":                                             # wooden crate: planks + frame
        n = n or 256
        x = np.linspace(0, 1, n)
        grain = np.sin((fbm(n, 4, rng, 3, aspect=(1, 6)) * 30 + np.linspace(0, 30 * np.pi, n)[None, :]))
        img = col(160, 116, 70) + grain[..., None] * col(14, 11, 8) + (fbm(n, 8, rng, 3) - 0.5)[..., None] * 24
        planks = (np.abs(((x * 4) % 1) - 0.5) > 0.47)[:, None]          # plank gaps (rows)
        frame = ((x < 0.09) | (x > 0.91))
        img = np.where(planks[..., None], img * 0.55, img)
        img = np.where((frame[None, :] | frame[:, None])[..., None], img * 0.82 + col(10, 6, 2), img)
    elif kind == "pooltile":                                          # weathered grey-green stone tiles, sandy grout
        n = n or 256
        x = np.linspace(0, 1, n, endpoint=False)
        gx = np.abs(((x * 8) % 1) - 0.5) > 0.44; grout = gx[None, :] | gx[:, None]
        tint = np.floor(x * 8)[None, :] * 7 + np.floor(x * 8)[:, None] * 13
        var = (np.sin(tint) * 0.5 + 0.5)[..., None] * col(-18, -10, -6)
        img = col(118, 132, 122) + var * 0.6 + (fbm(n, 6, rng, 3) - 0.5)[..., None] * col(26, 26, 22)
        img = np.where(grout[..., None], col(176, 160, 128) + (fbm(n, 30, rng, 2) - 0.5)[..., None] * 16, img)
    elif kind == "paper":                                             # scroll paper
        n = n or 64
        img = col(226, 214, 184) + (fbm(n, 4, rng, 3) - 0.5)[..., None] * 18
        img -= ((np.abs(((np.linspace(0, 1, n) * 6) % 1) - 0.5) > 0.46)[:, None] * 40)[..., None]
    elif kind == "water":                                             # blue-green ripples (scrolled in the shader)
        n = n or 256
        a = fbm(n, 4, rng, 4); b = fbm(n, 8, rng, 3)
        rip = np.abs(np.sin((a * 3 + b) * 2 * np.pi))                 # wavy caustic-like bands
        img = col(38, 112, 128) + (a - 0.5)[..., None] * col(30, 40, 40) + (rip ** 8)[..., None] * col(90, 110, 100)
    elif kind == "dust":                                              # wind-blown sand curtain: RGBA, opaque at the
        n = n or 512                                                  # foot (image bottom), fading out at the top
        streak = fbm(n, 3, rng, 5, aspect=(1, 1))
        streak = 0.6 * fbm(n, 2, rng, 4, aspect=(1, 6)) + 0.4 * streak  # long horizontal gusts
        v = np.linspace(1, 0, n)[:, None]                             # 1 at the top row
        prof = np.clip(1 - v, 0, 1) ** 1.1
        alpha = np.clip(prof * (0.45 + 0.9 * streak), 0, 1) * 245
        img = col(214, 178, 128) + (streak - 0.5)[..., None] * col(40, 34, 26)
    else:
        raise SystemExit(f"unknown kind {kind}")
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    im = im.filter(ImageFilter.SMOOTH_MORE)
    if alpha is not None:
        im = im.convert("RGBA"); im.putalpha(Image.fromarray(np.clip(alpha, 0, 255).astype(np.uint8)).filter(ImageFilter.SMOOTH_MORE))
        im.save(path)
        return path
    im.save(path, quality=92)
    return path


if __name__ == "__main__":
    make(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else None)
