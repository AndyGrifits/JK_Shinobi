#!/usr/bin/env python3
"""JK_Shinobi: Ghoul2 file formats (.gla skeleton/animation, .glm mesh) in plain Python.

Layouts follow codemp/rd-common/mdx_format.h; skinning follows tr_ghoul2.cpp
(final[child] = final[parent] x frame[child], vertex = sum w * final[bone] x v).
Standard library only (numpy is optional, used by the callers).
"""
import struct

MAX_QPATH = 64
GLA_HEADER = "<4si64sf6i"          # 100 bytes
GLM_HEADER = "<4si64s64s7i"        # 164 bytes
SURF_BOLT = 0x1
SURF_OFF = 0x2
MAX_BONEREFS = 32                  # 5 bits per bone reference
MAX_WEIGHTS = 4


def _cstr(b):
    return b.split(b"\0")[0].decode("latin-1")


def _pad(s, n=MAX_QPATH):
    b = s.encode("latin-1")
    if len(b) >= n:
        raise ValueError("name too long: %s" % s)
    return b + b"\0" * (n - len(b))


def _mat34(vals):
    return [list(vals[0:4]), list(vals[4:8]), list(vals[8:12])]


# ---------------------------------------------------------------- GLA

class Gla:
    """Skeleton + animation. bones[i] = dict(name, flags, parent, base, inv, children);
    base/inv are 3x4 row-major (rotation | translation) in model space."""

    def __init__(self, path):
        d = open(path, "rb").read()
        (ident, ver, name, self.scale, self.num_frames, self.ofs_frames, self.num_bones,
         self.ofs_pool, self.ofs_skel, _) = struct.unpack_from(GLA_HEADER, d, 0)
        if ident != b"2LGA":
            raise ValueError("not a .gla: %s" % path)
        self.name = _cstr(name)
        self.data = d
        hdr = struct.calcsize(GLA_HEADER)
        offs = struct.unpack_from("<%di" % self.num_bones, d, hdr)
        self.bones = []
        for o in offs:
            p = hdr + o
            bname = _cstr(d[p:p + 64])
            flags, parent = struct.unpack_from("<Ii", d, p + 64)
            base = _mat34(struct.unpack_from("<12f", d, p + 72))
            inv = _mat34(struct.unpack_from("<12f", d, p + 120))
            nc, = struct.unpack_from("<i", d, p + 168)
            children = list(struct.unpack_from("<%di" % nc, d, p + 172))
            self.bones.append(dict(name=bname, flags=flags, parent=parent, base=base,
                                   inv=inv, children=children))
        self.index = {b["name"]: i for i, b in enumerate(self.bones)}

    def frame_bone(self, frame, bone):
        """The 3x4 relative matrix of a bone in a frame (MC_UnCompressQuat)."""
        d = self.data
        o = self.ofs_frames + (frame * self.num_bones + bone) * 3
        i0, i1, i2 = d[o], d[o + 1], d[o + 2]
        pool = (i2 << 16) | (i1 << 8) | i0
        w, x, y, z, tx, ty, tz = struct.unpack_from("<7H", d, self.ofs_pool + pool * 14)
        w, x, y, z = (c / 16383.0 - 2.0 for c in (w, x, y, z))
        X, Y, Z = 2 * x, 2 * y, 2 * z
        wx, wy, wz = X * w, Y * w, Z * w
        xx, xy, xz = X * x, Y * x, Z * x
        yy, yz, zz = Y * y, Z * y, Z * z
        t = [c / 64.0 - 512.0 for c in (tx, ty, tz)]
        return [[1 - (yy + zz), xy - wz, xz + wy, t[0]],
                [xy + wz, 1 - (xx + zz), yz - wx, t[1]],
                [xz - wy, yz + wx, 1 - (xx + yy), t[2]]]

    def frame_skin(self, frame):
        """Skinning matrices (3x4) of all bones in a frame."""
        out = [None] * self.num_bones
        for i, b in enumerate(self.bones):          # parents come before children
            m = self.frame_bone(frame, i)
            out[i] = m if b["parent"] < 0 else mul34(out[b["parent"]], m)
        return out


