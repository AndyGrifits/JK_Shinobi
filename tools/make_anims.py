#!/usr/bin/env python3
"""JK_Shinobi: our own animations, written straight into the _humanoid skeletons.

    python3 tools/make_anims.py --gla _humanoid.gla --cfg animation.cfg \
        --out-gla out/_humanoid.gla --out-cfg out/animation.cfg [--preview DIR]

Each animation (tools/anim_defs.py) is a few key poses on top of a stock frame: the
pelvis moved / turned, the spine bent and twisted, hands and feet put where they should
be (two-bone IK, a pole for the elbow / knee), the hands and feet turned, the head
turned. Between the keys it eases. The frames are appended to the .gla (the stock ones
stay where they are, so every model keeps working) and the names to animation.cfg; the
same frame numbers must go into every skeleton the models use (_humanoid and
_humanoid_ninja), so run it once on each with the same definitions.

Model space: +X = the model's left, -Y = forward, +Z = up (the bind pose: an A-pose,
the feet at z ~0). A frame matrix is relative: final[bone] = final[parent] x frame[bone]
(g2file), final = posed bone matrix x inverse(bind bone matrix).
"""
import argparse
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import g2file  # noqa: E402
from make_skeleton import decompress, compress  # noqa: E402

FWD = np.array([0.0, -1.0, 0.0])
LEFT = np.array([1.0, 0.0, 0.0])
UP = np.array([0.0, 0.0, 1.0])


# ------------------------------------------------------------------ the skeleton

class Skel:
    def __init__(self, path, keep_frames=None):
        self.gla = g = g2file.Gla(path)
        d = g.data
        nb, nf = g.num_bones, g.num_frames
        raw = np.frombuffer(d[g.ofs_frames:g.ofs_frames + nf * nb * 3], dtype=np.uint8)
        raw = raw.reshape(nf, nb, 3).astype(np.int64)
        self.idx = raw[:, :, 0] | (raw[:, :, 1] << 8) | (raw[:, :, 2] << 16)
        if keep_frames is not None and keep_frames < nf:  # (ours from an earlier run: dropped)
            self.idx = self.idx[:keep_frames]
            nf = keep_frames
        used = int(self.idx.max()) + 1                    # (and their bones at the pool's end)
        self.pool = np.frombuffer(d[g.ofs_pool:g.ofs_pool + used * 14], dtype="<u2").reshape(-1, 7).copy()
        self.nb, self.nf = nb, nf
        self.parent = [b["parent"] for b in g.bones]
        self.name = [b["name"] for b in g.bones]
        self.index = g.index
        self.base = np.array([np.vstack([np.array(b["base"]), [0, 0, 0, 1]]) for b in g.bones])
        self.base_inv = np.linalg.inv(self.base)
        self.order = []                       # parents before children
        seen = set()

        def visit(i):
            if i in seen:
                return
            if self.parent[i] >= 0:
                visit(self.parent[i])
            seen.add(i)
            self.order.append(i)
        for i in range(nb):
            visit(i)
        self.children = [[] for _ in range(nb)]
        for i, p in enumerate(self.parent):
            if p >= 0:
                self.children[p].append(i)

    def rel(self, frame):
        m = decompress(self.pool[self.idx[frame]])
        out = np.tile(np.eye(4), (self.nb, 1, 1))
        out[:, :3, :] = m
        return out

    def finals(self, frame):
        rel = self.rel(frame)
        fin = np.zeros_like(rel)
        for i in self.order:
            p = self.parent[i]
            fin[i] = rel[i] if p < 0 else fin[p] @ rel[i]
        return fin

    def posed(self, frame):
        """(nb, 4, 4) bone matrices in model space (axes + the joint position)."""
        return self.finals(frame) @ self.base

    def rel_to_posed(self, rel):
        fin = np.zeros_like(rel)
        for i in self.order:
            p = self.parent[i]
            fin[i] = rel[i] if p < 0 else fin[p] @ rel[i]
        return fin @ self.base

    def to_rel(self, G):
        fin = G @ self.base_inv
        rel = np.zeros_like(fin)
        for i in range(self.nb):
            p = self.parent[i]
            rel[i] = fin[i] if p < 0 else np.linalg.inv(fin[p]) @ fin[i]
        return rel

    def subtree(self, i):
        out, st = [], [i]
        while st:
            j = st.pop()
            out.append(j)
            st.extend(self.children[j])
        return out


