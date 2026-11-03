#!/usr/bin/env python3
"""JK_Shinobi: a rigged character from an old Naruto game (.blend) -> a Jedi Academy
player model (models/players/<name>/model.glm + model_default.skin + textures).

    python3 tools/blend2glm.py Asuma.blend --gla _humanoid.gla --tags jedi_hm.glm \
        --textures DIR [--name asuma] [--out DIR]
(needs Blender's Python module: pip install bpy; or blender -b --python ... -- args)

Ghoul2 can't take a skeleton of the model's own: the .glm is skinned against the bind
pose stored in _humanoid.gla and every animation brings the stock bone lengths. So the
mesh is bent into the stock bind pose instead:

1. the mesh keeps the weights of its own game's rig (the source armature, here the
   Clash of Ninja one), which are the weights the game was made with;
2. each source bone gets a transform that takes its joint to the stock joint
   (limbs: turn + stretch along the bone, torso: per-axis scale, head: rigid + scale),
   and the mesh is skinned through those (linear blend) into the stock pose;
3. the weights are renamed onto _humanoid bones (RIG_MAP) and the spine part is
   spread over pelvis / lumbar / thoracic by height;
4. tag surfaces (*r_hand, *l_leg_foot, ...) come verbatim from a stock model on the
   same skeleton, so sabers, weapons and effects sit where the game expects them.

Blender's world units x 10 = game units (the Ghoul2 add-on's scene_root convention).
"""
import argparse
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g2file  # noqa: E402

UNIT = 10.0

# ---------------------------------------------------------------- rig mapping
# Clash of Ninja (Wii, nw4r) rig. For each source bone: the _humanoid bone its
# weights go to ("SPINE" = spread by height), and the "driver" whose transform moves
# its vertices into the stock pose.
CON_RIG = {
    "weights": {
        "nw4r_root": "SPINE", "Y": "SPINE", "X": "SPINE", "Z": "SPINE", "hip": "SPINE",
        "waist": "SPINE", "breast": "SPINE",
        "h_202nuno": "pelvis", "h_203nuno": "pelvis",
        "neck": "cervical", "head": "cranium", "mouth_L": "cranium", "mouth_R": "cranium",
        "mouth_TOP": "cranium", "ago": "jaw", "mouth_UNDER": "jaw",
        "%collar": "%clavical", "b_164LkatapadEX": "lclavical", "b_165RkatapadEX": "rclavical",
        "%harm": "%humerus", "%harmEX": "%humerusX", "%harmEXeff": "%humerusX",
        "%larm": "%radius", "h_200L": "lradiusX", "h_201R": "rradiusX",
        "%hand": "%hand",
        "%hleg": "%femurYZ", "%hlegEX": "%femurX", "%hlegEXeff": "%femurX",
        "%lleg": "%tibia", "%foot": "%talus", "%toe": "%talus",
    },
    # fingers (Japanese names): Oy = thumb -> d1, Hi / Na = index / middle -> d2,
    # Ku / Ko = ring / little -> d4; joint 01 -> j1, 02 and 03 -> j2.
    "fingers": {"Oy": "d1", "Hi": "d2", "Na": "d2", "Ku": "d4", "Ko": "d4"},
}


def expand_weights(rig, src_names):
    m = {}
    for k, v in rig["weights"].items():
        if "%" in k:
            for s in "lr":
                m[k.replace("%", s.upper() + "_")] = v.replace("%", s)
        else:
            m[k] = v
    for n in src_names:
        for s in "lr":
            pre = s.upper() + "_fin"
            if n.startswith(pre) and n[len(pre):len(pre) + 2] in rig["fingers"]:
                d = rig["fingers"][n[len(pre):len(pre) + 2]]
                m[n] = "%s_%s_%s" % (s, d, "j1" if n.endswith("01") else "j2")
    return m


def is_finger(name):
    return name[:6] in ("L_finO", "L_finH", "L_finN", "L_finK", "R_finO", "R_finH", "R_finN", "R_finK")


# ---------------------------------------------------------------- math

