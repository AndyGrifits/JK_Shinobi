"""Tileable textures for the procedural Orochimaru maze (dim purple-grey stone, dark wood, purple rope).

    python3 tools/maze/textures.py <out dir>      -> <out>/mz_*.jpg / .tga

Every texture wraps in both directions (periodic noise, joints on the tile period), so brush faces can tile
it at any offset.
"""
import os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "maps"))
from textures_gen import fbm, pnoise  # noqa: E402

def col(*c): return np.array(c, float)
def save(img, path):
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(path, quality=92)

def cells_rows(n, rows, cols_per_row, rng, jitter=0.25):
    """running-bond blocks: per pixel (block id, u in block, v in block, block w, block h) on an n x n tile"""
    edges_v = np.cumsum(rng.uniform(1 - jitter, 1 + jitter, rows)); edges_v = edges_v / edges_v[-1]
    edges_v = np.concatenate([[0], edges_v])
    y = np.linspace(0, 1, n, endpoint=False); x = np.linspace(0, 1, n, endpoint=False)
    r = np.searchsorted(edges_v, y, side="right") - 1
    v = (y - edges_v[r]) / (edges_v[r + 1] - edges_v[r]); hh = (edges_v[r + 1] - edges_v[r])
    bid = np.zeros((n, n), int); u = np.zeros((n, n)); ww = np.zeros((n, n))
    for ri in range(rows):
        k = cols_per_row
        e = np.cumsum(rng.uniform(1 - jitter, 1 + jitter, k)); e = e / e[-1]; e = np.concatenate([[0], e])
        sh = rng.uniform(0, 1)
        xs = (x + sh) % 1
        c = np.searchsorted(e, xs, side="right") - 1
        m = r == ri
        bid[m] = (ri * 64 + c)[None, :].repeat(m.sum(), 0)
        u[m] = ((xs - e[c]) / (e[c + 1] - e[c]))[None, :].repeat(m.sum(), 0)
        ww[m] = (e[c + 1] - e[c])[None, :].repeat(m.sum(), 0)
    return bid, u, v[:, None].repeat(n, 1), ww, hh[:, None].repeat(n, 1)

def blocks(n, rows, cols, base, var, mortar, rng, bevel=0.06, rough=8):
    bid, u, v, w, h = cells_rows(n, rows, cols, rng)
    tint = np.vectorize(lambda b: (np.sin(b * 12.9898) * 43758.5453) % 1)(bid)
    du = np.minimum(u, 1 - u) * w * cols; dv = np.minimum(v, 1 - v) * h * rows      # distance to joint, in blocks
    d = np.minimum(du / cols * n, dv / rows * n)                                    # ... in pixels
    img = base + (tint - 0.5)[..., None] * var + (fbm(n, rough, rng, 4) - 0.5)[..., None] * var * 1.4
    img += (fbm(n, 64, rng, 2) - 0.5)[..., None] * 18
    edge = np.clip(d / (bevel * n / rows), 0, 1)
    shade = 0.55 + 0.45 * edge                                                       # chamfered block edges
    lit = np.where(v < 0.5, 1.06, 0.94)[..., None]                                   # top edges catch the light
    img = img * np.where((edge < 1)[..., None], lit, 1) * shade[..., None]
    joint = d < 2.2
    img = np.where(joint[..., None], mortar + (fbm(n, 32, rng, 2) - 0.5)[..., None] * 14, img)
    return img

