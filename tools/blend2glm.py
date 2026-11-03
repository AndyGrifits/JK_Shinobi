#!/usr/bin/env python3
"""JK_Shinobi: a rigged character from an old Naruto game (.blend) -> a Jedi Academy
player model (models/players/<name>/model.glm + model_default.skin + textures).

    python3 tools/blend2glm.py Asuma.blend|Kakashi.smd --gla _humanoid.gla --tags jedi_hm.glm \
        --textures DIR [--name asuma] [--out DIR]
(needs Blender's Python module: pip install bpy; or blender -b --python ... -- args)

Ghoul2 can't take a skeleton of the model's own: the .glm is skinned against the bind
pose stored in _humanoid.gla and every animation brings the stock bone lengths. So the
mesh is bent into the stock bind pose instead:

1. the mesh keeps the weights of its own game's rig (the source armature, here the
   Clash of Ninja one), which are the weights the game was made with;
2. each source bone gets a transform that takes its joint to the stock joint
   (limbs: turn + stretch along the bone, torso: mapped by height onto the stock hip and
   shoulder joints, head: moved + scaled),
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
HEAD_OVER_FEET = 51.4  # head joint over the ankles, game units (as Kakashi's rip at SMD_SCALE)
ADULT_CM = 181       # Kakashi: drawn at the stock JA size; shorter ones scaled down
SMD_SCALE = 33.56     # these rips are ~1.8 high; same size as the .blend ones x 10

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
    parents = {b.name: (b.parent.name if b.parent else None) for b in arm.data.bones}
    return joints, parents, out


def _smd_rot(x, y, z):
    cx, sx, cy, sy, cz, sz = (math.cos(x), math.sin(x), math.cos(y), math.sin(y),
                              math.cos(z), math.sin(z))
    rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return rz @ ry @ rx


def smd_texture(material, tex_dir):
    """SMD material names from these rips look like 'kks_tex01.png_kks_eye': the part
    after '.png_' is the texture actually used when it names one (maybe cut short)."""
    files = os.listdir(tex_dir) if tex_dir and os.path.isdir(tex_dir) else []
    head, _, tail = material.partition(".png_")
    for cand in ([tail] if tail else []) + [head.replace(".png", "")]:
        for f in sorted(files):
            if f.lower().endswith(".png") and f[:-4].lower().startswith(cand.lower()):
                return f
    return head if head.endswith(".png") else head + ".png"


# the engine transforms a whole model (all its surfaces, 20 bytes a vertex) into one heap
# for each Ghoul2 trace (server: kunai, sabers on NPCs and clones) and saber mark (client):
# 1 MB in our engine build (tools/build-win64-engine.sh), 256 KB (~12k vertices) in stock
# OpenJK; the saber and the per-surface tables need some of it too
VERT_BUDGET = 45000
VERT_STOCK = 12000


def smooth_normals(m, max_angle=60.0):
    """Rips that come with one normal per face (flat shaded, and three game vertices
    per triangle): average each corner's normal with the other corners of the same
    vertex within max_angle, so smooth surfaces share vertices and hard edges stay."""
    by = {}
    for tri in m["tris"]:
        for vi, li in tri:
            by.setdefault(vi, []).append(li)
    n = np.array(m["loop_n"], dtype=float)
    out = n.copy()
    cos = math.cos(math.radians(max_angle))
    for ls in by.values():
        if len(ls) < 2:
            continue
        g = n[ls]
        close = (g @ g.T) >= cos
        # corners joined through close neighbours share one normal (one game vertex)
        grp = list(range(len(ls)))

        def root(i):
            while grp[i] != i:
                grp[i] = grp[grp[i]]
                i = grp[i]
            return i
        for a, b in zip(*np.nonzero(np.triu(close, 1))):
            grp[root(a)] = root(b)
        r = np.array([root(i) for i in range(len(ls))])
        for k in set(r.tolist()):
            sel = r == k
            avg = g[sel].sum(0)
            out[np.asarray(ls)[sel]] = np.round(avg / (np.linalg.norm(avg) + 1e-12), 3)
    m["loop_n"] = out


def faceted(m):
    by = {}
    for tri in m["tris"]:
        for vi, li in tri:
            by.setdefault(vi, []).append(li)
    n = np.asarray(m["loop_n"])
    spread = [1 - float(np.min(n[ls] @ n[ls].mean(0)) / (np.linalg.norm(n[ls].mean(0)) + 1e-12))
              for ls in by.values() if len(ls) > 1]
    return bool(spread) and float(np.median(spread)) > 0.05


def load_smd(path, tex_dir=None, scale=SMD_SCALE, flip_v=False, hard_angle=60.0):
    """A Source .smd (version 1): skeleton (frame 0 = bind pose) and triangles with
    their weights; one mesh per material. Same output as load_blend."""
    lines = open(path, encoding="latin-1").read().splitlines()
    i = lines.index("nodes") + 1
    names, par = {}, {}
    while lines[i].strip() != "end":
        q = lines[i].split('"')
        names[int(q[0])] = q[1]
        par[int(q[0])] = int(q[2])
        i += 1
    i = lines.index("skeleton") + 2
    world = {}
    while lines[i].strip() != "end":
        v = lines[i].split()
        if v[0] == "time":
            break
        b = int(v[0])
        m = np.eye(4)
        m[:3, :3] = _smd_rot(*map(float, v[4:7]))
        m[:3, 3] = list(map(float, v[1:4]))
        world[b] = m if par[b] < 0 else world[par[b]] @ m
        i += 1
    joints = {names[b]: world[b][:3, 3] * scale for b in world}
    parents = {names[b]: (names[par[b]] if par[b] >= 0 else None) for b in names}
    i = lines.index("triangles") + 1
    meshes = {}
    while i < len(lines) and lines[i].strip() != "end":
        mat = lines[i].strip()
        m = meshes.setdefault(mat, dict(name=mat, keys={}, co=[], weights=[], loop_n=[],
                                        loop_uv=[], tris=[], texture=smd_texture(mat, tex_dir)))
        tri = []
        for k in range(1, 4):
            v = lines[i + k].split()
            pos = tuple(float(x) * scale for x in v[1:4])
            if len(v) > 9 and int(v[9]) > 0:
                n = int(v[9])
                ws = [(names[int(v[10 + 2 * j])], float(v[11 + 2 * j])) for j in range(n)]
            else:
                ws = [(names[int(v[0])], 1.0)]
            key = (pos, tuple(sorted(ws)))
            if key not in m["keys"]:
                m["keys"][key] = len(m["co"])
                m["co"].append(pos)
                m["weights"].append(ws)
            m["loop_n"].append(tuple(float(x) for x in v[4:7]))
            m["loop_uv"].append((float(v[7]), 1.0 - float(v[8]) if flip_v else float(v[8])))
            tri.append((m["keys"][key], len(m["loop_n"]) - 1))
        m["tris"].append(tri)
        i += 4
    out = []
    for m in meshes.values():
        del m["keys"]
        m["co"] = np.array(m["co"])
        n = np.array(m["loop_n"])
        m["loop_n"] = n / (np.linalg.norm(n, axis=1)[:, None] + 1e-12)
        if faceted(m):
            smooth_normals(m, hard_angle)
            print("%s: one normal per face in the rip, smoothed (hard edges over %g deg kept)"
                  % (m["name"], hard_angle))
        out.append(m)
    return joints, parents, out


def upright(J, meshes):
    """Turn a rip into the game's frame (z up, the character's left +x, facing -y):
    rips of the same game come out in different axes (Kakashi z-up, Sasuke -y-up)."""
    mid = lambda a, b: (np.asarray(a) + np.asarray(b)) / 2
    up = np.asarray(J["head"]) - mid(J["L_foot"], J["R_foot"])
    k = HEAD_OVER_FEET / np.linalg.norm(up)     # rips also come in different units
    up /= np.linalg.norm(up)
    left = np.asarray(J["L_harm"]) - np.asarray(J["R_harm"])
    left -= up * np.dot(left, up)
    left /= np.linalg.norm(left)
    back = np.cross(up, left)
    R = np.array([left, back, up])
    if np.allclose(R, np.eye(3), atol=1e-3) and abs(k - 1) < 0.05:
        return J, meshes
    R = R * k
    toe = R @ (np.asarray(J["L_toe"]) - np.asarray(J["L_foot"]))
    if toe[1] > 0:
        print("warning: the toes point backwards after turning upright: a mirrored rip?")
    print("turned upright (the rip's up was %s), scaled x%.3f" % (np.round(up, 2), k))
    J = {k: R @ np.asarray(v) for k, v in J.items()}
    for m in meshes:
        m["co"] = np.asarray(m["co"]) @ R.T
        m["loop_n"] = np.asarray(m["loop_n"]) @ (R / k).T
    return J, meshes


def split_parts(meshes):
    """Cut each mesh into its connected parts (vertices at the same spot count as
    connected): [(part id, mesh)], ids are "<mesh>#<n>", biggest part first."""
    out = []
    for m in meshes:
        parent = list(range(len(m["co"])))

        def find(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x
        seen = {}
        for vi, c in enumerate(m["co"]):
            k = tuple(np.round(c, 2))
            if k in seen:
                parent[find(vi)] = find(seen[k])
            else:
                seen[k] = vi
        for tri in m["tris"]:
            for vi, _ in tri[1:]:
                parent[find(vi)] = find(tri[0][0])
        groups = {}
        for tri in m["tris"]:
            groups.setdefault(find(tri[0][0]), []).append(tri)
        for n, tris in enumerate(sorted(groups.values(), key=len, reverse=True)):
            part = dict(m)
            part["tris"] = tris
            part["part"] = "%s#%d" % (m["name"], n)
            out.append(part)
    return out


def part_volume(p):
    """Signed volume of a part around its centre: an outline shell (the toon 'inverted
    hull' the old games draw black around a mesh) has its faces turned inside out."""
    co = np.asarray(p["co"])
    tris = np.array([[vi for vi, _ in t] for t in p["tris"]])
    c = co[np.unique(tris)].mean(0)
    a, b, d = co[tris[:, 0]] - c, co[tris[:, 1]] - c, co[tris[:, 2]] - c
    return float(np.einsum("ij,ij->i", a, np.cross(b, d)).sum() / 6)


def outline_shells(parts):
    """Part ids that are outline shells: an inside-out copy of another part (same
    triangle count, same place and size) that faces the right way."""
    info = []
    for p in parts:
        vs = sorted({vi for t in p["tris"] for vi, _ in t})
        pts = np.asarray(p["co"])[vs]
        info.append((p["part"], len(p["tris"]), pts.min(0), pts.max(0), part_volume(p)))
    shells = set()
    for i, (pid, n, lo, hi, vol) in enumerate(info):
        if vol >= 0:
            continue
        for j, (qid, m, lo2, hi2, vol2) in enumerate(info):
            if i == j or m != n or vol2 <= 0:
                continue
            size, size2 = hi - lo, hi2 - lo2
            if (np.linalg.norm((lo + hi) / 2 - (lo2 + hi2) / 2) < 1.0
                    and np.all(np.abs(size - size2) <= 0.3 * np.maximum(size2, 0.5))):
                shells.add(pid)
                break
    return shells


WEAPON_HINTS = ("katana", "weapon", "kunai", "kagi", "saya", "w_15", "_kan")


def held_items(parts, parents):
    """Part ids of things held in the hands (kunai, swords, claws, books): parts on one
    hand bone only (or a weapon bone below it) that are not the hand itself, grouped with
    the ones touching them; a group that sticks out of the hand is in the hand. Small
    things on the hand (a glove plate) stay."""
    def side_of(bone):
        b = bone
        while b is not None:
            if b in ("L_hand", "R_hand"):
                return b[0]
            if any(h in b.lower() for h in WEAPON_HINTS):
                lo = b.lower()
                if "_l" in lo or lo.startswith("l_") or "l_" in lo or lo.endswith("l"):
                    return "L"
                if "_r" in lo or lo.startswith("r_") or "r_" in lo or lo.endswith("r"):
                    return "R"
            b = parents.get(b)
        return None

    info, hand_box = [], {}
    rig_fingers = any(is_finger(b) for b in parents)     # some rigs have mitts, no fingers
    for p in parts:
        vs = sorted({vi for t in p["tris"] for vi, _ in t})
        pts = np.asarray(p["co"])[vs]
        bones = {b for v in vs for b, w in p["weights"][v] if w > 1e-3}
        sides = {side_of(b) for b in bones}
        fingers = any(is_finger(b) for b in bones)
        lo, hi = pts.min(0), pts.max(0)
        arms = any(b[2:] in ("larm", "harm") for b in bones)
        if fingers or arms or (not rig_fingers and len(p["tris"]) > 150):
            # a hand (maybe with its forearm): the box of its vertices mostly on the hand
            for sd in "LR":
                hv = [v for v in vs if max(p["weights"][v], key=lambda bw: bw[1])[0] in
                      (sd + "_hand",) or is_finger(max(p["weights"][v], key=lambda bw: bw[1])[0])
                      and max(p["weights"][v], key=lambda bw: bw[1])[0].startswith(sd)]
                if len(hv) < 8:
                    continue
                hp = np.asarray(p["co"])[hv]
                box = hand_box.get(sd)
                hlo, hhi = hp.min(0), hp.max(0)
                hand_box[sd] = (hlo, hhi) if box is None else (np.minimum(box[0], hlo), np.maximum(box[1], hhi))
        elif len(sides) == 1 and None not in sides:
            # on a weapon bone hanging off the hand (not the hand itself): a grip, a claw
            weapon = not any(b in ("L_hand", "R_hand") or is_finger(b) for b in bones)
            info.append([p["part"], sides.pop(), lo, hi, weapon])
    held = set()
    for sd in "LR":
        box = hand_box.get(sd)
        # what lies flat on the hand inside its outline (glove plates) is part of it
        held.update(x[0] for x in info if x[1] == sd and x[4])
        items = [x for x in info if x[1] == sd and not x[4] and not (
            box is not None and np.all(x[2] >= box[0] - 0.3) and np.all(x[3] <= box[1] + 0.3))]
        if not items:
            continue
        # group the small parts that touch each other
        group = list(range(len(items)))

        def root(i):
            while group[i] != i:
                group[i] = group[group[i]]
                i = group[i]
            return i
        for i in range(len(items)):
            for j in range(i + 1, len(items)):
                a, b = items[i], items[j]
                if np.all(a[2] <= b[3] + 0.4) and np.all(b[2] <= a[3] + 0.4):
                    group[root(i)] = root(j)
        clusters = {}
        for i in range(len(items)):
            clusters.setdefault(root(i), []).append(items[i])
        for members in clusters.values():
            lo = np.min([m[2] for m in members], axis=0)
            hi = np.max([m[3] for m in members], axis=0)
            out = 99.0 if box is None else max(float(np.max(box[0] - lo)), float(np.max(hi - box[1])))
            if out > 2.0:
                held.update(m[0] for m in members)
    return held


def describe_part(p):
    vs = sorted({vi for t in p["tris"] for vi, _ in t})
    pts = np.array([p["co"][v] for v in vs])
    bones = {}
    for v in vs:
        for b, w in p["weights"][v]:
            bones[b] = bones.get(b, 0) + w
    top = ", ".join("%s %.0f" % kv for kv in sorted(bones.items(), key=lambda x: -x[1])[:3])
    return "%-28s %5d tris  min %s max %s  %s" % (
        p["part"], len(p["tris"]), np.round(pts.min(0), 1), np.round(pts.max(0), 1), top)


# ---------------------------------------------------------------- the fit

def stock_joints(gla):
    return {b["name"]: np.array([b["base"][0][3], b["base"][1][3], b["base"][2][3]])
            for b in gla.bones}


class TorsoMap:
    """x' = sx(z) x + ox(z), y' = sy y + oy(z), z' = g(z): g takes the hip joints and the
    shoulder joints exactly onto the stock ones (linear in between); above the shoulders
    the old rigs' necks don't line up with _humanoid's cervical, so neck and head are just
    scaled with the shoulders' width. The lateral scale / offsets are interpolated between
    the hip and the shoulder levels. at(p) gives the local affine 4x4."""

    def __init__(self, J, T):
        mid = lambda a, b: (a + b) / 2
        sh, th = mid(J["L_hleg"], J["R_hleg"]), mid(T["lfemurYZ"], T["rfemurYZ"])
        ss, ts = mid(J["L_harm"], J["R_harm"]), mid(T["lhumerus"], T["rhumerus"])
        self.sx = np.array([(T["lfemurYZ"][0] - T["rfemurYZ"][0]) / (J["L_hleg"][0] - J["R_hleg"][0]),
                            (T["lhumerus"][0] - T["rhumerus"][0]) / (J["L_harm"][0] - J["R_harm"][0])])
        self.sy = float(self.sx[1])
        self.ox = np.array([th[0] - self.sx[0] * sh[0], ts[0] - self.sx[1] * ss[0]])
        self.oy = np.array([th[1] - self.sy * sh[1], ts[1] - self.sy * ss[1]])
        self.zs = np.array([sh[2], ss[2]])
        self.zt = np.array([th[2], ts[2]])
        self.slopes = np.array([(ts[2] - th[2]) / (ss[2] - sh[2]), self.sx[1]])
        self.head_scale = float(self.sx[1])
        neck = self.point(J["neck"])
        self.report = ("torso: hips x%.2f, shoulders x%.2f wide; hips-shoulders x%.2f tall; "
                       "neck joint lands %.1f from cervical"
                       % (self.sx[0], self.sx[1], self.slopes[0], neck[2] - T["cervical"][2]))

    def _z(self, z):
        zs, zt, k = self.zs, self.zt, self.slopes
        if z <= zs[1]:
            return zt[0] + (z - zs[0]) * k[0], k[0]
        return zt[1] + (z - zs[1]) * k[1], k[1]

    def _t(self, z):
        t = (z - self.zs[0]) / (self.zs[1] - self.zs[0])
        return min(max(t, 0.0), 1.0)

    def point(self, p):
        return (self.at(p) @ np.append(p, 1))[:3]

    def at(self, p):
        t = self._t(p[2])
        sx = self.sx[0] + t * (self.sx[1] - self.sx[0])
        sy = self.sy
        ox = self.ox[0] + t * (self.ox[1] - self.ox[0])
        oy = self.oy[0] + t * (self.oy[1] - self.oy[0])
        z, dz = self._z(p[2])
        m = np.eye(4)
        m[0, 0], m[1, 1], m[2, 2] = sx, sy, dz
        m[0, 3] = ox
        m[1, 3] = oy
        m[2, 3] = z - dz * p[2]
        return m


def build_drivers(J, T):
    """Source joints J (CoN names), stock joints T (_humanoid names) ->
    {source bone: 4x4} for the bones that drive; plus a report."""
    D, rep = {}, []
    mid = lambda a, b: (a + b) / 2
    # torso: a height-dependent map (both stand upright facing -Y) that takes the hip
    # joints, the shoulder joints and the neck exactly onto the stock ones, so the
    # limbs start where the torso around them ends up (one affine fit put the
    # shoulders ~1 unit low and the arms, pinned to the stock humerus, stuck up).
    torso = TorsoMap(J, T)
    rep.append(torso.report)
    for n in ("nw4r_root", "Y", "X", "Z", "hip", "waist", "breast", "h_202nuno", "h_203nuno",
              "neck"):
        D[n] = torso
    # neck / head: the old rigs put the neck joint higher than _humanoid's cervical,
    # stretching the neck onto it would make a giraffe; the neck follows the torso map,
    # the head is moved with it and scaled uniformly, only the pivots differ.
    hs = torso.head_scale
    hp = torso.point(J["head"])
    D["head"] = affine(hs * np.eye(3), J["head"], hp)
    d = hp - T["cranium"]
    rep.append("head scale %.2f, head joint lands %.1f %.1f %.1f from cranium" % (hs, *d))
    for s in "lr":
        S = s.upper() + "_"
        D[S + "collar"], _, k1 = segment(J[S + "collar"], J[S + "harm"], T[s + "clavical"], T[s + "humerus"])
        D[S + "harm"], _, k2 = segment(J[S + "harm"], J[S + "larm"], T[s + "humerus"], T[s + "radius"])
        D[S + "larm"], rf, k3 = segment(J[S + "larm"], J[S + "hand"], T[s + "radius"], T[s + "hand"])
        if S + "finHi01" in J and S + "finKu01" in J:
            sk = mid(J[S + "finHi01"], J[S + "finKu01"])
            tk = mid(T[s + "_d2_j1"], T[s + "_d4_j1"])
            D[S + "hand"], _, k4 = segment(J[S + "hand"], sk, T[s + "hand"], tk, (0.8, 1.25))
        else:                                   # a rig without finger bones (mitts)
            D[S + "hand"], k4 = affine(rf, J[S + "hand"], T[s + "hand"]), 1.0
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


def top4(out, gla):
    w = sorted(((gla.index[b], v) for b, v in out.items() if v > 1e-3), key=lambda x: -x[1])[:4]
    return w or [(gla.index["pelvis"], 1.0)]


# _humanoid bones the torso transfer may hand out; the robe tails of jedi_hm go to
# the thighs they hang from
CORE = ("pelvis", "lower_lumbar", "upper_lumbar", "thoracic", "cervical",
        "lclavical", "rclavical", "lfemurYZ", "rfemurYZ")
TAIL = {"ltail": "lfemurYZ", "rtail": "rfemurYZ"}


class StockWeights:
    """The weights of a stock model's torso and hips (the surfaces its skin shows),
    sampled densely over its triangles, for the torso part of a converted mesh: in the
    bind pose both sit on the same skeleton, so each vertex takes the weights of the
    stock surface next to it. Those were painted for these animations (twisting
    lumbar / thoracic, the aim turning the torso); a weight split by height alone twists
    the torso like a candy wrapper."""

    def __init__(self, glm, skin, gla, parts=("torso", "hips", "l_leg", "r_leg"), k=8, n=5):
        from scipy.spatial import cKDTree
        on = None
        if skin and os.path.exists(skin):
            on = set()
            for line in open(skin, encoding="latin-1"):
                if "," in line:
                    name, sh = line.strip().split(",", 1)
                    if sh.strip().lower() != "*off":
                        on.add(name.lower())
        _, surfs = g2file.read_glm(glm)
        pts, ws, used = [], [], []
        nb = gla.num_bones
        tail = {gla.index[a]: gla.index[b] for a, b in TAIL.items()}
        bary = [(i / n, j / n) for i in range(n + 1) for j in range(n + 1 - i)]
        for sf in surfs:
            nm = sf.name.lower()
            if sf.flags & (g2file.SURF_BOLT | g2file.SURF_OFF) or not sf.tris or "_cap_" in nm:
                continue
            if not nm.startswith(parts) or (on is not None and nm not in on):
                continue
            used.append(sf.name)
            V = np.array(sf.verts)
            W = np.zeros((len(V), nb))
            for i, vw in enumerate(sf.weights):
                for b, w in vw:
                    W[i, tail.get(b, b)] += w
            for a, b, c in sf.tris:
                for u, v in bary:
                    t = 1 - u - v
                    pts.append(u * V[a] + v * V[b] + t * V[c])
                    ws.append(u * W[a] + v * W[b] + t * W[c])
        if not pts:
            raise SystemExit("no torso / hips surfaces in %s" % glm)
        self.tree = cKDTree(np.array(pts))
        self.W = np.array(ws)
        self.k = k
        self.gla = gla
        self.core = set(CORE)
        self.report = "torso weights from %s (%s)" % (os.path.basename(glm), ", ".join(used))

    def blend(self, p, raw):
        """raw: [{stock bone name: w}] per vertex at bent positions p; the share of each
        vertex's weight on torso bones (CORE) is replaced by the stock weights there."""
        names = [b["name"] for b in self.gla.bones]
        out = []
        idx = [i for i, o in enumerate(raw) if any(b in self.core for b in o)]
        if not idx:
            return raw
        d, nn = self.tree.query(p[idx], k=self.k)
        wq = 1.0 / (d + 0.25)
        wq /= wq.sum(axis=1, keepdims=True)
        stockw = np.einsum("nk,nkb->nb", wq, self.W[nn])
        by = dict(zip(idx, stockw))
        for i, o in enumerate(raw):
            if i not in by:
                out.append(o)
                continue
            tot = sum(o.values()) or 1.0
            a = sum(w for b, w in o.items() if b in self.core) / tot
            sw = by[i]
            sw = sw / (sw.sum() or 1.0)
            new = {b: w / tot for b, w in o.items() if b not in self.core}
            for bi in np.nonzero(sw > 1e-3)[0]:
                new[names[bi]] = new.get(names[bi], 0) + a * float(sw[bi])
            out.append(new)
        return out


def convert(src_meshes, src_parents, J, gla, rig, stock=None):
    T = stock_joints(gla)
    D, report = build_drivers(J, T)
    wmap = expand_weights(rig, J.keys())
    warned = set()

    def mapped(b):
        """The stock bone for a source bone; one the table doesn't know goes with its
        nearest mapped parent (hair -> head, shoulder pads -> collar)."""
        x = b
        while x is not None and x not in wmap:
            x = src_parents.get(x)
        if x is None:
            x = "hip"
        if x != b and b not in warned:
            warned.add(b)
            print("note: %s not in the table, weights go with %s (%s)" % (b, x, wmap[x]))
        return wmap[x]
    surfaces = []
    for m in src_meshes:
        used = {b for t in m["tris"] for vi, _ in t for b, _ in m["weights"][vi]}
        prop = m.get("prop") or (len(used) == 1 and is_finger(next(iter(used))))
        if prop:
            # a thing held in the hand (Asuma's trench knives, Kakashi's kunai): rigid
            # on the hand, not on a finger the animations curl
            hand = next(iter(used))[0].lower() + "hand"
            print("%s: held prop, rigid on %s" % (m["name"], hand))
        co = m["co"]
        n_v = len(co)
        M = np.zeros((n_v, 4, 4))
        final, raw = [], []
        for i, ws in enumerate(m["weights"]):
            tot = sum(w for _, w in ws) or 1.0
            acc = np.zeros((4, 4))
            for b, w in ws:
                d = driver_of(b, src_parents, D)
                acc += (w / tot) * (d.at(co[i]) if isinstance(d, TorsoMap) else d)
            if not ws:
                acc = D["hip"].at(co[i])
            M[i] = acc
        p = np.einsum("nij,nj->ni", M[:, :3, :3], co) + M[:, :3, 3]
        for i, ws in enumerate(m["weights"]):
            tot = sum(w for _, w in ws) or 1.0
            out = {}
            for b, w in ws:
                tgt = hand if prop else mapped(b)
                if tgt == "SPINE":
                    for sb, sw in spine_split(p[i][2], T).items():
                        out[sb] = out.get(sb, 0) + w / tot * sw
                else:
                    out[tgt] = out.get(tgt, 0) + w / tot
            raw.append(out)
        if stock is not None and not prop:
            raw = stock.blend(p, raw)
        for out in raw:
            final.append(top4(out, gla))
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


def tag_surfaces(path, gla, tags_gla=None):
    """The bolt surfaces ('*' names) of a stock model on the same bones. When our
    skeleton has other bone lengths (tags_gla = the one the stock model sits on), each
    tag vertex moves with the joints its bones moved: *r_hand stays in the hand."""
    head, surfs = g2file.read_glm(path)
    if head["num_bones"] != gla.num_bones:
        raise SystemExit("%s is not on this skeleton" % path)
    tags = [s for s in surfs if s.flags & g2file.SURF_BOLT]
    if tags_gla is not None:
        old, new = stock_joints(tags_gla), stock_joints(gla)
        names = [b["name"] for b in gla.bones]
        moved = 0
        for t in tags:
            for k, (v, ws) in enumerate(zip(t.verts, t.weights)):
                dv = sum(w * (new[names[b]] - old[names[b]]) for b, w in ws)
                if np.linalg.norm(dv) > 1e-6:
                    t.verts[k] = tuple(float(x) for x in np.asarray(v) + dv)
                    moved += 1
        if moved:
            print("tags: %d vertices moved with the bones" % moved)
    return tags


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("blend")
    ap.add_argument("--gla", required=True, help="_humanoid.gla")
    ap.add_argument("--tags", required=True, help="stock .glm to take the tag surfaces from")
    ap.add_argument("--stock-gla", help="the stock _humanoid.gla, when --gla is one with other "
                    "bone lengths (make_skeleton.py): the tags move with the bones")
    ap.add_argument("--stock-skin", help="its .skin (which torso / hips surfaces are shown); "
                    "default: model_default.skin next to it")
    ap.add_argument("--no-stock-weights", action="store_true",
                    help="keep the height-split torso weights instead of the stock model's")
    ap.add_argument("--textures", required=True, help="folder with the textures the materials use")
    ap.add_argument("--name", help="player model name (default: the .blend's name, lower case)")
    ap.add_argument("--out", default=".", help="writes <out>/models/players/<name>/")
    ap.add_argument("--title", help="the name on the character screen (default: the model name, capitalised)")
    ap.add_argument("--female", action="store_true", help="a woman (the character screen sets sex female)")
    ap.add_argument("--armature", help="source armature (default: the one the meshes use)")
    ap.add_argument("--drop", default="", help="comma-separated mesh objects to leave out")
    ap.add_argument("--parts", action="store_true", help="list the connected parts and stop")
    ap.add_argument("--flip-v", action="store_true", help=".smd whose texture v runs the other way "
                    "(the face comes out dark / textures scrambled; Sasuke's rip)")
    ap.add_argument("--height", type=float, help="the character's height in cm (Naruto databooks); "
                    "the game draws him at height / %d of the stock size, hitbox included" % ADULT_CM)
    ap.add_argument("--drop-parts", default="", help="comma-separated part ids (see --parts) to leave out")
    ap.add_argument("--drop-held", action="store_true", help="leave out what the hands hold "
                    "(kunai, swords, claws, books: found by themselves; --parts shows them)")
    ap.add_argument("--hard-angle", type=float, default=60.0,
                    help="flat-shaded rips: corners whose normals differ more than this (deg) "
                    "stay a hard edge (two game vertices); higher = fewer vertices")
    ap.add_argument("--keep-shells", action="store_true", help="keep the outline shells (inside-out "
                    "copies the old games draw black around a mesh; left out by default)")
    ap.add_argument("--prop-parts", default="", help="part ids that are held props: rigid on their "
                    "hand, off in model_default.skin, on in model_props.skin")
    a = ap.parse_args(argv)

    name = (a.name or os.path.splitext(os.path.basename(a.blend))[0]).lower()
    gla = g2file.Gla(a.gla)
    if a.blend.lower().endswith(".smd"):
        J, parents, meshes = load_smd(a.blend, a.textures, flip_v=a.flip_v, hard_angle=a.hard_angle)
    else:
        J, parents, meshes = load_blend(a.blend, a.armature)
    J, meshes = upright(J, meshes)
    drop = {d for d in a.drop.split(",") if d}
    meshes = [m for m in meshes if m["name"] not in drop]
    if True:
        parts = split_parts(meshes)
        shells = set() if a.keep_shells else outline_shells(parts)
        held = held_items([p for p in parts if p["part"] not in shells], parents)
        if a.parts:
            for p in parts:
                print(describe_part(p) + ("   <- outline shell" if p["part"] in shells else "")
                      + ("   <- held" if p["part"] in held else ""))
            return None
        gone = {d for d in a.drop_parts.split(",") if d} | shells | (held if a.drop_held else set())
        props = {d for d in a.prop_parts.split(",") if d}
        unknown = (gone | props) - {p["part"] for p in parts}
        if unknown:
            raise SystemExit("no such parts: %s" % ", ".join(sorted(unknown)))
        keep, extra = {}, []
        for p in parts:
            if p["part"] in gone:
                print("left out:", describe_part(p), "(outline shell)" if p["part"] in shells
                      else "(held)" if p["part"] in held else "")
            elif p["part"] in props:
                print("prop:    ", describe_part(p))
                extra.append(dict(p, name=p["part"], prop=True))
            else:
                keep.setdefault(p["name"], []).extend(p["tris"])
        meshes = [dict(m, tris=keep[m["name"]]) for m in meshes if m["name"] in keep] + extra

    stock = None
    if not a.no_stock_weights:
        skin = a.stock_skin or os.path.join(os.path.dirname(a.tags), "model_default.skin")
        if not os.path.exists(skin):
            alt = a.tags[:-len("model.glm")] + "model_default.skin" if a.tags.endswith("model.glm") else ""
            skin = alt if alt and os.path.exists(alt) else None
        stock = StockWeights(a.tags, skin, gla)
        print(stock.report)
    conv, report, T = convert(meshes, parents, J, gla, CON_RIG, stock)
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
    for t in tag_surfaces(a.tags, gla, g2file.Gla(a.stock_gla) if a.stock_gla else None):
        t.parent = 0
        t.shader = ""
        surfaces.append(t)
    g2file.write_glm(os.path.join(outdir, "model.glm"), gamedir + "/model",
                     gla.name, gla.num_bones, surfaces)
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
    # the character screen lists every player model with this file: its title, then "female" for a woman
    with open(os.path.join(outdir, "shinobi.char"), "w", newline="\n") as f:
        f.write("%s\n%s" % (a.title or name.capitalize(), "female\n" if a.female else ""))
    scale_file = os.path.join(outdir, "scale.cfg")
    if a.height:
        pct = int(round(100 * a.height / ADULT_CM))
        pct = min(max(pct, 50), 150)
        with open(scale_file, "w", newline="\n") as f:
            f.write("// JK_Shinobi: model and hitbox size in %% of the stock size (%g cm / %d cm)\n"
                    "scale %d\n" % (a.height, ADULT_CM, pct))
        print("scale %d%% (%g cm)" % (pct, a.height))
    elif os.path.exists(scale_file):
        os.remove(scale_file)
    nv = sum(len(s.verts) for s in mesh_surfs)
    nt = sum(len(s.tris) for s in mesh_surfs)
    print("wrote %s: %d mesh surfaces, %d verts, %d tris, %d tags"
          % (outdir, len(mesh_surfs), nv, nt, len(surfaces) - 1 - len(mesh_surfs)))
    nall = sum(len(s.verts) for s in surfaces)
    if nall > VERT_BUDGET:
        print("WARNING: %d verts (with tags and props) is over %d: Ghoul2 traces and saber "
              "marks on it run out of the engine's 1 MB transform space (20 bytes a vertex) "
              "and the game drops with 'Ran out of transform space'. Make it lighter."
              % (nall, VERT_BUDGET))
    elif nall > VERT_STOCK:
        print("note: %d verts: too heavy for stock OpenJK (256 KB transform space), fine on "
              "the mod's engine build" % nall)
    return outdir


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--" in args:
        args = args[args.index("--") + 1:]
    main(args)