def rot_between(u, w):
    u = u / np.linalg.norm(u)
    w = w / np.linalg.norm(w)
    v = np.cross(u, w)
    c = float(np.dot(u, w))
    if c < -0.9999:
        a = np.cross(u, [1, 0, 0])
        if np.linalg.norm(a) < 1e-3:
            a = np.cross(u, [0, 1, 0])
        a /= np.linalg.norm(a)
        return 2 * np.outer(a, a) - np.eye(3)
    vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + vx + vx @ vx / (1 + c)


def affine(lin, src, dst):
    """4x4: x -> lin (x - src) + dst."""
    m = np.eye(4)
    m[:3, :3] = lin
    m[:3, 3] = dst - lin @ src
    return m


def segment(sa, sb, ta, tb, clamp=(0.6, 1.6)):
    """Turn the source bone sa->sb onto ta->tb and stretch it along its length."""
    r = rot_between(sb - sa, tb - ta)
    w = (tb - ta) / np.linalg.norm(tb - ta)
    k = np.linalg.norm(tb - ta) / np.linalg.norm(sb - sa)
    k = min(max(k, clamp[0]), clamp[1])
    s = np.eye(3) + (k - 1) * np.outer(w, w)
    return affine(s @ r, sa, ta), r, k


# ---------------------------------------------------------------- blend side

def load_blend(path, armature_name=None):
    import bpy
    bpy.ops.wm.open_mainfile(filepath=os.path.abspath(path))
    meshes = []
    arm = None
    for o in bpy.data.objects:
        if o.type != "MESH":
            continue
        mod = next((m for m in o.modifiers if m.type == "ARMATURE" and m.object), None)
        if not mod:
            continue
        if armature_name and mod.object.name != armature_name:
            continue
        arm = mod.object
        meshes.append(o)
    if not arm:
        raise SystemExit("no mesh with an Armature modifier found")
    for b in arm.pose.bones:                          # bind pose
        b.matrix_basis.identity()
    bpy.context.view_layer.update()
    joints = {b.name: np.array(arm.matrix_world @ b.head_local) * UNIT for b in arm.data.bones}
    out = []
    for o in meshes:
        me = o.data
        me.calc_loop_triangles()
        names = {g.index: g.name for g in o.vertex_groups}
        mw = np.array(o.matrix_world)
        co = np.array([tuple(v.co) for v in me.vertices])
        co = (co @ mw[:3, :3].T + mw[:3, 3]) * UNIT
        nrm_m = np.linalg.inv(mw[:3, :3]).T
        loop_n = np.array([tuple(l.normal) for l in me.loops]) if hasattr(me.loops[0], "normal") else None
        if loop_n is None or not len(loop_n):
            loop_n = np.array([tuple(me.vertices[l.vertex_index].normal) for l in me.loops])
        loop_n = loop_n @ nrm_m.T
        loop_n /= np.linalg.norm(loop_n, axis=1)[:, None] + 1e-12
        uv = me.uv_layers.active.data if me.uv_layers.active else None
        weights = [[(names[g.group], g.weight) for g in v.groups if g.weight > 1e-4]
                   for v in me.vertices]
        mat = o.material_slots[0].material if o.material_slots else None
        tex = None
        if mat and mat.use_nodes:
            for n in mat.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image:
                    tex = os.path.basename(n.image.filepath or n.image.name)
                    break
        tris = []
        for t in me.loop_triangles:
            tris.append([(t.vertices[k], t.loops[k]) for k in range(3)])
        out.append(dict(name=o.name, co=co, loop_n=loop_n,
                        loop_uv=[tuple(uv[i].uv) for i in range(len(me.loops))] if uv else None,
                        weights=weights, tris=tris, texture=tex))
    return arm.name, joints, out


# ---------------------------------------------------------------- the fit

def stock_joints(gla):
    return {b["name"]: np.array([b["base"][0][3], b["base"][1][3], b["base"][2][3]])
            for b in gla.bones}