def mul34(a, b):
    r = [[0.0] * 4 for _ in range(3)]
    for i in range(3):
        for j in range(4):
            r[i][j] = sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0.0)
    return r


def xform34(m, p):
    return [m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3] for i in range(3)]


# ---------------------------------------------------------------- GLM

class Surface:
    """name, shader, flags, parent; verts [(x,y,z)], normals, uvs [(u,v) v down],
    weights [[(global bone, w), ...]], tris [(a,b,c)] wound as the game wants
    (clockwise seen from outside)."""

    def __init__(self, name, shader="", flags=0, parent=0):
        self.name, self.shader, self.flags, self.parent = name, shader, flags, parent
        self.verts, self.normals, self.uvs, self.weights, self.tris = [], [], [], [], []


def read_glm(path):
    """(header dict, [Surface]) for LOD 0, weights resolved to global bone indexes."""
    d = open(path, "rb").read()
    ident, ver, name, anim, anim_index, nbones, nlods, ofs_lods, nsurf, ofs_hier, _ = \
        struct.unpack_from(GLM_HEADER, d, 0)
    if ident != b"2LGM":
        raise ValueError("not a .glm: %s" % path)
    hdr = struct.calcsize(GLM_HEADER)
    hoffs = struct.unpack_from("<%di" % nsurf, d, hdr)
    surfs = []
    for o in hoffs:
        p = hdr + o
        s = Surface(_cstr(d[p:p + 64]))
        s.flags, = struct.unpack_from("<I", d, p + 64)
        s.shader = _cstr(d[p + 68:p + 132])
        _, s.parent, _ = struct.unpack_from("<3i", d, p + 132)
        surfs.append(s)
    offs = struct.unpack_from("<%di" % nsurf, d, ofs_lods + 4)
    for i, o in enumerate(offs):
        sp = ofs_lods + 4 + o
        _, idx, _, nv, ofv, nt, oft, nb, ofb, _ = struct.unpack_from("<10i", d, sp)
        s = surfs[idx]
        refs = struct.unpack_from("<%di" % nb, d, sp + ofb)
        for v in range(nv):
            q = sp + ofv + v * 32
            vals = struct.unpack_from("<6fI4B", d, q)
            s.normals.append(vals[0:3])
            s.verts.append(vals[3:6])
            packed, bw = vals[6], vals[7:11]
            n = (packed >> 30) + 1
            ws, total = [], 0.0
            for k in range(n):
                bi = (packed >> (5 * k)) & 31
                if k == n - 1:
                    w = 1.0 - total
                else:
                    w = (bw[k] | ((packed >> (12 + 2 * k)) & 0x300)) / 1023.0
                    total += w
                ws.append((refs[bi], w))
            s.weights.append(ws)
        uvb = sp + ofv + nv * 32
        s.uvs = [struct.unpack_from("<2f", d, uvb + v * 8) for v in range(nv)]
        s.tris = [struct.unpack_from("<3i", d, sp + oft + t * 12) for t in range(nt)]
    head = dict(name=_cstr(name), anim=_cstr(anim), num_bones=nbones, num_lods=nlods)
    return head, surfs


def _pack_weights(ws, refmap):
    """uiNmWeightsAndBoneIndexes + BoneWeightings[4] for <=4 (bone, w) summing to 1."""
    ws = sorted(ws, key=lambda x: -x[1])[:MAX_WEIGHTS]
    total = sum(w for _, w in ws) or 1.0
    ws = [(b, w / total) for b, w in ws]
    n = len(ws)
    packed = (n - 1) << 30
    bytes4 = [0, 0, 0, 0]
    for k, (b, w) in enumerate(ws):
        packed |= refmap[b] << (5 * k)
        if k < n - 1:
            q = max(0, min(1023, int(round(w * 1023))))
            bytes4[k] = q & 0xFF
            packed |= (q & 0x300) << (12 + 2 * k)
    return packed, bytes4