def rot(axis, deg):
    a = np.asarray(axis, dtype=float)
    a = a / np.linalg.norm(a)
    t = np.radians(deg)
    K = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3) + np.sin(t) * K + (1 - np.cos(t)) * K @ K


def rot_between(u, v):
    u = u / np.linalg.norm(u)
    v = v / np.linalg.norm(v)
    c = np.dot(u, v)
    ax = np.cross(u, v)
    s = np.linalg.norm(ax)
    if s < 1e-8:
        if c > 0:
            return np.eye(3)
        perp = np.cross(u, LEFT if abs(u[0]) < 0.9 else UP)
        return rot(perp, 180)
    return rot(ax, np.degrees(np.arctan2(s, c)))


# ------------------------------------------------------------------ posing

class Pose:
    """Posed bone matrices of one frame, changed by the operations below."""

    def __init__(self, skel, G):
        self.s = skel
        self.G = G.copy()

    def pos(self, name):
        return self.G[self.s.index[name], :3, 3].copy()

    def group(self, name):
        """The bones that move with this one: its subtree; a hand also takes the fingers
        and the hang tag (their parent is the forearm)."""
        s = self.s
        i = s.index[name]
        bones = s.subtree(i)
        if name in ("lhand", "rhand"):
            side = name[0]
            bones += [j for j, n in enumerate(s.name)
                      if n.startswith(side + "_d") or n == side + "hang_tag_bone"]
        return sorted(set(bones))

    def turn(self, bones, R, pivot):
        T = np.eye(4)
        T[:3, :3] = R
        T[:3, 3] = pivot - R @ pivot
        for j in bones:
            self.G[j] = T @ self.G[j]

    def turn_bone(self, name, R, pivot=None):
        self.turn(self.group(name), R, self.pos(name) if pivot is None else pivot)

    def move_all(self, d):
        self.G[:, :3, 3] += d

    def two_bone(self, upper, lower, end, target, pole):
        """Upper / lower / end joints (e.g. humerus, radius, hand): put the end at target,
        the middle joint bent towards pole."""
        A, B, C = self.pos(upper), self.pos(lower), self.pos(end)
        a, b = np.linalg.norm(B - A), np.linalg.norm(C - B)
        t = np.asarray(target, dtype=float)
        d = t - A
        dist = np.clip(np.linalg.norm(d), abs(a - b) + 1e-3, a + b - 1e-3)
        n = d / np.linalg.norm(d)
        x = (a * a - b * b + dist * dist) / (2 * dist)
        h = np.sqrt(max(a * a - x * x, 0.0))
        p = np.asarray(pole, dtype=float) - A
        p = p - np.dot(p, n) * n
        if np.linalg.norm(p) < 1e-6:
            p = np.cross(n, UP)
        p = p / np.linalg.norm(p)
        Bn = A + n * x + p * h
        self.turn_bone(upper, rot_between(B - A, Bn - A), A)
        C1 = self.pos(end)
        self.turn_bone(lower, rot_between(C1 - Bn, (A + n * dist) - Bn), Bn)

    def aim_axes(self, name, fwd_now, fwd_want, up_now=None, up_want=None):
        """Turn a bone's group so a direction it carries (fwd_now) points along fwd_want,
        then, about that, up_now towards up_want."""
        R = rot_between(fwd_now, fwd_want)
        self.turn_bone(name, R)
        if up_now is not None and up_want is not None:
            u = R @ up_now
            f = fwd_want / np.linalg.norm(fwd_want)
            u = u - np.dot(u, f) * f
            w = np.asarray(up_want, dtype=float) - np.dot(up_want, f) * f
            if np.linalg.norm(u) > 1e-6 and np.linalg.norm(w) > 1e-6:
                self.turn_bone(name, rot_between(u, w))

    def bone_dir(self, a, b):
        return self.pos(b) - self.pos(a)


