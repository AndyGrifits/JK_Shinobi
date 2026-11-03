#!/usr/bin/env python3
"""JK_Shinobi: make a player model light enough for the engine's Ghoul2 traces.

    python3 tools/glm_slim.py models/players/kagura/model.glm [--max-verts 10500] [--out x.glm]

OpenJK transforms a whole model (every surface, 20 bytes a vertex) into a 256 KB heap for
each Ghoul2 trace (server: kunai, sabers and jutsu hitting NPCs and shadow clones) and each
saber mark (client), and drops the game with "Ran out of transform space for Ghoul2
Models" when it doesn't fit (Kagura as a shadow clone, 13.8k vertices). The model and the
saber(s) share it, so a player model should stay at about 10.5k game vertices.

This collapses edges (quadric error, vertices stay where they were) on every mesh surface
by the same share until the model fits. Points are welded by position first (the game
splits vertices at texture seams and hard edges, and those splits would block every
collapse); each corner of a new triangle then takes back the UV, normal and weights of
the rip's vertex at that point that fit the triangle best. Tags (surfaces named "*...")
are left alone. Needs pymeshlab (pip install pymeshlab; on Linux also libopengl0).
"""
import argparse
import itertools
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g2file  # noqa: E402

MAX_VERTS = 10500


def slim_surface(s, perc):
    """A copy of Surface s with about perc of its triangles (None = keep it as is)."""
    import pymeshlab as ml
    V = np.asarray(s.verts, float)
    N = np.asarray(s.normals, float)
    UV = np.asarray(s.uvs, float)
    T = np.asarray(s.tris, np.int64)
    _, first, pid = np.unique(np.round(V, 4), axis=0, return_index=True, return_inverse=True)
    pid = pid.ravel()
    P = V[first]
    F = pid[T]
    ok = (F[:, 0] != F[:, 1]) & (F[:, 1] != F[:, 2]) & (F[:, 0] != F[:, 2])
    if ok.sum() < 16:
        return None
    n = len(P)
    idx = np.arange(n)
    col = np.stack([(idx & 255) / 255.0, ((idx >> 8) & 255) / 255.0, ((idx >> 16) & 255) / 255.0,
                    np.ones(n)], 1)
    ms = ml.MeshSet()
    ms.add_mesh(ml.Mesh(vertex_matrix=P, face_matrix=F[ok].astype(np.int32), v_color_matrix=col))
    ms.meshing_decimation_quadric_edge_collapse(
        targetperc=perc, qualitythr=0.3, preserveboundary=True, boundaryweight=1.5,
        optimalplacement=False, preservenormal=True, preservetopology=False,
        planarquadric=True, autoclean=False)
    ms.meshing_remove_unreferenced_vertices()
    o = ms.current_mesh()
    oc = o.vertex_color_matrix()
    opid = (np.round(oc[:, 0] * 255).astype(int) | (np.round(oc[:, 1] * 255).astype(int) << 8)
            | (np.round(oc[:, 2] * 255).astype(int) << 16))
    if np.abs(P[opid] - o.vertex_matrix()).max() > 1e-3:
        raise RuntimeError("decimation moved vertices (%s)" % s.name)
    OF = opid[o.face_matrix()]
    # which side the game's normals face, from the original triangles
    g = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    sign = 1.0 if np.sum(g * N[T].mean(1)) >= 0 else -1.0
    cand = {}
    for vi in range(len(V)):
        cand.setdefault(int(pid[vi]), []).append(vi)
    # edges of the rip's triangles (game vertex pairs): a new triangle takes the vertices
    # that make up most of the old edges between its points, then the ones facing its
    # way, then the ones closest together on the texture
    edges = set()
    for t in T:
        for a_, b_ in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            edges.add((int(a_), int(b_)))
            edges.add((int(b_), int(a_)))
    out = g2file.Surface(s.name, s.shader, s.flags, s.parent)
    made = {}
    for f in OF:
        fn = sign * np.cross(P[f[1]] - P[f[0]], P[f[2]] - P[f[0]])
        fn /= np.linalg.norm(fn) + 1e-12
        cs = [cand[int(p)] for p in f]

        def score(c):
            e = ((c[0], c[1]) in edges) + ((c[1], c[2]) in edges) + ((c[2], c[0]) in edges)
            nd = sum(float(np.dot(N[vi], fn)) for vi in c)
            uvd = (np.linalg.norm(UV[c[0]] - UV[c[1]]) + np.linalg.norm(UV[c[1]] - UV[c[2]])
                   + np.linalg.norm(UV[c[2]] - UV[c[0]]))
            return (-e, round(uvd, 3), -round(nd, 1))
        pick = min(itertools.product(*cs), key=score)
        tri = []
        for vi in pick:
            if vi not in made:
                made[vi] = len(out.verts)
                out.verts.append(tuple(V[vi]))
                out.normals.append(tuple(N[vi]))
                out.uvs.append(tuple(UV[vi]))
                out.weights.append(s.weights[vi])
            tri.append(made[vi])
        if len(set(tri)) == 3:
            out.tris.append(tuple(tri))
    return out