def build_drivers(J, T):
    """Source joints J (CoN names), stock joints T (_humanoid names) ->
    {source bone: 4x4} for the bones that drive; plus a report."""
    D, rep = {}, []
    mid = lambda a, b: (a + b) / 2
    # torso: per-axis scale + translation (both stand upright facing -Y)
    src = [J["L_hleg"], J["R_hleg"], J["L_harm"], J["R_harm"], J["neck"]]
    dst = [T["lfemurYZ"], T["rfemurYZ"], T["lhumerus"], T["rhumerus"], T["cervical"]]
    src, dst = np.array(src), np.array(dst)
    sx = np.ptp(dst[:, 0]) / np.ptp(src[:, 0])
    sz = np.ptp(dst[:, 2]) / np.ptp(src[:, 2])
    sy = math.sqrt(sx * sz)
    lin = np.diag([sx, sy, sz])
    off = (dst - src @ lin.T).mean(axis=0)
    torso = np.eye(4)
    torso[:3, :3] = lin
    torso[:3, 3] = off
    rep.append("torso scale x %.3f y %.3f z %.3f" % (sx, sy, sz))
    for n in ("nw4r_root", "Y", "X", "Z", "hip", "waist", "breast", "h_202nuno", "h_203nuno"):
        D[n] = torso
    # neck / head: the old rigs put the neck joint higher than _humanoid's cervical,
    # stretching the neck onto it would make a giraffe; the head and neck follow
    # the torso fit instead (head scaled uniformly), only the pivots differ.
    D["neck"] = torso
    hs = (sx + sz) / 2
    hp = (torso @ np.append(J["head"], 1))[:3]
    D["head"] = affine(hs * np.eye(3), J["head"], hp)
    d = hp - T["cranium"]
    rep.append("head scale %.2f, head joint lands %.1f %.1f %.1f from cranium" % (hs, *d))
    for s in "lr":
        S = s.upper() + "_"
        D[S + "collar"], _, k1 = segment(J[S + "collar"], J[S + "harm"], T[s + "clavical"], T[s + "humerus"])
        D[S + "harm"], _, k2 = segment(J[S + "harm"], J[S + "larm"], T[s + "humerus"], T[s + "radius"])
        D[S + "larm"], _, k3 = segment(J[S + "larm"], J[S + "hand"], T[s + "radius"], T[s + "hand"])
        sk = mid(J[S + "finHi01"], J[S + "finKu01"])
        tk = mid(T[s + "_d2_j1"], T[s + "_d4_j1"])
        D[S + "hand"], _, k4 = segment(J[S + "hand"], sk, T[s + "hand"], tk, (0.8, 1.25))
        D[S + "hleg"], _, k5 = segment(J[S + "hleg"], J[S + "lleg"], T[s + "femurYZ"], T[s + "tibia"])
        D[S + "lleg"], rl, k6 = segment(J[S + "lleg"], J[S + "foot"], T[s + "tibia"], T[s + "talus"])
        D[S + "foot"] = affine(rl, J[S + "foot"], T[s + "talus"])
        rep.append("%s: collar %.2f upper arm %.2f forearm %.2f hand %.2f thigh %.2f shin %.2f"
                   % (s, k1, k2, k3, k4, k5, k6))
    return D, rep


def driver_of(bone, parents, D):
    b = bone
    while b is not None:
        if b in D:
            return D[b]
        b = parents.get(b)
    return D["hip"]


SPINE = ["pelvis", "lower_lumbar", "upper_lumbar", "thoracic"]


def spine_split(z, T):
    """Weights over pelvis / lower_lumbar / upper_lumbar / thoracic for a height z:
    each bone is 1 at the middle of its stretch (its joint up to the next one) and
    hands over linearly to the next one by the middle of that one's stretch."""
    hs = [T[n][2] for n in SPINE]
    hs[0] = min(hs[0], hs[1] - 1)
    ends = hs[1:] + [hs[-1] + (hs[-1] - hs[-2])]
    centers = [(a + b) / 2 for a, b in zip(hs, ends)]
    if z <= centers[0]:
        return {SPINE[0]: 1.0}
    for i in range(len(SPINE) - 1):
        if z <= centers[i + 1]:
            t = (z - centers[i]) / (centers[i + 1] - centers[i])
            return {SPINE[i]: 1 - t, SPINE[i + 1]: t}
    return {SPINE[-1]: 1.0}


