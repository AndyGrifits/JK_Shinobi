#!/usr/bin/env python3
"""JK_Shinobi: render a player model in given skeleton poses (no Blender): textured flat
triangles, painter's order, front / side / three-quarter views. Used by make_anims.py to
check new animations before the game sees them.

    from anim_preview import Model, render_strip
    m = Model("assets/models/players/neji/model.glm")
    render_strip(m, [finals0, finals1, ...], "out.png", views=("front", "side"))
finals: (nb, 4, 4) skinning matrices (final = posed x inverse bind).
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g2file  # noqa: E402


def _load_tex(path_noext, root):
    from PIL import Image
    for ext in (".tga", ".png", ".jpg"):
        p = os.path.join(root, path_noext + ext)
        if os.path.exists(p):
            return np.asarray(Image.open(p).convert("RGB"), dtype=np.float32) / 255.0
    return None


class Model:
    def __init__(self, glm, assets_root=None):
        root = assets_root or os.path.join(os.path.dirname(glm), "..", "..", "..")
        _, surfs = g2file.read_glm(glm)
        skin = {}
        sk = os.path.join(os.path.dirname(glm), "model_default.skin")
        if os.path.exists(sk):
            for line in open(sk):
                if "," in line:
                    a, b = line.strip().split(",", 1)
                    skin[a] = b
        self.parts = []
        for s in surfs:
            if s.flags & (g2file.SURF_BOLT | g2file.SURF_OFF) or not s.tris:
                continue
            sh = skin.get(s.name, s.shader)
            if not sh or sh.endswith("off"):
                continue
            tex = _load_tex(os.path.splitext(sh)[0], root)
            v = np.array(s.verts, dtype=np.float64)
            tris = np.array(s.tris)
            uv = np.array(s.uvs)
            if tex is not None:
                c = uv[tris].mean(axis=1)
                h, w = tex.shape[:2]
                x = np.clip((c[:, 0] % 1.0) * (w - 1), 0, w - 1).astype(int)
                y = np.clip((c[:, 1] % 1.0) * (h - 1), 0, h - 1).astype(int)
                col = tex[y, x]
            else:
                col = np.tile([0.7, 0.7, 0.7], (len(tris), 1))
            nb = max(b for ws in s.weights for b, _ in ws) + 1
            W = np.zeros((len(v), 64))
            for i, ws in enumerate(s.weights):
                for b, w in ws:
                    W[i, b] += w
            self.parts.append((v, tris, col, W))

    def skinned(self, finals):
        out = []
        for v, tris, col, W in self.parts:
            vh = np.hstack([v, np.ones((len(v), 1))])
            per = np.einsum("bij,vj->vbi", finals[:, :3, :], vh)        # (nv, nb, 3)
            p = np.einsum("vb,vbi->vi", W[:, :finals.shape[0]], per)
            out.append((p[tris], col))
        return out


VIEWS = {  # screen x, screen y, towards the viewer
    "front": (np.array([1.0, 0, 0]), np.array([0, 0, 1.0]), np.array([0, -1.0, 0])),
    "side": (np.array([0, 1.0, 0]), np.array([0, 0, 1.0]), np.array([1.0, 0, 0])),
    "3q": (np.array([0.7071, 0.7071, 0]), np.array([0, 0, 1.0]), np.array([0.7071, -0.7071, 0])),
    "back3q": (np.array([-0.7071, 0.7071, 0]), np.array([0, 0, 1.0]), np.array([0.7071, 0.7071, 0])),
}


def draw(ax, model, finals, view, title=None, ground=-24.0):
    from matplotlib.collections import PolyCollection
    sx, sy, toward = VIEWS[view]
    light = 0.6 * toward + 0.6 * np.array([0, 0, 1.0]) + 0.3 * sx
    light /= np.linalg.norm(light)
    polys, cols, depth = [], [], []
    for tri, col in model.skinned(finals):
        n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
        nn = np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
        n = n / nn
        shade = 0.35 + 0.65 * np.abs(n @ light)
        polys.append(np.stack([tri @ sx, tri @ sy], axis=2))
        cols.append(np.clip(col * shade[:, None], 0, 1))
        depth.append((tri @ toward).mean(axis=1))
    polys = np.concatenate(polys)
    cols = np.concatenate(cols)
    depth = np.concatenate(depth)
    o = np.argsort(depth)
    ax.add_collection(PolyCollection(polys[o], facecolors=cols[o], edgecolors="none"))
    ax.axhline(ground, color="#999", lw=0.8)
    ax.set_xlim(-45, 45)
    ax.set_ylim(-28, 46)
    ax.set_aspect("equal")
    ax.axis("off")
    if title:
        ax.set_title(title, fontsize=8)


def render_strip(model, frames, path, views=("front", "side", "3q"), titles=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    n = len(frames)
    fig, axs = plt.subplots(len(views), n, figsize=(1.9 * n, 2.2 * len(views)), squeeze=False)
    for c, fin in enumerate(frames):
        for r, v in enumerate(views):
            draw(axs[r][c], model, fin, v, (titles[c] if titles and r == 0 else None))
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


def render_gif(model, frames, path, view="3q", fps=20):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from PIL import Image
    imgs = []
    for fin in frames:
        fig, ax = plt.subplots(figsize=(3, 3))
        draw(ax, model, fin, view)
        fig.tight_layout()
        fig.canvas.draw()
        imgs.append(Image.fromarray(np.asarray(fig.canvas.buffer_rgba())[:, :, :3]))
        plt.close(fig)
    imgs[0].save(path, save_all=True, append_images=imgs[1:], duration=int(1000 / fps), loop=0)
