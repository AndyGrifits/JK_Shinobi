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


def save_rgba(name, rgb, alpha):
    """Colour texture with a separate alpha channel (for alpha-blended effects)."""
    rgb = np.clip(np.nan_to_num(rgb), 0.0, 1.0)
    alpha = np.clip(np.nan_to_num(alpha), 0.0, 1.0)
    data = np.dstack([rgb[..., 0], rgb[..., 1], rgb[..., 2], alpha])
    Image.fromarray((data * 255).astype(np.uint8), "RGBA").save(os.path.join(OUT, name + ".tga"))


def value_noise(n, cells, seed):
    """Smooth tileable-ish value noise in [0, 1]."""
    rng = np.random.default_rng(seed)
    lattice = rng.random((cells + 1, cells + 1))
    lattice[-1, :] = lattice[0, :]
    lattice[:, -1] = lattice[:, 0]
    coords = np.linspace(0, cells, n, endpoint=False)
    i = coords.astype(int)
    f = coords - i
    f = f * f * (3 - 2 * f)
    a = lattice[np.ix_(i, i)]
    b = lattice[np.ix_(i, i + 1)]
    c = lattice[np.ix_(i + 1, i)]
    d = lattice[np.ix_(i + 1, i + 1)]
    fx = f[None, :]
    fy = f[:, None]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def smoke(n=128):
    """Soft puff of smoke: white, density in alpha."""
    _, _, r, _ = grid(n)
    noise = 0.5 * value_noise(n, 4, 1) + 0.3 * value_noise(n, 8, 2) + 0.2 * value_noise(n, 16, 3)
    blob = np.clip(1.0 - r, 0, 1) ** 1.5
    alpha = np.clip(blob * (0.55 + 0.9 * noise) - 0.08, 0, 1)
    shade = 0.75 + 0.25 * noise  # slightly uneven brightness
    rgb = np.dstack([shade, shade, shade])
    return rgb, alpha


def leaf(n=64):
    """A single leaf with a midrib, pointing up; tinted green by the effect."""
    x, y, _, _ = grid(n)
    t = (1 - y) / 2  # 0 bottom .. 1 top
    half_width = 0.42 * np.clip(np.sin(np.clip(t, 0, 1) * math.pi), 0, 1) ** 0.9
    inside = np.abs(x) < half_width
    edge = np.clip((half_width - np.abs(x)) / 0.06, 0, 1)
    stem = (np.abs(x) < 0.03) & (y > 0.75)
    alpha = np.where(inside, edge, 0.0) + np.where(stem, 1.0, 0.0)
    vein = np.exp(-(x / 0.025) ** 2) * 0.35 + np.exp(-((np.abs(x) - (t - 0.3) * 0.5) / 0.02) ** 2) * 0.15 * (t > 0.3)
    light = 0.85 - vein + 0.15 * x  # one side a bit brighter
    rgb = np.dstack([light, light, light])
    return rgb, alpha


def bolt(n=128, seed=7, branches=2):
    """Jagged lightning bolt across the texture with a couple of side branches."""
    rng = np.random.default_rng(seed)
    img = np.zeros((n, n), np.float32)
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)

    def stroke(points, width, strength):
        nonlocal img
        for (x0, y0), (x1, y1) in zip(points[:-1], points[1:]):
            dx, dy = x1 - x0, y1 - y0
            length2 = dx * dx + dy * dy + 1e-6
            t = np.clip(((xx - x0) * dx + (yy - y0) * dy) / length2, 0, 1)
            d = np.hypot(xx - (x0 + t * dx), yy - (y0 + t * dy))
            img = np.maximum(img, strength * np.exp(-(d / width) ** 2))

    def jagged(x0, y0, x1, y1, steps, jitter):
        pts = []
        for i in range(steps + 1):
            t = i / steps
            off = 0 if i in (0, steps) else rng.normal(0, jitter)
            # perpendicular offset
            px, py = -(y1 - y0), (x1 - x0)
            norm = math.hypot(px, py) or 1
            pts.append((x0 + (x1 - x0) * t + px / norm * off, y0 + (y1 - y0) * t + py / norm * off))
        return pts

    main_pts = jagged(4, n * 0.5, n - 4, n * 0.5, 9, n * 0.07)
    stroke(main_pts, 1.6, 1.0)   # bright core
    stroke(main_pts, 5.0, 0.35)  # glow
    for _ in range(branches):
        k = rng.integers(2, len(main_pts) - 2)
        x0, y0 = main_pts[k]
        x1 = min(n - 4, x0 + rng.uniform(15, 35))
        y1 = y0 + rng.choice([-1, 1]) * rng.uniform(15, 30)
        pts = jagged(x0, y0, x1, y1, 4, n * 0.03)
        stroke(pts, 1.1, 0.8)
        stroke(pts, 3.5, 0.25)
    # fade at both ends so sprites blend
    xs = np.linspace(0, 1, n)[None, :]
    return img * np.clip(np.sin(xs * math.pi) * 1.6, 0, 1)


