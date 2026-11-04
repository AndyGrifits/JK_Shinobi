#!/usr/bin/env python3
"""Simple seamless skybox for an open-air map: colour by elevation only (so the face order and
orientation conventions cannot produce seams), soft clouds overhead fading out before the face edges.

    python3 tools/maps/gen_sky.py maps/<name>/map.json <build dir>
    -> <build>/base/<sky.env>_{rt,lf,ft,bk,up,dn}.jpg
"""
import json, os, sys
import numpy as np
from PIL import Image

cfg_path, build = sys.argv[1:3]
cfg = json.load(open(cfg_path)); sky = cfg["sky"]
N = 512
zen = np.array(sky.get("zenith", [62, 120, 214]), float)
hor = np.array(sky.get("horizon", [196, 222, 245]), float)
gnd = np.array(sky.get("ground", [150, 160, 150]), float)
rng = np.random.default_rng(7)

def noise2(n, octaves=5):
    out = np.zeros((n, n))
    for o in range(octaves):
        k = 4 * 2 ** o
        g = rng.random((k + 1, k + 1))
        x = np.linspace(0, k, n, endpoint=False)
        i = x.astype(int); f = x - i; f = f * f * (3 - 2 * f)
        a = g[i][:, i]; b = g[i + 1][:, i]; c = g[i][:, i + 1]; d = g[i + 1][:, i + 1]
        out += ((a * (1 - f[:, None]) + b * f[:, None]) * (1 - f[None, :]) +
                (c * (1 - f[:, None]) + d * f[:, None]) * f[None, :]) / 2 ** o
    return out / out.max()

u = np.linspace(-1, 1, N)
U, V = np.meshgrid(u, -u)                      # V: +1 at the image top
def colour(elev):                              # elev in radians, -pi/2..pi/2
    t = np.clip(elev / (np.pi / 2), -1, 1)[..., None]
    up = hor + (zen - hor) * (np.abs(t) ** 0.55)
    down = hor + (gnd - hor) * np.clip(-t * 4, 0, 1)
    return np.where(t >= 0, up, down)

side = colour(np.arctan2(V, np.sqrt(1 + U ** 2)))
up_e = np.arctan2(1, np.sqrt(U ** 2 + V ** 2))
up = colour(up_e)
cl = np.clip((noise2(N) - 0.48) * 3.0, 0, 1) * np.clip((np.degrees(up_e) - 58) / 15, 0, 1)
up = up * (1 - cl[..., None] * 0.85) + 255 * cl[..., None] * 0.85
dn = colour(-np.arctan2(1, np.sqrt(U ** 2 + V ** 2)))

hl = sky.get("hills")       # a distant panorama around the horizon, one repeat per side face (no geometry)
if hl:
    pan = Image.open(hl["path"]).convert("RGBA")
    if "crop" in hl: pan = pan.crop(tuple(hl["crop"]))
    P = np.asarray(pan).astype(float); ph, pw = P.shape[:2]
    if "key_sky" in hl:                        # sky above the hills -> transparent, per column from the top
        sk = ((P[..., 2] - P[..., 0]) > hl["key_sky"][0]) & (P[..., 2] > hl["key_sky"][1])
        P[..., 3] = np.where(np.cumsum(~sk, axis=0) > 0, 255, 0)
    e_top, e_bot = np.radians(hl.get("top_deg", 9)), np.radians(hl.get("bottom_deg", -2))
    el = np.arctan2(V, np.sqrt(1 + U ** 2)); az = np.arctan(U)
    col = np.clip(((az + np.pi / 4) / (np.pi / 2) * pw).astype(int), 0, pw - 1)
    row = np.clip(((e_top - el) / (e_top - e_bot) * (ph - 1)).astype(int), 0, ph - 1)
    smp = P[row, col]; a = (smp[..., 3:] / 255.0) * (el <= e_top)[..., None]
    side = side * (1 - a) + smp[..., :3] * a
    below = (el < e_bot)[..., None]            # under the hills: their foot colour fading into the ground
    foot = P[ph - 4:, :, :3].reshape(-1, 3).mean(0); fade = np.clip((e_bot - el) / np.radians(6), 0, 1)[..., None]
    side = np.where(below, foot * (1 - fade) + side * fade, side)

out = os.path.join(build, "base", os.path.dirname(sky["env"]))
os.makedirs(out, exist_ok=True)
stem = os.path.join(build, "base", sky["env"])
for face in ("rt", "lf", "ft", "bk"):
    Image.fromarray(side.clip(0, 255).astype(np.uint8)).save(f"{stem}_{face}.jpg", quality=92)
Image.fromarray(up.clip(0, 255).astype(np.uint8)).save(f"{stem}_up.jpg", quality=92)
Image.fromarray(dn.clip(0, 255).astype(np.uint8)).save(f"{stem}_dn.jpg", quality=92)
print("SKY OK", stem)