def write_glm(path, model_name, anim_name, num_bones, surfaces):
    """One LOD. surfaces: [Surface]; parent = index into the list (-1 = root).
    Each surface must have <= 32 distinct bones and <= 1000 vertices."""
    hdr = struct.calcsize(GLM_HEADER)
    n = len(surfaces)
    children = [[] for _ in surfaces]
    for i, s in enumerate(surfaces):
        if s.parent >= 0:
            children[s.parent].append(i)
    # surface hierarchy
    hier, hoffs, pos = [], [], 4 * n
    for i, s in enumerate(surfaces):
        blob = (_pad(s.name) + struct.pack("<I", s.flags) + _pad(s.shader) +
                struct.pack("<3i", 0, s.parent, len(children[i])) +
                struct.pack("<%di" % len(children[i]), *children[i]))
        hoffs.append(pos)
        hier.append(blob)
        pos += len(blob)
    hier_blob = struct.pack("<%di" % n, *hoffs) + b"".join(hier)
    ofs_hier = hdr + 4 * n
    ofs_lods = hdr + len(hier_blob)
    # LOD 0 surfaces
    surf_blobs = []
    for i, s in enumerate(surfaces):
        bones = []
        for ws in s.weights:
            for b, w in sorted(ws, key=lambda x: -x[1])[:MAX_WEIGHTS]:
                if b not in bones:
                    bones.append(b)
        if len(bones) > MAX_BONEREFS:
            raise ValueError("surface %s uses %d bones (max 32)" % (s.name, len(bones)))
        if len(s.verts) > 1000:
            raise ValueError("surface %s has %d verts (max 1000)" % (s.name, len(s.verts)))
        refmap = {b: k for k, b in enumerate(bones)}
        vdata = []
        for p, nrm, ws in zip(s.verts, s.normals, s.weights):
            packed, bw = _pack_weights(ws, refmap)
            vdata.append(struct.pack("<6fI4B", *nrm, *p, packed, *bw))
        vblob = b"".join(vdata) + b"".join(struct.pack("<2f", *uv) for uv in s.uvs)
        tblob = b"".join(struct.pack("<3i", *t) for t in s.tris)
        rblob = struct.pack("<%di" % len(bones), *bones)
        h = 40
        ofs_v, ofs_t = h, h + len(vblob)
        ofs_b = ofs_t + len(tblob)
        end = ofs_b + len(rblob)
        surf_blobs.append([i, s, ofs_v, ofs_t, ofs_b, end, vblob, tblob, rblob, len(bones)])
    lod_hdr = 4 + 4 * n
    offsets, pos = [], lod_hdr
    for sb in surf_blobs:
        offsets.append(pos - 4)            # relative to the offsets table
        pos += sb[5]
    lod_size = pos
    out = bytearray()
    out += struct.pack("<i", lod_size) + struct.pack("<%di" % n, *offsets)
    for k, (i, s, ofs_v, ofs_t, ofs_b, end, vblob, tblob, rblob, nb) in enumerate(surf_blobs):
        here = ofs_lods + len(out)
        out += struct.pack("<10i", 0, i, -here, len(s.verts), ofs_v, len(s.tris), ofs_t,
                           nb, ofs_b, end)
        out += vblob + tblob + rblob
    total = ofs_lods + len(out)
    head = struct.pack(GLM_HEADER, b"2LGM", 6, _pad(model_name), _pad(anim_name), 0,
                       num_bones, 1, ofs_lods, n, ofs_hier, total)
    with open(path, "wb") as f:
        f.write(head + hier_blob + bytes(out))