def chip(n=32, seed=3):
    """Irregular rock chip (alpha-blended, tinted by the effect)."""
    rng = np.random.default_rng(seed)
    x, y, r, a = grid(n)
    radius = 0.7 + 0.12 * np.cos(a * 3 + rng.uniform(0, 6)) + 0.08 * np.cos(a * 5 + rng.uniform(0, 6))
    alpha = np.clip((radius - r) / 0.06, 0, 1)
    shade = 0.75 + 0.25 * (-x * 0.6 - y * 0.6) + 0.15 * value_noise(n, 4, seed)
    rgb = np.dstack([shade, shade, shade])
    return rgb, alpha


def rock(n=256):
    """Tileable earthy rock texture for the earth wall pillars (full colour)."""
    noise = (0.45 * value_noise(n, 6, 11) + 0.3 * value_noise(n, 12, 12)
             + 0.15 * value_noise(n, 24, 13) + 0.1 * value_noise(n, 48, 14))
    # cracks: thin dark lines where a ridged noise is near its middle value
    ridged = np.abs(value_noise(n, 5, 15) - 0.5)
    mask = np.clip((value_noise(n, 3, 17) - 0.45) * 4, 0, 1)  # cracks only in places
    cracks = np.clip(1.0 - ridged / 0.012, 0, 1) * 0.45 * mask
    speckle = (value_noise(n, 64, 18) > 0.8) * 0.08
    # horizontal strata
    yy = np.linspace(0, 1, n)[:, None]
    strata = 0.06 * np.sin(yy * math.pi * 14 + 3 * value_noise(n, 4, 16))
    v = np.clip(0.42 + 0.6 * (noise - 0.5) + strata - cracks - speckle, 0, 1)
    base = np.array([0.56, 0.47, 0.36])  # warm earth
    rgb = v[..., None] * base[None, None, :] * 1.6
    rgb = np.clip(rgb, 0, 1)
    data = np.dstack([rgb, np.ones((n, n))])
    return data


def fire(n=128):
    """Churning ball of flame for the great fireball (full colour, additive).

    Turbulent noise warped around the centre: white-yellow core, orange body,
    ragged red edges fading into black. Several copies are drawn spinning
    against each other, so it must look good at any rotation.
    """
    x, y, r, a = grid(n)
    turb = (0.5 * value_noise(n, 4, 21) + 0.3 * value_noise(n, 8, 22) + 0.2 * value_noise(n, 16, 23))
    # swirl the noise lookup so flames curl around the centre
    swirl_t = (0.5 * value_noise(n, 6, 24) + 0.5 * value_noise(n, 12, 25))
    edge = 0.62 + 0.30 * (turb - 0.5) * 2 + 0.08 * np.sin(a * 5 + swirl_t * 6)
    body = np.clip((edge - r) / 0.28, 0, 1)            # 1 inside, ragged falloff at the rim
    heat = np.clip(1.0 - r / np.maximum(edge, 1e-3), 0, 1) ** 0.8 * (0.75 + 0.5 * swirl_t)
    heat = np.clip(heat, 0, 1)
    # colour ramp by heat: dark red -> red -> orange -> yellow -> white
    stops = np.array([0.0, 0.25, 0.5, 0.75, 1.0])
    cols = np.array([[0.25, 0.02, 0.0], [0.85, 0.12, 0.02], [1.0, 0.45, 0.05], [1.0, 0.8, 0.25], [1.0, 1.0, 0.85]])
    rgb = np.stack([np.interp(heat, stops, cols[:, c]) for c in range(3)], axis=-1)
    rgb *= body[..., None]
    data = np.dstack([rgb, np.ones((n, n))])
    return np.clip(data, 0, 1)