# the hand: palm normal and finger direction in its own (bind) axes, found from the
# bind pose (the palm faces the thigh in the A-pose: -X for the left hand, +X for the right)
def hand_frame(pose, side):
    s = side
    wrist = pose.pos(s + "hand")
    fingers = (pose.pos(s + "_d2_j1") + pose.pos(s + "_d4_j1")) / 2 - wrist
    thumb = pose.pos(s + "_d1_j1") - wrist
    f = fingers / np.linalg.norm(fingers)
    palm = np.cross(f, thumb) if s == "l" else np.cross(thumb, f)
    palm = palm - np.dot(palm, f) * f
    return f, palm / np.linalg.norm(palm)


def set_hand(pose, side, finger_dir, palm_dir):
    f, palm = hand_frame(pose, side)
    pose.aim_axes(side + "hand", f, np.asarray(finger_dir, float), palm, np.asarray(palm_dir, float))


def flatten_hand(pose, side, curl=0.0):
    """Fingers straight (0) or curled (1) relative to the hand."""
    s = side
    f, palm = hand_frame(pose, s)
    for a, b in (("_d2_j1", "_d2_j2"), ("_d4_j1", "_d4_j2")):
        j1 = pose.pos(s + a)
        seg = pose.pos(s + b) - j1
        want = f * np.cos(np.radians(80 * curl)) - palm * np.sin(np.radians(80 * curl))
        R = rot_between(seg, want)
        pose.turn([pose.s.index[s + b]], R, j1)


# ------------------------------------------------------------------ the key poses

def ease(t):
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return np.asarray(a, float) * (1 - t) + np.asarray(b, float) * t


def build_pose(skel, G0, k, ref):
    """k: one key pose (dict) -> posed matrices. ref: joints of the reference frame."""
    p = Pose(skel, G0)
    pelvis = p.pos("pelvis")
    # the whole body: lowered / shifted, turned about the pelvis
    if "body_yaw" in k:
        p.turn(list(range(skel.nb)), rot(UP, k["body_yaw"]), pelvis)
    if "body_pitch" in k:  # forward (+): the head goes down, the hips stay
        p.turn(list(range(skel.nb)), rot(rot(UP, k.get("body_yaw", 0)) @ LEFT, k["body_pitch"]), pelvis)
    if "body_roll" in k:   # (+): the left shoulder goes down
        p.turn(list(range(skel.nb)), rot(rot(UP, k.get("body_yaw", 0)) @ FWD, -k["body_roll"]), pelvis)
    if "pelvis" in k:
        p.move_all(np.asarray(k["pelvis"], float))
    # the spine: bend forward (+) / sideways, twist, spread over three joints
    for nm, share in (("lower_lumbar", 0.3), ("upper_lumbar", 0.35), ("thoracic", 0.35)):
        if "spine_twist" in k:
            p.turn_bone(nm, rot(UP, k["spine_twist"] * share))
        if "spine_bend" in k:
            p.turn_bone(nm, rot(rot(UP, k.get("body_yaw", 0)) @ LEFT, k["spine_bend"] * share))
        if "spine_side" in k:
            p.turn_bone(nm, rot(rot(UP, k.get("body_yaw", 0)) @ FWD, k["spine_side"] * share))
    if "head" in k:  # yaw, pitch (down +)
        yaw, pitch = k["head"]
        p.turn_bone("cervical", rot(UP, yaw))
        p.turn_bone("cervical", rot(rot(UP, yaw + k.get("body_yaw", 0)) @ LEFT, pitch))  # (about +X: + tips -Y forward down)
    # legs: feet where asked (relative to the reference feet), the knees towards their poles;
    # or (lleg / rleg) the foot along a direction from the hip, the knee pole from the hip
    for s in "lr":
        if s + "leg" in k:
            hip = p.pos(s + "femurYZ")
            d, reach = k[s + "leg"]
            d = np.asarray(d, float) / np.linalg.norm(d)
            length = np.linalg.norm(p.pos(s + "tibia") - hip) + np.linalg.norm(p.pos(s + "talus") - p.pos(s + "tibia"))
            pole = hip + np.asarray(k.get(s + "knee", FWD * 20), float)
            p.two_bone(s + "femurYZ", s + "tibia", s + "talus", hip + d * length * reach, pole)
            if s + "toes" in k:   # the foot: its (bind) forward towards this
                ti = skel.index[s + "talus"]
                fwd_now = p.G[ti, :3, :3] @ np.linalg.inv(G0[ti, :3, :3]) @ FWD
                p.turn_bone(s + "talus", rot_between(fwd_now, np.asarray(k[s + "toes"], float)))
        elif s + "foot" in k:
            tgt = ref[s + "talus"] + np.asarray(k[s + "foot"], float)
            pole = tgt + np.asarray(k.get(s + "knee", FWD * 20), float) + UP * 15
            p.two_bone(s + "femurYZ", s + "tibia", s + "talus", tgt, pole)
            # the foot back flat, pointing where asked
            yaw = k.get(s + "foot_yaw", 0.0)
            G_ref = G0[skel.index[s + "talus"]]
            Rc = p.G[skel.index[s + "talus"], :3, :3] @ np.linalg.inv(G_ref[:3, :3])  # (scaled axes: not a transpose)
            p.turn_bone(s + "talus", rot(UP, yaw) @ np.linalg.inv(Rc))
    # arms: hands where asked (from the shoulder as it is now, model axes), elbows towards
    # their poles (also from the shoulder)
    for s in "lr":
        if s + "hand" in k or s + "arm" in k or s + "hand_at" in k:
            sh = p.pos(s + "humerus")
            if s + "hand_at" in k:  # absolute (a hand on the floor)
                tgt = np.asarray(k[s + "hand_at"], float)
            elif s + "arm" in k:   # (direction, reach 0..1 of the arm's length): 1 = straight
                d, reach = k[s + "arm"]
                d = np.asarray(d, float) / np.linalg.norm(d)
                length = np.linalg.norm(p.pos(s + "radius") - sh) + np.linalg.norm(p.pos(s + "hand") - p.pos(s + "radius"))
                tgt = sh + d * length * reach
            else:
                tgt = sh + np.asarray(k[s + "hand"], float)
            pole = sh + np.asarray(k.get(s + "elbow", [(1 if s == "l" else -1) * 20, 10, -10]), float)
            p.two_bone(s + "humerus", s + "radius", s + "hand", tgt, pole)
        if s + "palm" in k:
            fdir, pdir = k[s + "palm"]
            set_hand(p, s, fdir, pdir)
        if s + "curl" in k:
            flatten_hand(p, s, k[s + "curl"])
    if "turn" in k:   # the finished pose, turned whole about the pelvis (deg, + = to the model's left)
        p.turn(list(range(skel.nb)), rot(UP, k["turn"]), p.pos("pelvis"))
    return p.G


