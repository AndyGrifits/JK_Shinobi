#!/usr/bin/env python3
"""JK_Shinobi: cold weapons (swords, knives, staffs) as Jedi Academy "sabers" without a
light blade.

    python3 tools/weapon2glm.py tools/weapons.json --src "<folder with the model folders>" \
        --out assets [--only katana,staff]

A weapon is a saber the game draws with no blade (noBlade, noDlight, no clash flare, a
plain swing trail) that can't be switched off: the stock saber combat (stances, blocks,
kata) stays, only the look and sound change. For each entry of the json this writes

    <out>/models/weapons2/shinobi_<name>/model.glm   + its textures (.tga)
    <out>/ext_data/sabers/shinobi_<name>.sab

The glm is on the engine's built-in one-bone skeleton ("*default", as the stock hilts).
The game puts the weapon's origin at the hand bolt (*r_hand / *l_hand), whose axes are:
+Z out of the thumb side of the fist (where a held blade points), +Y up the back of the
hand when the arm hangs, X across. The damage line starts at the *blade<n> tag and runs
along its -Y for the .sab's saberLength; the tags here point along +Z (a staff's second
one along -Z), from where the blade leaves the guard.

Entry keys:
  source       mesh file (.obj or .dae) under --src
  map          weapon axes X,Y,Z as source axes, e.g. "Y,-Z,-X" (blade along +Z)
  grip         source point that goes in the fist
  scale        game units per source unit
  stretch      [start, factor]: past Z = start (game units, after mapping) stretch along Z
  thickness    factor for X and Y (a thinner staff)
  blades       [[start Z, direction +1/-1, length], ...] in game units
  title, staff (two-handed, saberType SABER_STAFF), damage, speed (animSpeedScale),
  knockback, move (moveSpeedScale), radius (saberRadius), sounds {swing, hit, block}
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g2file  # noqa: E402


def load_obj(path):
    """[(texture file, verts (n,3), uvs (n,2) v up, normals (n,3), tris (m,3))] per material."""
    V, VT, VN = [], [], []
    mats, cur = {}, None
    mtl_tex = {}
    base = os.path.dirname(path)
    for line in open(path, encoding="latin-1"):
        p = line.split()
        if not p:
            continue
        if p[0] == "v":
            V.append([float(x) for x in p[1:4]])
        elif p[0] == "vt":
            VT.append([float(x) for x in p[1:3]])
        elif p[0] == "vn":
            VN.append([float(x) for x in p[1:4]])
        elif p[0] == "mtllib":
            mname = None
            for ml in open(os.path.join(base, " ".join(p[1:])), encoding="latin-1"):
                q = ml.split()
                if q and q[0] == "newmtl":
                    mname = " ".join(q[1:])
                elif q and q[0] == "map_Kd" and mname:
                    mtl_tex[mname] = " ".join(q[1:])
        elif p[0] == "usemtl":
            cur = " ".join(p[1:])
        elif p[0] == "f":
            corners = []
            for c in p[1:]:
                ix = (c.split("/") + ["", ""])[:3]
                corners.append(tuple(int(x) - 1 if x else -1 for x in ix))
            for k in range(1, len(corners) - 1):
                mats.setdefault(cur, []).append((corners[0], corners[k], corners[k + 1]))
    V, VT, VN = np.array(V), np.array(VT) if VT else None, np.array(VN) if VN else None
    by_tex = {}
    for m, faces in mats.items():
        by_tex.setdefault(mtl_tex.get(m, ""), []).extend(faces)
    out = []
    for tex, faces in by_tex.items():
        idx, verts, uvs, nrms, tris = {}, [], [], [], []
        for f in faces:
            t = []
            for c in f:
                if c not in idx:
                    idx[c] = len(verts)
                    verts.append(V[c[0]])
                    uvs.append(VT[c[1]] if VT is not None and c[1] >= 0 else (0, 0))
                    nrms.append(VN[c[2]] if VN is not None and c[2] >= 0 else (0, 0, 0))
                t.append(idx[c])
            tris.append(t)
        out.append((tex, np.array(verts), np.array(uvs), np.array(nrms), np.array(tris)))
    return out


def load_dae(path):
    import collada
    c = collada.Collada(path)
    out = []
    by_tex = {}
    for g in c.geometries:
        for pr in g.primitives:
            mat = next((m for m in c.materials if m.id == pr.material), None)
            tex = ""
            if mat is not None and hasattr(mat.effect.diffuse, "sampler"):
                tex = os.path.basename(mat.effect.diffuse.sampler.surface.image.path)
            V = pr.vertex[pr.vertex_index].reshape(-1, 3)
            UV = (pr.texcoordset[0][pr.texcoord_indexset[0]].reshape(-1, 2)
                  if len(pr.texcoordset) else np.zeros((len(V), 2)))
            N = pr.normal[pr.normal_index].reshape(-1, 3) if pr.normal is not None else np.zeros_like(V)
            by_tex.setdefault(tex, []).append((V, UV, N))
    for tex, chunks in by_tex.items():
        V = np.vstack([c[0] for c in chunks])
        UV = np.vstack([c[1] for c in chunks])
        N = np.vstack([c[2] for c in chunks])
        key = np.round(np.hstack([V, UV, N]), 5)
        _, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
        out.append((tex, V[first], UV[first], N[first], inv.ravel().reshape(-1, 3)))
    return out


def axis_map(spec):
    """'Y,-Z,-X' -> 3x3 matrix taking source coordinates to weapon coordinates."""
    M = np.zeros((3, 3))
    for row, s in enumerate(spec.split(",")):
        s = s.strip().upper()
        sign = -1.0 if s.startswith("-") else 1.0
        M[row, "XYZ".index(s[-1])] = sign
    if abs(np.linalg.det(M) - 1) > 1e-6:
        raise SystemExit("map %s is a mirror, not a turn" % spec)
    return M


def tag_surface(name, start, direction):
    """A bolt triangle whose tag matrix (G2_ProcessSurfaceBolt: origin = vertex 2, Y = the
    first side, X = the third) has its -Y along +Z * direction."""
    o = np.array([0.0, 0.0, start])
    d = float(direction)
    p2 = o
    p0 = o + np.array([0.5, 0.0, 0.0])
    p1 = p0 + np.array([0.0, 0.0, -2.0 * d])
    s = g2file.Surface(name, "", g2file.SURF_BOLT, 0)
    for p in (p0, p1, p2):
        s.verts.append(tuple(float(x) for x in p))
        s.normals.append((0.0, 1.0, 0.0))
        s.uvs.append((0.0, 0.0))
        s.weights.append([(0, 1.0)])
    s.tris.append((0, 1, 2))
    return s


def build(entry, name, src_root, out_root, log=print):
    src = os.path.join(src_root, entry["source"])
    parts = load_dae(src) if src.lower().endswith(".dae") else load_obj(src)
    M = axis_map(entry["map"])
    grip = np.array(entry["grip"], dtype=float)
    scale = float(entry["scale"])
    gamedir = "models/weapons2/shinobi_%s" % name
    outdir = os.path.join(out_root, gamedir)
    os.makedirs(outdir, exist_ok=True)
    surfaces, nv = [], 0
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    from PIL import Image
    for k, (tex, V, UV, N, T) in enumerate(parts):
        P = (V - grip) @ M.T * scale
        Nn = N @ M.T
        if entry.get("thickness"):
            P[:, :2] *= float(entry["thickness"])
        if entry.get("stretch"):
            z0, f = entry["stretch"]
            far = P[:, 2] > z0
            P[far, 2] = z0 + (P[far, 2] - z0) * f
        lo, hi = np.minimum(lo, P.min(0)), np.maximum(hi, P.max(0))
        shader = ""
        if tex:
            stem = os.path.splitext(os.path.basename(tex))[0]
            Image.open(os.path.join(os.path.dirname(src), os.path.basename(tex))).convert("RGB").save(
                os.path.join(outdir, stem + ".tga"))
            shader = "%s/%s" % (gamedir, stem)
        s = g2file.Surface("%s_%d" % (name, k), shader, 0, -1 if k == 0 else 0)
        for p, uv, n in zip(P, UV, Nn):
            ln = np.linalg.norm(n)
            s.verts.append(tuple(float(x) for x in p))
            s.normals.append(tuple(float(x) for x in (n / ln if ln > 1e-9 else (0, 0, 1))))
            s.uvs.append((float(uv[0]), float(1.0 - uv[1])))
            s.weights.append([(0, 1.0)])
        # the game wants triangles clockwise seen from outside: the source is anticlockwise
        s.tris = [(int(t[0]), int(t[2]), int(t[1])) for t in T]
        if len(s.verts) > 1000:
            raise SystemExit("%s: %d vertices in one surface (max 1000)" % (name, len(s.verts)))
        nv += len(s.verts)
        surfaces.append(s)
    for b, (start, direction, length) in enumerate(entry["blades"]):
        surfaces.append(tag_surface("*blade%d" % (b + 1), start, direction))
    g2file.write_glm(os.path.join(outdir, "model.glm"), gamedir + "/model", "*default", 1, surfaces)
    log("%s: %d vertices, %.1f x %.1f x %.1f units (z %.1f .. %.1f), %d blade(s)"
        % (name, nv, *(hi - lo), lo[2], hi[2], len(entry["blades"])))
    write_sab(entry, name, gamedir, os.path.join(out_root, "ext_data", "sabers"))
    return nv


SOUND = "sound/weapons/shinobi/"


def write_sab(entry, name, gamedir, sabdir):
    os.makedirs(sabdir, exist_ok=True)
    snd = entry.get("sounds", {})
    blades = entry["blades"]
    lines = ["// JK_Shinobi: written by tools/weapon2glm.py from tools/weapons.json - edit that",
             "shinobi_%s" % name, "{",
             '\tname\t\t\t"%s"' % entry["title"],
             "\tsaberType\t\t%s" % ("SABER_STAFF" if entry.get("staff") else "SABER_SINGLE"),
             "\tsaberModel\t\t%s/model.glm" % gamedir,
             "\tnumBlades\t\t%d" % len(blades)]
    for b, (start, direction, length) in enumerate(blades):
        sfx = "" if b == 0 else str(b + 1)
        lines += ["\tsaberLength%s\t\t%g" % (sfx, length),
                  "\tsaberRadius%s\t\t%g" % (sfx, entry.get("radius", 2.5)),
                  "\tsaberColor%s\t\tblue" % sfx]
    if entry.get("staff"):
        lines += ["\ttwoHanded\t\t1"]
    lines += [
        "\tnoBlade\t\t\t1", "\tnoBlade2\t\t1",
        "\tnoDlight\t\t1", "\tnoDlight2\t\t1",
        "\tnoClashFlare\t1", "\tnoClashFlare2\t1",
        "\tnoWallMarks\t\t1", "\tnoWallMarks2\t1",
        "\tnoIdleEffect\t1", "\tnoIdleEffect2\t1",
        "\ttrailStyle\t\t1", "\ttrailStyle2\t\t1",
        "\tnoManualDeactivate\t1", "\tnoManualDeactivate2\t1",
        "\tthrowable\t\t0",
        "\tdisarmable\t\t0",
        "\tdamageScale\t\t%g" % entry.get("damage", 1.0),
        "\tdamageScale2\t%g" % entry.get("damage", 1.0),
        "\tknockbackScale\t%g" % entry.get("knockback", 1.0),
        "\tknockbackScale2\t%g" % entry.get("knockback", 1.0),
        "\tanimSpeedScale\t%g" % entry.get("speed", 1.0),
        "\tmoveSpeedScale\t%g" % entry.get("move", 1.0),
        "\tsoundOn\t\t\t%s" % (SOUND + snd.get("on", "draw.wav")),
        "\tsoundLoop\t\t%s" % (SOUND + "silence.wav"),
        "\tsoundOff\t\t%s" % (SOUND + snd.get("off", "sheathe.wav")),
    ]
    for kind in ("swing", "hit", "block", "bounce"):
        files = snd.get(kind, [])
        for i, f in enumerate(files[:3]):
            key = {"swing": "swingSound%d", "hit": "hitSound%d", "block": "blockSound%d",
                   "bounce": "bounceSound%d"}[kind] % (i + 1)
            lines.append("\t%s\t\t%s" % (key, SOUND + f))
            if kind != "swing":
                k2 = key.replace("Sound", "2Sound")
                lines.append("\t%s\t\t%s" % (k2, SOUND + f))
    lines.append("}")
    with open(os.path.join(sabdir, "shinobi_%s.sab" % name), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("spec", help="tools/weapons.json")
    ap.add_argument("--src", required=True, help="folder the entries' source paths start from")
    ap.add_argument("--out", required=True, help="assets folder")
    ap.add_argument("--only", default="", help="comma-separated weapon names")
    a = ap.parse_args()
    spec = json.load(open(a.spec))
    only = {x for x in a.only.split(",") if x}
    for name, entry in spec.items():
        if name.startswith("_") or (only and name not in only):
            continue
        build(entry, name, a.src, a.out)


if __name__ == "__main__":
    main()