def log_texture(n=256):
    """Bark (top half) and end grain with growth rings (bottom-left quarter)."""
    img = np.zeros((n, n, 3))
    h = n // 2
    # bark: vertical-ish furrows (u runs around the log, v along it)
    yy, xx = np.mgrid[0:h, 0:n].astype(np.float32)
    furrow = 0.5 + 0.5 * np.sin(xx / n * 2 * math.pi * 14 + 2.5 * value_noise(n, 6, 31)[:h, :] * 6)
    grain = value_noise(n, 24, 32)[:h, :] * 0.5 + value_noise(n, 48, 33)[:h, :] * 0.5
    v = 0.35 + 0.35 * furrow ** 2 + 0.3 * grain
    bark = np.array([0.36, 0.25, 0.15])
    img[:h] = v[..., None] * bark[None, None, :] * 1.3
    # end grain: pale wood with dark rings, a dark bark rim
    q = h
    gy, gx = np.mgrid[0:q, 0:q].astype(np.float32)
    c = (q - 1) / 2
    r = np.sqrt((gx - c) ** 2 + (gy - c) ** 2) / c
    wobble = 0.03 * value_noise(q, 5, 34)
    rings = 0.5 + 0.5 * np.cos((r + wobble) * math.pi * 18)
    wood = np.array([0.78, 0.62, 0.40])
    tone = (0.85 - 0.25 * rings ** 3)[..., None] * wood[None, None, :]
    rim = np.clip((r - 0.9) / 0.06, 0, 1)[..., None]
    tone = tone * (1 - rim) + rim * bark[None, None, :] * 0.8
    img[h:, :q] = tone
    img[h:, q:] = tone  # unused, keep it wood coloured
    return np.clip(img, 0, 1)


def ragged_disc(n, seed, edge=0.82, rough=0.10):
    """Irregular disc mask in [0, 1] (puddles, mud, scorch marks)."""
    _, _, r, a = grid(n)
    wob = rough * (value_noise(n, 5, seed) - 0.5) * 2 + 0.04 * np.sin(a * 7 + seed)
    return np.clip((edge + wob - r) / 0.12, 0, 1)


def puddle(n=128):
    """Water lying on the floor: bright ripples, see-through middle (tinted blue)."""
    _, _, r, _ = grid(n)
    disc = ragged_disc(n, 41)
    ripple = 0.5 + 0.5 * np.cos(r * math.pi * 7 + value_noise(n, 4, 42) * 9)
    gloss = value_noise(n, 8, 43)
    shade = 0.7 + 0.2 * ripple + 0.1 * gloss
    alpha = disc * (0.5 + 0.2 * ripple ** 4 + 0.15 * gloss)
    return np.dstack([shade, shade, shade]), alpha


def mud(n=128):
    disc = ragged_disc(n, 51, edge=0.8, rough=0.14)
    noise = 0.6 * value_noise(n, 8, 52) + 0.4 * value_noise(n, 24, 53)
    base = np.array([0.36, 0.25, 0.14])
    rgb = (0.7 + 0.6 * noise)[..., None] * base[None, None, :]
    wet = np.clip((value_noise(n, 12, 54) - 0.62) * 3, 0, 1)[..., None] * 0.14  # glossy wet spots
    return np.clip(rgb + wet, 0, 1), disc * 0.9