def interp_keys(keys, t):
    """keys: [(time 0..1, dict)]; numbers and vectors ease between neighbours."""
    if t <= keys[0][0]:
        return dict(keys[0][1])
    for (t0, a), (t1, b) in zip(keys, keys[1:]):
        if t <= t1:
            u = ease((t - t0) / (t1 - t0) if t1 > t0 else 1.0)
            out = {}
            for key in set(a) | set(b):
                if key in a and key in b:
                    va, vb = a[key], b[key]
                    if isinstance(va, tuple) and isinstance(va[0], (list, tuple)):  # (dir, dir)
                        out[key] = tuple(lerp(x, y, u) for x, y in zip(va, vb))
                    else:
                        out[key] = lerp(va, vb, u) if not np.isscalar(va) else va * (1 - u) + vb * u
                else:
                    out[key] = b[key] if key in b else a[key]
            return out
    return dict(keys[-1][1])


def quat_mat(q):
    """(N, 4) w x y z -> (N, 3, 3)."""
    w, x, y, z = q[:, 0], q[:, 1], q[:, 2], q[:, 3]
    m = np.empty((len(q), 3, 3))
    m[:, 0, 0] = 1 - 2 * (y * y + z * z)
    m[:, 0, 1] = 2 * (x * y - z * w)
    m[:, 0, 2] = 2 * (x * z + y * w)
    m[:, 1, 0] = 2 * (x * y + z * w)
    m[:, 1, 1] = 1 - 2 * (x * x + z * z)
    m[:, 1, 2] = 2 * (y * z - x * w)
    m[:, 2, 0] = 2 * (x * z - y * w)
    m[:, 2, 1] = 2 * (y * z + x * w)
    m[:, 2, 2] = 1 - 2 * (x * x + y * y)
    return m


