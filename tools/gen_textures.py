#!/usr/bin/env python3
"""Generate JK_Shinobi effect textures procedurally.

Writes grayscale-on-black RGBA TGAs into assets/gfx/shinobi/. The shaders
blend them additively and tint them with the effect's vertex colour, so the
textures themselves stay neutral white.

Usage: python3 tools/gen_textures.py
"""
import math
import os

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "gfx", "shinobi")


def grid(n):
    y, x = np.mgrid[0:n, 0:n].astype(np.float32)
    c = (n - 1) / 2.0
    x = (x - c) / c
    y = (y - c) / c
    r = np.sqrt(x * x + y * y)
    a = np.arctan2(y, x)
    return x, y, r, a


def save(name, v):
    v = np.clip(np.nan_to_num(v), 0.0, 1.0)
    g = (v * 255).astype(np.uint8)
    rgba = np.dstack([g, g, g, g])
    Image.fromarray(rgba, "RGBA").save(os.path.join(OUT, name + ".tga"))


def glow(n=64):
    _, _, r, _ = grid(n)
    core = np.exp(-(r / 0.22) ** 2)
    halo = np.clip(1.0 - r, 0, 1) ** 2.2
    return core * 0.7 + halo * 0.6


def swirl(n=128, arms=3, twist=5.0):
    _, _, r, a = grid(n)
    phase = a * arms + r * twist * math.pi
    stripes = (0.5 + 0.5 * np.cos(phase)) ** 3
    falloff = np.clip(1.0 - r, 0, 1) ** 1.2 * np.clip(r / 0.25, 0, 1)
    return stripes * falloff * 1.3


def ring(n=128, radius=0.78, width=0.09):
    _, _, r, _ = grid(n)
    return np.exp(-((r - radius) / width) ** 2) * (r < 1.0)


def wisp(n=64):
    x, y, _, _ = grid(n)
    # vertical flame tongue: narrow at the top, wide at the bottom
    t = (y + 1) / 2  # 0 top .. 1 bottom
    width = 0.15 + 0.45 * t
    body = np.exp(-(x / width) ** 2) * np.clip(np.sin(np.clip(t, 0, 1) * math.pi), 0, 1) ** 0.8
    return body


def spark(n=32):
    x, y, r, _ = grid(n)
    cross = np.exp(-(x / 0.08) ** 2) * np.clip(1 - abs(y), 0, 1) + np.exp(-(y / 0.08) ** 2) * np.clip(1 - abs(x), 0, 1)
    return np.clip(cross * 0.8 + np.exp(-(r / 0.2) ** 2), 0, 1)


def main():
    os.makedirs(OUT, exist_ok=True)
    save("glow", glow())
    save("swirl", swirl())
    save("ring", ring())
    save("wisp", wisp())
    save("spark", spark())
    print("textures written to", OUT)


if __name__ == "__main__":
    main()