def convert(src_meshes, src_parents, J, gla, rig):
    T = stock_joints(gla)
    D, report = build_drivers(J, T)
    wmap = expand_weights(rig, J.keys())
    surfaces = []
    for m in src_meshes:
        used = {b for ws in m["weights"] for b, _ in ws}
        prop = len(used) == 1 and is_finger(next(iter(used)))
        if prop:
            # a thing held in the hand (Asuma's trench knives): rigid on the hand,
            # not on a finger the animations curl
            hand = next(iter(used))[0].lower() + "hand"
            print("%s: held prop, rigid on %s" % (m["name"], hand))
        co = m["co"]
        n_v = len(co)
        M = np.zeros((n_v, 4, 4))
        final = []
        for i, ws in enumerate(m["weights"]):
            tot = sum(w for _, w in ws) or 1.0
            acc = np.zeros((4, 4))
            for b, w in ws:
                acc += (w / tot) * driver_of(b, src_parents, D)
            if not ws:
                acc = D["hip"]
            M[i] = acc
        p = np.einsum("nij,nj->ni", M[:, :3, :3], co) + M[:, :3, 3]
        for i, ws in enumerate(m["weights"]):
            tot = sum(w for _, w in ws) or 1.0
            out = {}
            for b, w in ws:
                tgt = hand if prop else wmap.get(b)
                if tgt is None:
                    print("warning: no mapping for source bone %s, using pelvis" % b)
                    tgt = "pelvis"
                if tgt == "SPINE":
                    for sb, sw in spine_split(p[i][2], T).items():
                        out[sb] = out.get(sb, 0) + w / tot * sw
                else:
                    out[tgt] = out.get(tgt, 0) + w / tot
            final.append(sorted(((gla.index[b], w) for b, w in out.items() if w > 1e-3),
                                key=lambda x: -x[1])[:4] or [(gla.index["pelvis"], 1.0)])
        # normals through the inverse transpose of each vertex's blended matrix
        nrm_mats = np.linalg.inv(M[:, :3, :3]).transpose(0, 2, 1)
        surfaces.append(dict(name=m["name"], prop=prop, p=p, nrm_mats=nrm_mats, loop_n=m["loop_n"],
                             loop_uv=m["loop_uv"], weights=final, tris=m["tris"],
                             texture=m["texture"]))
    return surfaces, report, T


def build_glm_surfaces(conv, shader_of, surf_name_of, max_verts=990):
    """Split into game vertices (position + uv + normal) and chunks within the limits."""
    out = []
    for c in conv:
        verts, key_index = [], {}
        tris = []
        for tri in c["tris"]:
            idx = []
            for vi, li in tri:
                uv = c["loop_uv"][li] if c["loop_uv"] else (0, 0)
                n = c["nrm_mats"][vi] @ c["loop_n"][li]
                n = n / (np.linalg.norm(n) + 1e-12)
                key = (vi, round(uv[0], 5), round(uv[1], 5), tuple(np.round(n, 3)))
                if key not in key_index:
                    key_index[key] = len(verts)
                    verts.append((vi, (uv[0], 1.0 - uv[1]), n))
                idx.append(key_index[key])
            tris.append((idx[0], idx[2], idx[1]))      # Blender CCW -> game CW
        # chunking
        chunks = []
        cur, remap, bones = None, {}, set()
        for t in tris:
            new = [v for v in t if v not in remap]
            nb = set(b for v in t for b, _ in c["weights"][verts[v][0]])
            if cur is None or len(cur.verts) + len(new) > max_verts or len(bones | nb) > 32:
                cur = g2file.Surface("", shader_of(c), 0, 0)
                chunks.append(cur)
                remap, bones = {}, set()
            bones |= nb
            o = []
            for v in t:
                if v not in remap:
                    remap[v] = len(cur.verts)
                    vi, uv, n = verts[v]
                    cur.verts.append(tuple(float(x) for x in c["p"][vi]))
                    cur.normals.append(tuple(float(x) for x in n))
                    cur.uvs.append(tuple(float(x) for x in uv))
                    cur.weights.append(c["weights"][vi])
                o.append(remap[v])
            cur.tris.append(tuple(o))
        base = surf_name_of(c)
        for k, ch in enumerate(chunks):
            ch.name = base if len(chunks) == 1 else "%s_%d" % (base, k)
            out.append(ch)
    return out