def make_all(out):
    os.makedirs(out, exist_ok=True)
    R = lambda s: np.random.default_rng(s)
    n = 512
    # walls: irregular stone courses, cool purple-grey
    save(blocks(n, 5, 4, col(98, 86, 104), col(26, 22, 26), col(38, 32, 42), R(1)), f"{out}/mz_wall.jpg")
    # floor: big worn flagstones
    f = blocks(n, 3, 3, col(86, 76, 92), col(22, 18, 22), col(34, 30, 36), R(2), bevel=0.04, rough=6)
    f *= (0.9 + 0.2 * fbm(n, 4, R(3), 3))[..., None]
    save(f, f"{out}/mz_floor.jpg")
    # ceiling / raw rock: warm grey, rough
    rk = fbm(n, 6, R(4), 6); cr = fbm(n, 24, R(5), 3)
    img = col(104, 88, 86) + (rk - 0.5)[..., None] * col(70, 60, 58) + (cr - 0.5)[..., None] * 30
    img *= (0.75 + 0.5 * np.clip(rk * 1.4 - 0.2, 0, 1))[..., None]
    save(img, f"{out}/mz_rock.jpg")
    # trim: dark carved stone with vertical flutes (pillars, door frames)
    x = np.linspace(0, 1, 256, endpoint=False)
    flute = 0.5 + 0.5 * np.cos(x * 2 * np.pi * 8)
    img = col(64, 56, 72) + (flute[None, :] - 0.5)[..., None] * 30 + (fbm(256, 8, R(6), 4) - 0.5)[..., None] * 26
    save(img, f"{out}/mz_trim.jpg")
    # dark wood planks
    g = np.sin(fbm(256, 4, R(7), 3, aspect=(1, 8)) * 18 + np.linspace(0, 30 * np.pi, 256)[None, :])
    img = col(78, 56, 42) + g[..., None] * col(10, 7, 5) + (fbm(256, 8, R(8), 3) - 0.5)[..., None] * col(30, 22, 16)
    img = np.where((np.arange(256) % 64 < 2)[:, None, None], img * 0.45, img)
    save(np.transpose(img, (1, 0, 2)), f"{out}/mz_wood.jpg")
    # Orochimaru's purple rope (twist along u)
    yy, xx = np.mgrid[0:256, 0:256] / 256
    tw = 0.5 + 0.5 * np.sin((xx * 6 + yy * 3) * 2 * np.pi)
    img = col(96, 52, 140) * (0.55 + 0.6 * tw)[..., None] + (fbm(256, 16, R(9), 2) - 0.5)[..., None] * 20
    save(img, f"{out}/mz_rope.jpg")
    # rusty lab metal
    img = col(92, 90, 88) + (fbm(256, 6, R(10), 5) - 0.5)[..., None] * col(40, 36, 34)
    rust = np.clip(fbm(256, 5, R(11), 4) * 2.2 - 1.1, 0, 1)
    img = img * (1 - rust[..., None]) + rust[..., None] * col(110, 62, 36)
    save(img, f"{out}/mz_metal.jpg")
    # tank liquid: green glow with bubbles of noise (scrolled in the shader)
    img = col(40, 150, 70) + (fbm(256, 8, R(12), 4) - 0.5)[..., None] * col(40, 90, 50)
    save(img, f"{out}/mz_liquid.jpg")
    # glass: faint streaks (blended over)
    img = col(150, 180, 175) + (fbm(256, 4, R(13), 3, aspect=(1, 4)) - 0.5)[..., None] * 60
    save(img, f"{out}/mz_glass.jpg")
    # water: dark murky green-blue ripples
    rp = fbm(256, 6, R(14), 4)
    img = col(40, 62, 66) + (rp - 0.5)[..., None] * col(30, 40, 40)
    save(img, f"{out}/mz_water.jpg")
    # flame (additive): soft teardrop, black background
    yy, xx = np.mgrid[0:128, 0:128] / 128
    d = np.sqrt(((xx - 0.5) / (0.22 + 0.25 * yy)) ** 2 + ((yy - 0.62) / 0.42) ** 2)
    a = np.clip(1 - d, 0, 1) ** 1.6
    img = a[..., None] * col(255, 170, 70) + (a ** 3)[..., None] * col(0, 70, 90)
    save(img, f"{out}/mz_flame.jpg")
    # glow decal (additive halo on the wall behind a torch)
    d = np.sqrt((xx - 0.5) ** 2 + (yy - 0.5) ** 2) * 2
    img = (np.clip(1 - d, 0, 1) ** 2)[..., None] * col(120, 70, 30)
    save(img, f"{out}/mz_halo.jpg")
    print("TEXTURES OK", out)

if __name__ == "__main__":
    make_all(sys.argv[1])
