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
            return (-e, -round(nd, 1), uvd)
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