def _collapse(P, F, perc):
    """Quadric edge collapse of points P / triangles F (indices into P) to about perc of
    the triangles, vertices kept where they were, borders kept: the new triangles as
    indices into P."""
    import pymeshlab as ml
    used = np.unique(F)
    if len(F) < 16:
        return F
    loc = np.full(len(P), -1)
    loc[used] = np.arange(len(used))
    ms = ml.MeshSet()
    ms.add_mesh(ml.Mesh(vertex_matrix=np.ascontiguousarray(P[used], dtype=np.float64),
                        face_matrix=np.ascontiguousarray(loc[F], dtype=np.int32)))
    ms.meshing_decimation_quadric_edge_collapse(
        targetperc=perc, qualitythr=0.3, preserveboundary=True, boundaryweight=1.5,
        optimalplacement=False, preservenormal=True, preservetopology=False,
        planarquadric=True, autoclean=False)
    o = ms.current_mesh()
    from scipy.spatial import cKDTree
    d, near = cKDTree(P[used]).query(o.vertex_matrix())
    if len(d) and d.max() > 1e-3:
        raise RuntimeError("decimation moved vertices")
    return used[near[o.face_matrix()]]


def slim_source_mesh(m, perc, swatch=None, texture=None):
    """The same collapse on a converter source mesh (blend2glm: co, weights, loop_n, loop_uv,
    tris of (vertex, loop) corners), before it is cut into 1000-vertex surfaces - for rips
    that are many times over the budget (a 46k-triangle Shinobi Striker model), where the
    cut surfaces' borders would hold the collapse back. Those rips colour the model from
    small swatches of a palette texture: each new triangle takes back, at its points, the old
    (vertex, loop) corners closest together on the texture (one swatch, not a shard of
    another colour), then the ones facing its way. swatch=N collapses each texture cell
    1/N wide on its own with its borders kept instead (exact colours, but it stops early).
    A copy with about perc of the triangles."""
    co = np.asarray(m["co"], float)
    LN = np.asarray(m["loop_n"], float)
    LUV = np.asarray(m["loop_uv"], float) if m.get("loop_uv") is not None else np.zeros((len(LN), 2))
    C = np.array([[c for c in t] for t in m["tris"]], np.int64)      # (t, 3, 2): vertex, loop
    if len(C) < 32:
        return m
    _, first, pid = np.unique(np.round(co, 4), axis=0, return_index=True, return_inverse=True)
    pid = pid.ravel()
    P = co[first]
    F = pid[C[:, :, 0]]
    ok = (F[:, 0] != F[:, 1]) & (F[:, 1] != F[:, 2]) & (F[:, 0] != F[:, 2])
    cell = np.floor(LUV[C[:, :, 1]].mean(1) * (swatch or 1)).astype(int)
    g = np.cross(co[C[:, 1, 0]] - co[C[:, 0, 0]], co[C[:, 2, 0]] - co[C[:, 0, 0]])
    sign = 1.0 if np.sum(g * LN[C[:, :, 1]].mean(1)) >= 0 else -1.0
    groups = [np.where(ok)[0]] if not swatch else \
        [np.where(ok & (cell[:, 0] == k0) & (cell[:, 1] == k1))[0] for k0, k1 in {tuple(c) for c in cell}]
    tris = []
    for sel in groups:
        if not len(sel):
            continue
        cand = {}
        for vi, li in C[sel].reshape(-1, 2):
            lst = cand.setdefault(int(pid[vi]), [])
            if not any(np.dot(LN[li], LN[l2]) > 0.999 and np.abs(LUV[li] - LUV[l2]).max() < 1e-4
                       for _, l2 in lst):
                lst.append((int(vi), int(li)))
        for f in _collapse(P, F[sel], perc):
            fn = sign * np.cross(P[f[1]] - P[f[0]], P[f[2]] - P[f[0]])
            fn /= np.linalg.norm(fn) + 1e-12
            cs = [cand[int(p)] for p in f]
            if len(cs[0]) * len(cs[1]) * len(cs[2]) == 1:
                tris.append([cs[0][0], cs[1][0], cs[2][0]])
                continue

            # corners together on the texture (not across a seam), then the ones facing its way
            # (at a hard edge both faces' corners sit at the point: the front's, not the side's)
            def score(c):
                uv = LUV[[x[1] for x in c]]
                d = (np.abs(uv[0] - uv[1]).sum() + np.abs(uv[1] - uv[2]).sum() + np.abs(uv[2] - uv[0]).sum())
                return (d > 0.05, -round(sum(float(np.dot(LN[li], fn)) for _, li in c), 2), float(d))
            tris.append(list(min(itertools.product(*cs), key=score)))
    out = dict(m, tris=tris)
    if texture and os.path.exists(texture):
        out = _flat_palette(out, m, texture)
    return out