def tag_surfaces(path, gla):
    """The bolt surfaces ('*' names) of a stock model on the same skeleton."""
    head, surfs = g2file.read_glm(path)
    if head["num_bones"] != gla.num_bones:
        raise SystemExit("%s is not on this skeleton" % path)
    return [s for s in surfs if s.flags & g2file.SURF_BOLT]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("blend")
    ap.add_argument("--gla", required=True, help="_humanoid.gla")
    ap.add_argument("--tags", required=True, help="stock .glm to take the tag surfaces from")
    ap.add_argument("--textures", required=True, help="folder with the textures the materials use")
    ap.add_argument("--name", help="player model name (default: the .blend's name, lower case)")
    ap.add_argument("--out", default=".", help="writes <out>/models/players/<name>/")
    ap.add_argument("--armature", help="source armature (default: the one the meshes use)")
    ap.add_argument("--drop", default="", help="comma-separated mesh objects to leave out")
    a = ap.parse_args(argv)

    name = (a.name or os.path.splitext(os.path.basename(a.blend))[0]).lower()
    gla = g2file.Gla(a.gla)
    arm_name, J, meshes = load_blend(a.blend, a.armature)
    drop = {d for d in a.drop.split(",") if d}
    meshes = [m for m in meshes if m["name"] not in drop]
    import bpy
    parents = {b.name: (b.parent.name if b.parent else None)
               for b in bpy.data.objects[arm_name].data.bones}

    conv, report, T = convert(meshes, parents, J, gla, CON_RIG)
    for r in report:
        print(r)

    gamedir = "models/players/%s" % name
    outdir = os.path.join(a.out, gamedir)
    os.makedirs(outdir, exist_ok=True)

    textures = {}
    from PIL import Image
    for c in conv:
        t = c["texture"]
        if t and t not in textures:
            stem = os.path.splitext(t)[0].lower()
            Image.open(os.path.join(a.textures, t)).convert("RGB").save(
                os.path.join(outdir, stem + ".tga"))
            textures[t] = "%s/%s" % (gamedir, stem)

    counter = {}

    def surf_name(c):
        if c["prop"]:
            hand = c["weights"][0][0][0]
            base = name + "_prop_" + gla.bones[hand]["name"][0]
        else:
            base = name + "_" + (os.path.splitext(c["texture"] or "mesh")[0].split("_")[-1])
        counter[base] = counter.get(base, -1) + 1
        return "%s%s" % (base, "" if counter[base] == 0 else "_%d" % counter[base])

    mesh_surfs = build_glm_surfaces(conv, lambda c: textures.get(c["texture"], ""), surf_name)
    root = g2file.Surface("hips", "", 0, -1)
    surfaces = [root]
    for s in mesh_surfs:
        s.parent = 0
        surfaces.append(s)
    for t in tag_surfaces(a.tags, gla):
        t.parent = 0
        t.shader = ""
        surfaces.append(t)
    g2file.write_glm(os.path.join(outdir, "model.glm"), gamedir + "/model",
                     "models/players/_humanoid/_humanoid", gla.num_bones, surfaces)
    props = [s for s in mesh_surfs if "_prop_" in s.name]
    skins = {"default": True}
    if props:
        skins["props"] = True                  # /model <name>/props shows them
        skins["default"] = False
    for skin, with_props in skins.items():
        with open(os.path.join(outdir, "model_%s.skin" % skin), "w", newline="\n") as f:
            for s in mesh_surfs:
                on = with_props or s not in props
                f.write("%s,%s\n" % (s.name, s.shader if on else "*off"))
    nv = sum(len(s.verts) for s in mesh_surfs)
    nt = sum(len(s.tris) for s in mesh_surfs)
    print("wrote %s: %d mesh surfaces, %d verts, %d tris, %d tags"
          % (outdir, len(mesh_surfs), nv, nt, len(surfaces) - 1 - len(mesh_surfs)))
    return outdir


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--" in args:
        args = args[args.index("--") + 1:]
    main(args)