def scorch(n=128):
    _, _, r, _ = grid(n)
    disc = ragged_disc(n, 61, edge=0.85, rough=0.16)
    noise = value_noise(n, 10, 62)
    rgb = np.dstack([0.08 + 0.1 * noise, 0.05 + 0.06 * noise, 0.03 + 0.03 * noise])
    alpha = disc * np.clip(1.1 - r * 0.7, 0, 1) * 0.85
    return rgb, alpha


def water_sphere(n=128):
    """A ball of water seen from any side: clear middle, bright rim, a highlight."""
    x, y, r, _ = grid(n)
    inside = r < 1.0
    rim = np.exp(-((r - 0.93) / 0.07) ** 2)
    swirl_n = value_noise(n, 6, 71)
    body = 0.22 + 0.12 * swirl_n
    hl = np.exp(-(((x + 0.35) ** 2 + (y + 0.4) ** 2) / 0.02)) * 0.9
    alpha = np.where(inside, np.clip(body + rim * 0.75 + hl, 0, 1), 0)
    shade = np.clip(0.75 + 0.25 * swirl_n + hl, 0, 1)
    return np.dstack([shade * 0.85, shade * 0.95, shade]), alpha


def droplet(n=32):
    _, _, r, _ = grid(n)
    alpha = np.clip(1 - r, 0, 1) ** 1.5
    return np.ones((n, n, 3)), alpha


def streak(n=64):
    """Soft horizontal line for wind gusts (additive)."""
    x, y, _, _ = grid(n)
    return np.exp(-(y / 0.08) ** 2) * np.clip(np.sin((x + 1) / 2 * math.pi), 0, 1) ** 0.7


def shadow_band(n=64):
    """Shadow Bind strip: black, solid in the middle of the width, soft edges."""
    _, y, _, _ = grid(n)
    alpha = np.clip((1.0 - np.abs(y)) / 0.35, 0, 1) ** 1.5 * 0.85
    return np.zeros((n, n, 3)), alpha


def kunai_texture(n=64):
    """Left half: brushed steel; right half: dark cloth-wrapped grip."""
    img = np.zeros((n, n, 3))
    h = n // 2
    yy, xx = np.mgrid[0:n, 0:h].astype(np.float32)
    brushed = 0.55 + 0.2 * value_noise(n, 32, 81)[:, :h] + 0.15 * (1 - np.abs(yy / n - 0.5) * 2)
    img[:, :h] = np.clip(brushed, 0, 1)[..., None] * np.array([0.82, 0.85, 0.9])[None, None, :]
    stripes = 0.5 + 0.5 * np.sin((xx + yy) / n * math.pi * 10)
    img[:, h:] = (0.12 + 0.1 * stripes)[..., None] * np.array([0.9, 0.8, 0.7])[None, None, :]
    return np.clip(img, 0, 1)


def main():
    os.makedirs(OUT, exist_ok=True)
    Image.fromarray((fire() * 255).astype(np.uint8), "RGBA").convert("RGB").save(os.path.join(OUT, "fire.tga"))
    save("glow", glow())
    save("swirl", swirl())
    save("ring", ring())
    save("wisp", wisp())
    save("spark", spark())
    save("bolt", bolt())
    save_rgba("smoke", *smoke())
    save_rgba("leaf", *leaf())
    save_rgba("chip", *chip())
    save_rgba("puddle", *puddle())
    save_rgba("mud", *mud())
    save_rgba("scorch", *scorch())
    save_rgba("watersphere", *water_sphere())
    save_rgba("droplet", *droplet())
    save("streak", streak())
    save_rgba("shadowband", *shadow_band())

    models = os.path.join(ROOT, "assets", "models", "shinobi")
    os.makedirs(models, exist_ok=True)
    Image.fromarray((rock() * 255).astype(np.uint8), "RGBA").convert("RGB").save(os.path.join(models, "earth_rock.tga"))
    Image.fromarray((log_texture() * 255).astype(np.uint8), "RGB").save(os.path.join(models, "log.tga"))
    Image.fromarray((kunai_texture() * 255).astype(np.uint8), "RGB").save(os.path.join(models, "kunai.tga"))
    print("textures written to", OUT, "and", models)


if __name__ == "__main__":
    main()