def _flat_palette(new, old, texture, flat=2, diff=40):
    """Palette-coloured rips (Shinobi Striker): the colours are cells of flat colour on a
    small texture and the folds / highlights are thin strips of another cell, so a big
    triangle of the collapsed mesh with its corners in two cells smears every cell in
    between across itself (camouflage). Such a triangle - corners on flat texture (the
    texels around each the same colour) of clearly different colours - takes the colour
    the old surface had at its middle: all three corners on that one texture point."""
    from PIL import Image
    from scipy.spatial import cKDTree
    im = np.asarray(Image.open(texture).convert("RGB"), float)
    h, w = im.shape[:2]
    LUV = np.asarray(new["loop_uv"], float)
    LN = np.asarray(new["loop_n"], float)
    co = np.asarray(new["co"], float)

    def texel(uv):
        x = np.clip((uv[..., 0] % 1.0) * w, 0, w - 1).astype(int)
        y = np.clip((1.0 - uv[..., 1] % 1.0) * h, 0, h - 1).astype(int)
        return x, y

    def is_flat(uv):
        x, y = texel(uv)
        c = im[y, x]
        ok = np.ones(len(x), bool)
        for dx in (-flat, 0, flat):
            for dy in (-flat, 0, flat):
                n = im[np.clip(y + dy, 0, h - 1), np.clip(x + dx, 0, w - 1)]
                ok &= np.abs(n - c).max(1) < 12
        return ok
    OC = np.array([[c for c in t] for t in old["tris"]], np.int64)
    ocen = co[OC[:, :, 0]].mean(1)
    ouv = LUV[OC[:, :, 1]].mean(1)
    tree = cKDTree(ocen)
    NC = np.array([[c for c in t] for t in new["tris"]], np.int64)
    uv = LUV[NC[:, :, 1]]                                   # (t, 3, 2)
    x, y = texel(uv.reshape(-1, 2))
    col = im[y, x].reshape(-1, 3, 3)
    fl = is_flat(uv.reshape(-1, 2)).reshape(-1, 3)
    spread = np.abs(col[:, :, None, :] - col[:, None, :, :]).max(axis=(1, 2, 3))
    bad = np.where(fl.all(1) & (spread > diff))[0]
    if not len(bad):
        return new
    _, near = tree.query(co[NC[bad, :, 0]].mean(1))
    want = ouv[near]
    xw, yw = texel(want)
    wcol = im[yw, xw]
    loop_uv = list(map(tuple, LUV))
    loop_n = list(LN)
    tris = [list(map(tuple, t)) for t in NC]
    for k, t in enumerate(bad):
        # the corner whose colour is the old surface's there: its texture point for all three
        best = int(np.argmin(np.abs(col[t] - wcol[k]).sum(1)))
        target = tuple(LUV[NC[t, best, 1]])
        for j in range(3):
            vi, li = NC[t, j]
            if j == best:
                continue
            loop_uv.append(target)
            loop_n.append(LN[li])
            tris[t][j] = (int(vi), len(loop_uv) - 1)
    print("%s: %d collapsed triangles across palette colours made one colour" % (new["name"], len(bad)))
    return dict(new, tris=tris, loop_uv=loop_uv, loop_n=np.array(loop_n))


def slim(surfs, max_verts=MAX_VERTS, log=print):
    """surfaces -> lighter surfaces (the same list if they already fit)."""
    total = sum(len(s.verts) for s in surfs)
    if total <= max_verts:
        return surfs, total
    mesh = [i for i, s in enumerate(surfs) if not s.name.startswith("*") and len(s.tris) >= 16]

    def run(perc):
        res = list(surfs)
        for i in mesh:
            r = slim_surface(surfs[i], perc)
            if r is not None and len(r.verts) < len(surfs[i].verts):
                res[i] = r
        return res, sum(len(s.verts) for s in res)

    lo, hi, best = 0.1, 1.0, None
    for _ in range(8):
        mid = (lo + hi) / 2
        res, n = run(mid)
        if n <= max_verts:
            best, lo = (res, n, mid), mid
        else:
            hi = mid
    if best is None:
        raise SystemExit("can't get %d vertices under %d" % (total, max_verts))
    res, n, perc = best
    log("slimmed: %d -> %d vertices, %d -> %d triangles (target %d%%)"
        % (total, n, sum(len(s.tris) for s in surfs), sum(len(s.tris) for s in res),
           round(perc * 100)))
    return res, n


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("glm")
    ap.add_argument("--out", help="default: overwrite the input")
    ap.add_argument("--max-verts", type=int, default=MAX_VERTS)
    a = ap.parse_args()
    head, surfs = g2file.read_glm(a.glm)
    res, n = slim(surfs, a.max_verts)
    if res is surfs:
        print("%s: %d vertices, fits" % (a.glm, n))
        return
    g2file.write_glm(a.out or a.glm, head["name"], head["anim"], head["num_bones"], res)
    print("wrote", a.out or a.glm)


if __name__ == "__main__":
    main()