def blend_rel(a, b, u, bind_pos):
    """Two poses' relative matrices (nb, 4, 4) -> between them: every joint's rotation
    slerped, the joint itself (its bind position carried by the matrix) lerped - so a bone
    turns about its own joint, the pelvis too (limbs swing in arcs, whatever the keys were
    made of)."""
    from make_skeleton import quat_from
    qa, qb = quat_from(a[:, :3, :3]), quat_from(b[:, :3, :3])
    dot = np.sum(qa * qb, axis=1)
    qb[dot < 0] *= -1
    dot = np.abs(dot)
    th = np.arccos(np.clip(dot, -1, 1))
    sn = np.sin(th)
    small = sn < 1e-6
    wa = np.where(small, 1 - u, np.sin((1 - u) * th) / np.where(small, 1, sn))
    wb = np.where(small, u, np.sin(u * th) / np.where(small, 1, sn))
    q = qa * wa[:, None] + qb * wb[:, None]
    q /= np.linalg.norm(q, axis=1, keepdims=True)
    out = np.tile(np.eye(4), (len(a), 1, 1))
    R = quat_mat(q)
    ja = np.einsum("nij,nj->ni", a[:, :3, :3], bind_pos) + a[:, :3, 3]
    jb = np.einsum("nij,nj->ni", b[:, :3, :3], bind_pos) + b[:, :3, 3]
    j = ja * (1 - u) + jb * u
    out[:, :3, :3] = R
    out[:, :3, 3] = j - np.einsum("nij,nj->ni", R, bind_pos)
    return out


EASES = {
    "smooth": lambda t: t * t * (3 - 2 * t),
    "linear": lambda t: t,
    "fast": lambda t: 1 - (1 - t) ** 3,      # snaps out, settles (a strike)
    "slow": lambda t: t ** 3,                # winds up slowly, ends fast
}


def make_frames(skel, anim, ref_frame):
    """Each key -> a full pose; the frames between are blended joint by joint. A key is
    (time, pose) or (time, pose, ease): how the motion INTO that key goes."""
    G0 = skel.posed(ref_frame)
    ref = {skel.name[i]: G0[i, :3, 3].copy() for i in range(skel.nb)}
    keys = []
    for key in anim["keys"]:
        t, pose = key[0], key[1]
        ease_name = key[2] if len(key) > 2 else "smooth"
        G = build_pose(skel, G0, pose, ref)
        keys.append((t, skel.to_rel(G), EASES[ease_name], G))
    plant = anim.get("plant", ())   # feet that stay put on the floor through the blends ("l", "r")
    n = anim["frames"]
    out = []
    for f in range(n):
        t = f / (n - 1) if n > 1 else 1.0
        if t <= keys[0][0]:
            out.append(keys[0][1].copy())
            continue
        for (t0, a, _, Ga), (t1, b, ez, Gb) in zip(keys, keys[1:]):
            if t <= t1 + 1e-9:
                u = ez((t - t0) / (t1 - t0)) if t1 > t0 else 1.0
                rel = blend_rel(a, b, u, skel.base[:, :3, 3])
                if plant:   # the blend lifts a planted foot: put it back (IK, the knee where it is)
                    P = Pose(skel, skel.rel_to_posed(rel))
                    for sd in plant:
                        ti = skel.index[sd + "talus"]
                        want = Ga[ti, :3, 3] * (1 - u) + Gb[ti, :3, 3] * u
                        R_was = P.G[ti, :3, :3].copy()
                        knee = P.pos(sd + "tibia")
                        P.two_bone(sd + "femurYZ", sd + "tibia", sd + "talus", want, knee + (knee - P.pos(sd + "femurYZ")) * 0.0)
                        R_now = P.G[ti, :3, :3]
                        P.turn_bone(sd + "talus", R_was @ np.linalg.inv(R_now))  # the foot keeps its turn
                    rel = skel.to_rel(P.G)
                out.append(rel)
                break
        else:
            out.append(keys[-1][1].copy())
    return out


# ------------------------------------------------------------------ writing

def write(skel, src_path, out_path, new_frames):
    """Append frames (each (nb, 4, 4) relative matrices) to the .gla."""
    g = skel.gla
    d = g.data
    nb = skel.nb
    rel = np.array(new_frames)[:, :, :3, :]                       # (n, nb, 3, 4)
    comp = compress(rel.reshape(-1, 3, 4))
    uniq, inv = np.unique(comp, axis=0, return_inverse=True)
    pool = np.vstack([skel.pool, uniq])
    new_idx = (len(skel.pool) + inv.ravel()).reshape(len(new_frames), nb)
    idx = np.vstack([skel.idx, new_idx])
    if len(pool) >= 1 << 24:
        raise SystemExit("too many bones in the pool")
    nf = len(idx)
    io = np.empty((nf, nb, 3), dtype=np.uint8)
    io[:, :, 0] = idx & 0xFF
    io[:, :, 1] = (idx >> 8) & 0xFF
    io[:, :, 2] = (idx >> 16) & 0xFF
    head = bytearray(d[:g.ofs_frames])
    frames = io.tobytes()
    pad = (-(g.ofs_frames + len(frames))) % 4
    ofs_pool = g.ofs_frames + len(frames) + pad
    out = head + frames + b"\0" * pad + pool.astype("<u2").tobytes()
    out = bytearray(out)
    struct.pack_into("<6i", out, 76, nf, g.ofs_frames, nb, ofs_pool, g.ofs_skel, len(out))
    with open(out_path, "wb") as f:
        f.write(out)
    return skel.nf, nf


def read_cfg_lines(path):
    return open(path, encoding="latin-1").read().splitlines()


def cfg_frames(lines):
    anims = {}
    for line in lines:
        if line.startswith("// JK_Shinobi: our own animations"):
            break
        p = line.split()
        if len(p) >= 5 and p[0].isupper() and p[1].lstrip("-").isdigit():
            anims[p[0]] = (int(p[1]), int(p[2]), int(p[3]), int(p[4]))
    return anims


def main():
    import anim_defs
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--gla", nargs="+", required=True, help="skeletons: each gets the same frames")
    ap.add_argument("--out-gla", nargs="+", required=True)
    ap.add_argument("--cfg", required=True)
    ap.add_argument("--out-cfg", required=True)
    ap.add_argument("--only", help="comma list of animation names (default: all)")
    a = ap.parse_args()
    lines = read_cfg_lines(a.cfg)
    stock = cfg_frames(lines)
    stock_end = max(f + n for f, n, _, _ in stock.values())  # the stock frames end here
    names = [n for n in anim_defs.ANIMS if not a.only or n in a.only.split(",")]
    first_new = None
    for src, dst in zip(a.gla, a.out_gla):
        skel = Skel(src, keep_frames=stock_end)
        if first_new is None:
            first_new = skel.nf
        elif skel.nf != first_new:
            raise SystemExit("%s has %d frames, the first skeleton %d" % (src, skel.nf, first_new))
        frames, table = [], []
        for nm in names:
            anim = anim_defs.ANIMS[nm]
            ref = stock[anim["ref"][0]][0] + anim["ref"][1]
            fr = make_frames(skel, anim, ref)
            table.append((nm, skel.nf + len(frames), len(fr), anim.get("loop", -1), anim.get("fps", 20)))
            frames += fr
        old, new = write(skel, src, dst, frames)
        print("%s: %d stock frames + %d new -> %s" % (src, old, new - old, dst))
    out = []
    for ln in lines:
        if ln.startswith("// JK_Shinobi: our own animations"):
            break                                        # (an earlier run's list: replaced)
        out.append(ln)
    while out and not out[-1].strip():
        out.pop()
    out.append("")
    out.append("// JK_Shinobi: our own animations (tools/make_anims.py, tools/anim_defs.py)")
    for nm, first, n, loop, fps in table:
        out.append("%-32s %6d %4d %4d %4d" % (nm, first, n, loop, fps))
    open(a.out_cfg, "w", encoding="latin-1", newline="\n").write("\n".join(out) + "\n")
    print("animation.cfg: %d added" % len(table))


if __name__ == "__main__":
    main()
