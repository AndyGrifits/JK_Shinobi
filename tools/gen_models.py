#!/usr/bin/env python3
"""Generate the earth wall pillar models (MD3) for JK_Shinobi.

Each pillar is an irregular, slightly leaning stone column: an N-sided prism
whose rings are jittered, topped with a jagged cap. Origin is at ground level
(z = 0); the model extends below ground so it can rise out of the floor.

Writes assets/models/shinobi/earth_pillar{1,2,3}.md3 using the shader
models/shinobi/earth_rock (see assets/shaders/shinobi.shader).

Usage: python3 tools/gen_models.py
"""
import math
import os
import random
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "models", "shinobi")
SHADER = "models/shinobi/earth_rock"

MD3_IDENT = b"IDP3"
MD3_VERSION = 15
MAX_QPATH = 64
XYZ_SCALE = 64.0  # MD3 stores coordinates * 64 as shorts


def encode_normal(n):
    """Q3 lat/long normal encoding (see tr_surface.cpp: lat = atan2(y,x), lng = acos(z))."""
    x, y, z = n
    length = math.sqrt(x * x + y * y + z * z) or 1.0
    x, y, z = x / length, y / length, z / length
    if x == 0 and y == 0:
        return 0 if z > 0 else 128  # lat 0, lng 0 or 128 (pi)
    lat = int(round(math.atan2(y, x) * 255 / (2 * math.pi))) & 0xFF
    lng = int(round(math.acos(max(-1.0, min(1.0, z))) * 255 / (2 * math.pi))) & 0xFF
    return (lat << 8) | lng


def pad(name, size=MAX_QPATH):
    data = name.encode("ascii")
    return data + b"\0" * (size - len(data))


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def build_pillar(seed, sides=7, height=104.0, radius=19.0):
    rng = random.Random(seed)
    verts, uvs, normals, tris = [], [], [], []

    # rings from below ground up to the shoulder
    ring_z = [-24.0, 18.0, 52.0, 84.0]
    lean = (rng.uniform(-3, 3), rng.uniform(-3, 3))
    phase = rng.uniform(0, 2 * math.pi)
    rings = []
    for k, z in enumerate(ring_z):
        t = (z - ring_z[0]) / (ring_z[-1] - ring_z[0])
        cx, cy = lean[0] * t, lean[1] * t
        row = []
        for i in range(sides):
            a = phase + 2 * math.pi * i / sides
            r = radius * rng.uniform(0.82, 1.08) * (1.0 - 0.12 * t)
            row.append((cx + math.cos(a) * r, cy + math.sin(a) * r, z + rng.uniform(-3, 3)))
        rings.append(row)

    # side walls: duplicate the seam column so U wraps 0..1
    circumference = 2 * math.pi * radius
    base_index = len(verts)
    for k, row in enumerate(rings):
        for i in range(sides + 1):
            p = row[i % sides]
            verts.append(p)
            u = i / sides * (circumference / 128.0)
            v = -(p[2] - ring_z[0]) / 128.0
            uvs.append((u, v))
            normals.append((p[0], p[1], 0.15))  # roughly radial, refined below
    cols = sides + 1
    for k in range(len(rings) - 1):
        for i in range(sides):
            a = base_index + k * cols + i
            b = a + 1
            c = a + cols
            d = c + 1
            tris.append((a, c, b))
            tris.append((b, c, d))

    # jagged cap: centre peak plus a ring of shards
    top = rings[-1]
    peak = (lean[0] + rng.uniform(-4, 4), lean[1] + rng.uniform(-4, 4), height + rng.uniform(-6, 8))
    cap_start = len(verts)
    verts.append(peak)
    uvs.append((0.5, 0.5))
    normals.append((0, 0, 1))
    for i in range(sides):
        p = top[i]
        shard = (p[0] * 0.9, p[1] * 0.9, p[2] + rng.uniform(4, 16))
        verts.append(shard)
        uvs.append((0.5 + shard[0] / 64.0, 0.5 + shard[1] / 64.0))
        normals.append((shard[0], shard[1], 2.0))
    # cap triangles (peak fan) and a skirt joining the shards to the top ring
    for i in range(sides):
        s0 = cap_start + 1 + i
        s1 = cap_start + 1 + (i + 1) % sides
        tris.append((cap_start, s0, s1))
    skirt_start = len(verts)
    for i in range(sides):
        p = top[i]
        verts.append(p)
        uvs.append((i / sides * (circumference / 128.0), 0.0))
        normals.append((p[0], p[1], 0.5))
    for i in range(sides):
        r0 = skirt_start + i
        r1 = skirt_start + (i + 1) % sides
        s0 = cap_start + 1 + i
        s1 = cap_start + 1 + (i + 1) % sides
        tris.append((r0, s0, r1))
        tris.append((r1, s0, s1))

    # orient every triangle so cross(b - a, c - a) points out of the rock
    # (away from the column's axis); these face normals drive the lighting
    def outward(tri):
        a, b, c = tri
        n = cross(sub(verts[b], verts[a]), sub(verts[c], verts[a]))
        cen = [(verts[a][i] + verts[b][i] + verts[c][i]) / 3.0 for i in range(3)]
        axis_z = min(cen[2], ring_z[-1] - 20.0)
        away = (cen[0], cen[1], cen[2] - axis_z)
        return tri if n[0] * away[0] + n[1] * away[1] + n[2] * away[2] >= 0 else (a, c, b)
    tris = [outward(t) for t in tris]

    # flat-ish faceted look: recompute normals from faces and average per vertex
    acc = [[0.0, 0.0, 0.0] for _ in verts]
    for a, b, c in tris:
        n = cross(sub(verts[b], verts[a]), sub(verts[c], verts[a]))
        for idx in (a, b, c):
            acc[idx][0] += n[0]
            acc[idx][1] += n[1]
            acc[idx][2] += n[2]
    # fall back to the radial hint if a vertex has no faces
    normals = [tuple(acc[i]) if any(acc[i]) else normals[i] for i in range(len(verts))]
    return verts, uvs, normals, tris


def write_md3(path, name, verts, uvs, normals, tris, shader=SHADER):
    num_verts, num_tris = len(verts), len(tris)
    mins = [min(v[i] for v in verts) for i in range(3)]
    maxs = [max(v[i] for v in verts) for i in range(3)]
    radius = max(math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2) for v in verts)

    # --- surface ---
    surf_header_size = 4 + MAX_QPATH + 4 * 10
    shaders = pad(shader) + struct.pack("<i", 0)
    tri_data = b"".join(struct.pack("<3i", *t) for t in tris)
    st_data = b"".join(struct.pack("<2f", u, v) for u, v in uvs)
    xyz_data = b"".join(
        struct.pack("<3hH", *(int(round(c * XYZ_SCALE)) for c in p), encode_normal(n))
        for p, n in zip(verts, normals)
    )
    ofs_shaders = surf_header_size
    ofs_tris = ofs_shaders + len(shaders)
    ofs_st = ofs_tris + len(tri_data)
    ofs_xyz = ofs_st + len(st_data)
    ofs_end = ofs_xyz + len(xyz_data)
    surface = (
        MD3_IDENT + pad("pillar")
        + struct.pack("<10i", 0, 1, 1, num_verts, num_tris, ofs_tris, ofs_shaders, ofs_st, ofs_xyz, ofs_end)
        + shaders + tri_data + st_data + xyz_data
    )

    # --- frame ---
    frame = struct.pack("<3f3f3ff", *mins, *maxs, 0, 0, 0, radius) + pad("frame0", 16)

    header_size = 4 + 4 + MAX_QPATH + 4 * 9
    ofs_frames = header_size
    ofs_tags = ofs_frames + len(frame)
    ofs_surfaces = ofs_tags  # no tags
    ofs_end_file = ofs_surfaces + len(surface)
    header = (
        MD3_IDENT + struct.pack("<i", MD3_VERSION) + pad(name)
        + struct.pack("<9i", 0, 1, 0, 1, 0, ofs_frames, ofs_tags, ofs_surfaces, ofs_end_file)
    )
    with open(path, "wb") as f:
        f.write(header + frame + surface)


LOG_SHADER = "models/shinobi/log"
KUNAI_SHADER = "models/shinobi/kunai"


def build_kunai():
    """Kunai along +X with the point at the front, centred near its balance point.

    Texture (models/shinobi/kunai.tga): steel on the left half (u < 0.5), wrapped
    dark handle on the right half.
    """
    verts, uvs, tris, hints = [], [], [], []

    def add(v, uv):
        verts.append(v)
        uvs.append(uv)
        return len(verts) - 1

    # blade: diamond cross-section (wide in Y, thin in Z), tapering to the tip
    blade = [(0.0, 1.0, 0.25), (4.0, 2.4, 0.5), (11.0, 0.9, 0.25), (14.0, 0.0, 0.0)]
    rings = []
    for x, w, t in blade:
        ring = [add((x, 0.0, t), (0.05 + x / 30, 0.2)), add((x, w, 0.0), (0.05 + x / 30, 0.0)),
                add((x, 0.0, -t), (0.05 + x / 30, 0.8)), add((x, -w, 0.0), (0.05 + x / 30, 1.0))]
        rings.append(ring)
    for a, b in zip(rings[:-1], rings[1:]):
        for k in range(4):
            k2 = (k + 1) % 4
            tris += [(a[k], b[k], a[k2]), (a[k2], b[k], b[k2])]
            hints += [None, None]
    # handle: hexagonal grip from x=0 back to x=-7
    sides = 6
    hr = 0.55
    hrings = []
    for x in (0.0, -7.0):
        ring = []
        for k in range(sides):
            ang = 2 * math.pi * k / sides
            ring.append(add((x, math.cos(ang) * hr, math.sin(ang) * hr), (0.55 + 0.4 * k / sides, 0.1 if x == 0 else 0.9)))
        hrings.append(ring)
    for k in range(sides):
        k2 = (k + 1) % sides
        a, b = hrings[0], hrings[1]
        tris += [(a[k], b[k], a[k2]), (a[k2], b[k], b[k2])]
        hints += [None, None]
    # ring at the pommel: a flat octagonal loop in the XY plane around x=-8.6
    seg = 8
    outer, inner = 1.5, 0.9
    cx = -8.6
    o, i_ = [], []
    for k in range(seg):
        ang = 2 * math.pi * k / seg
        for zz in (0.25, -0.25):
            pass
        o.append((add((cx + math.cos(ang) * outer, math.sin(ang) * outer, 0.25), (0.6, 0.5)),
                  add((cx + math.cos(ang) * outer, math.sin(ang) * outer, -0.25), (0.6, 0.5))))
        i_.append((add((cx + math.cos(ang) * inner, math.sin(ang) * inner, 0.25), (0.7, 0.5)),
                   add((cx + math.cos(ang) * inner, math.sin(ang) * inner, -0.25), (0.7, 0.5))))
    for k in range(seg):
        k2 = (k + 1) % seg
        # top and bottom faces
        tris += [(o[k][0], o[k2][0], i_[k][0]), (i_[k][0], o[k2][0], i_[k2][0])]
        hints += [(0, 0, 1), (0, 0, 1)]
        tris += [(o[k][1], i_[k][1], o[k2][1]), (i_[k][1], i_[k2][1], o[k2][1])]
        hints += [(0, 0, -1), (0, 0, -1)]
        # outer rim
        tris += [(o[k][0], o[k][1], o[k2][0]), (o[k2][0], o[k][1], o[k2][1])]
        hints += [("out", k), ("out", k)]

    def orient(tri, hint):
        a, b, c = tri
        n = cross(sub(verts[b], verts[a]), sub(verts[c], verts[a]))
        cen = [(verts[a][j] + verts[b][j] + verts[c][j]) / 3.0 for j in range(3)]
        if hint is None:
            h = (0.0, cen[1], cen[2])            # away from the X axis
        elif isinstance(hint[0], str):
            h = (cen[0] - cx, cen[1], 0.0)      # away from the ring centre
        else:
            h = hint
        return tri if n[0] * h[0] + n[1] * h[1] + n[2] * h[2] >= 0 else (a, c, b)
    tris = [orient(t, h) for t, h in zip(tris, hints)]
    acc = [[0.0, 0.0, 0.0] for _ in verts]
    for a, b, c in tris:
        n = cross(sub(verts[b], verts[a]), sub(verts[c], verts[a]))
        ln = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
        for idx in (a, b, c):
            for j in range(3):
                acc[idx][j] += n[j] / ln
    # scale up a little so it reads at game distances
    verts = [(x * 1.15, y * 1.15, z * 1.15) for x, y, z in verts]
    return verts, uvs, [tuple(v) for v in acc], tris


def build_log(seed=7, sides=10, length=52.0, radius=7.0, segments=4):
    """A sawn-off log lying along X, centred on the origin (the Kawarimi decoy).

    Texture layout (models/shinobi/log.tga): bark in the top half, the end
    grain disc in the bottom-left quarter.
    """
    rng = random.Random(seed)
    verts, uvs, tris, outward_hint = [], [], [], []
    half = length / 2
    bumps = [radius * rng.uniform(0.9, 1.08) for _ in range(sides)]
    rings = []
    for s_i in range(segments + 1):
        x = -half + length * s_i / segments
        taper = 1.0 - 0.06 * (s_i / segments)
        ring = []
        for i in range(sides + 1):  # duplicate seam vertex for clean UVs
            a = 2 * math.pi * (i % sides) / sides
            r = bumps[i % sides] * taper * rng.uniform(0.97, 1.03) if i < sides else None
            ring.append((a, r))
        rings.append((x, ring))
    # fix the seam radius to match vertex 0 of the same ring
    side_idx = []
    for s_i, (x, ring) in enumerate(rings):
        row = []
        r0 = ring[0][1]
        for i, (a, r) in enumerate(ring):
            r = r0 if r is None else r
            row.append(len(verts))
            verts.append((x, math.cos(a) * r, math.sin(a) * r))
            uvs.append((i / sides, 0.5 * s_i / segments))
        side_idx.append(row)
    for s_i in range(segments):
        for i in range(sides):
            a, b = side_idx[s_i][i], side_idx[s_i][i + 1]
            c, d = side_idx[s_i + 1][i], side_idx[s_i + 1][i + 1]
            tris.append((a, c, b))
            tris.append((b, c, d))
            outward_hint += [None, None]
    # end caps: their own vertices so the normals stay flat
    for end, sign in ((0, -1.0), (segments, 1.0)):
        x = rings[end][0]
        centre = len(verts)
        verts.append((x, 0.0, 0.0))
        uvs.append((0.25, 0.75))
        ring_start = len(verts)
        for i in range(sides):
            vx, vy, vz = verts[side_idx[end][i]]
            verts.append((vx, vy, vz))
            a = 2 * math.pi * i / sides
            uvs.append((0.25 + 0.24 * math.cos(a), 0.75 + 0.24 * math.sin(a)))
        for i in range(sides):
            tris.append((centre, ring_start + i, ring_start + (i + 1) % sides))
            outward_hint.append((sign, 0.0, 0.0))

    def orient(tri, hint):
        a, b, c = tri
        n = cross(sub(verts[b], verts[a]), sub(verts[c], verts[a]))
        if hint is None:
            cen = [(verts[a][k] + verts[b][k] + verts[c][k]) / 3.0 for k in range(3)]
            hint = (0.0, cen[1], cen[2])
        return tri if n[0] * hint[0] + n[1] * hint[1] + n[2] * hint[2] >= 0 else (a, c, b)
    tris = [orient(t, h) for t, h in zip(tris, outward_hint)]

    acc = [[0.0, 0.0, 0.0] for _ in verts]
    for a, b, c in tris:
        n = cross(sub(verts[b], verts[a]), sub(verts[c], verts[a]))
        ln = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
        for idx in (a, b, c):
            for k in range(3):
                acc[idx][k] += n[k] / ln
    normals = [tuple(v) for v in acc]
    return verts, uvs, normals, tris


def finish(verts, tris, outward):
    """Orient triangles away from outward(centroid) and compute smooth normals."""
    out_tris = []
    for a, b, c in tris:
        n = cross(sub(verts[b], verts[a]), sub(verts[c], verts[a]))
        cen = [(verts[a][j] + verts[b][j] + verts[c][j]) / 3.0 for j in range(3)]
        h = outward(cen)
        out_tris.append((a, b, c) if n[0] * h[0] + n[1] * h[1] + n[2] * h[2] >= 0 else (a, c, b))
    acc = [[0.0, 0.0, 0.0] for _ in verts]
    for a, b, c in out_tris:
        n = cross(sub(verts[b], verts[a]), sub(verts[c], verts[a]))
        ln = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
        for idx in (a, b, c):
            for j in range(3):
                acc[idx][j] += n[j] / ln
    return [tuple(v) for v in acc], out_tris


def build_shuriken(radius=16.0, thick=1.4, hole=2.2):
    """A fuma shuriken lying flat in the XY plane: four curved blades round a hole.
    Uses the kunai texture's steel half (u < 0.5)."""
    verts, uvs, tris = [], [], []
    pts = 48
    outline = []
    for k in range(pts):
        ang = 2 * math.pi * k / pts
        # four blades, each swept a little (a hooked edge)
        ph = (ang * 2) % math.pi
        blade = max(0.0, math.cos(ph - 0.35)) ** 5
        r = radius * (0.3 + 0.7 * blade)
        outline.append((math.cos(ang) * r, math.sin(ang) * r))
    inner = [(math.cos(2 * math.pi * k / pts) * hole, math.sin(2 * math.pi * k / pts) * hole) for k in range(pts)]
    def uv(x, y):
        return (0.05 + 0.4 * (x / radius * 0.5 + 0.5), 0.5 + 0.5 * y / radius)
    top_o = [len(verts) + i for i in range(pts)]
    for x, y in outline:
        verts.append((x, y, thick / 2)); uvs.append(uv(x, y))
    bot_o = [len(verts) + i for i in range(pts)]
    for x, y in outline:
        verts.append((x, y, -thick / 2)); uvs.append(uv(x, y))
    top_i = [len(verts) + i for i in range(pts)]
    for x, y in inner:
        verts.append((x, y, thick / 2)); uvs.append(uv(x, y))
    bot_i = [len(verts) + i for i in range(pts)]
    for x, y in inner:
        verts.append((x, y, -thick / 2)); uvs.append(uv(x, y))
    kinds = []
    for k in range(pts):
        k2 = (k + 1) % pts
        tris += [(top_o[k], top_o[k2], top_i[k]), (top_i[k], top_o[k2], top_i[k2])]; kinds += ["up", "up"]
        tris += [(bot_o[k], bot_i[k], bot_o[k2]), (bot_i[k], bot_i[k2], bot_o[k2])]; kinds += ["down", "down"]
        tris += [(top_o[k], bot_o[k], top_o[k2]), (top_o[k2], bot_o[k], bot_o[k2])]; kinds += ["out", "out"]
        tris += [(top_i[k], top_i[k2], bot_i[k]), (top_i[k2], bot_i[k2], bot_i[k])]; kinds += ["in", "in"]
    oriented = []
    for t, kd in zip(tris, kinds):
        a, b, c = t
        n = cross(sub(verts[b], verts[a]), sub(verts[c], verts[a]))
        cen = [(verts[a][j] + verts[b][j] + verts[c][j]) / 3.0 for j in range(3)]
        h = {"up": (0, 0, 1), "down": (0, 0, -1), "out": (cen[0], cen[1], 0), "in": (-cen[0], -cen[1], 0)}[kd]
        oriented.append(t if n[0] * h[0] + n[1] * h[1] + n[2] * h[2] >= 0 else (a, c, b))
    normals, tris = finish(verts, oriented, lambda c: (c[0], c[1], c[2]))
    # flat faces read better with face normals on the caps
    normals = [(0.0, 0.0, 1.0) if i in set(top_o + top_i) else ((0.0, 0.0, -1.0) if i in set(bot_o + bot_i) else n)
               for i, n in enumerate(normals)]
    return verts, uvs, normals, oriented


def build_box(hx, hy, hz):
    """A box centred on the origin, the texture over the +X / -X faces (a paper tag lies in YZ)."""
    verts, uvs, tris = [], [], []
    faces = [((1, 0, 0), ( 0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, -1, 0), (0, 0, 1)),
             ((0, 1, 0), (-1, 0, 0), (0, 0, 1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (0, 1, 0), (-1, 0, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]
    h = (hx, hy, hz)
    for n, u, v in faces:
        base = len(verts)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = tuple(n[j] * h[j] + u[j] * su * h[j] + v[j] * sv * h[j] for j in range(3))
            verts.append(p)
            uvs.append(((su + 1) / 2, 1 - (sv + 1) / 2))
        tris += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    normals, tris = finish(verts, tris, lambda c: c)
    return verts, uvs, normals, tris


def build_rockbox(hx, hy, z0, z1, seed, grid=4, bump=3.5):
    """A rough stone block: an axis-aligned box (it is the collision box) whose
    faces are subdivided and bumped in and out a little, sharp at the edges."""
    rng = random.Random(seed)
    verts, uvs, tris = [], [], []
    cz = (z0 + z1) / 2.0
    h = (hx, hy, (z1 - z0) / 2.0)
    faces = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, -1, 0), (0, 0, 1)),
             ((0, 1, 0), (-1, 0, 0), (0, 0, 1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (1, 0, 0), (0, -1, 0))]
    for n, u, v in faces:
        base = len(verts)
        for j in range(grid + 1):
            for i in range(grid + 1):
                su = -1 + 2.0 * i / grid
                sv = -1 + 2.0 * j / grid
                p = [n[k] * h[k] + u[k] * su * h[k] + v[k] * sv * h[k] for k in range(3)]
                if 0 < i < grid and 0 < j < grid:
                    d = rng.uniform(-bump, bump * 0.6)
                    for k in range(3):
                        p[k] += n[k] * d
                p[2] += cz
                verts.append(tuple(p))
                # world-scaled texture coordinates (128 units per repeat)
                uu = sum(u[k] * su * h[k] for k in range(3)) / 128.0
                vv = -sum(v[k] * sv * h[k] for k in range(3)) / 128.0
                uvs.append((uu, vv))
        for j in range(grid):
            for i in range(grid):
                a = base + j * (grid + 1) + i
                tris += [(a, a + 1, a + grid + 2), (a, a + grid + 2, a + grid + 1)]
    normals, tris = finish(verts, tris, lambda c: (c[0], c[1], c[2] - cz))
    return verts, uvs, normals, tris


def build_sphere(radius=5.0, rings=6, segs=10):
    verts, uvs, tris = [], [], []
    for i in range(rings + 1):
        th = math.pi * i / rings
        for j in range(segs + 1):
            ph = 2 * math.pi * j / segs
            verts.append((radius * math.sin(th) * math.cos(ph), radius * math.sin(th) * math.sin(ph) * 0.8,
                          radius * math.cos(th) * 0.7))
            uvs.append((j / segs, i / rings))
    for i in range(rings):
        for j in range(segs):
            a = i * (segs + 1) + j
            b = a + segs + 1
            tris += [(a, b, a + 1), (a + 1, b, b + 1)]
    tris = [t for t in tris if len(set(verts[k] for k in t)) == 3]
    normals, tris = finish(verts, tris, lambda c: c)
    return verts, uvs, normals, tris


def main():
    os.makedirs(OUT, exist_ok=True)
    verts, uvs, normals, tris = build_kunai()
    write_md3(os.path.join(OUT, "kunai.md3"), "models/shinobi/kunai.md3", verts, uvs, normals, tris, shader=KUNAI_SHADER)
    print("models/shinobi/kunai.md3", len(verts), "verts", len(tris), "tris")
    verts, uvs, normals, tris = build_kunai()
    write_md3(os.path.join(OUT, "exkunai.md3"), "models/shinobi/exkunai.md3", verts, uvs, normals, tris, shader="models/shinobi/exkunai")
    # (shuriken.md3 now comes from the artist's model: tools/glm2acc.py --game-dir models/shinobi)
    for name, mesh, shader in (("tag", build_box(0.3, 2.4, 5.0), "models/shinobi/tag"),
                               ("pill_heal", build_sphere(), "models/shinobi/pill_heal"),
                               ("pill_chakra", build_sphere(), "models/shinobi/pill_chakra")):
        write_md3(os.path.join(OUT, name + ".md3"), "models/shinobi/" + name + ".md3", *mesh, shader=shader)
        print("models/shinobi/" + name + ".md3", len(mesh[0]), "verts", len(mesh[3]), "tris")
    verts, uvs, normals, tris = build_log()
    write_md3(os.path.join(OUT, "log.md3"), "models/shinobi/log.md3", verts, uvs, normals, tris, shader=LOG_SHADER)
    print("models/shinobi/log.md3", len(verts), "verts", len(tris), "tris")
    # Rock Pillar blocks (collision boxes in bg_shinobi.h: SHINOBI_ROCK_*)
    for name, mesh in (("rock_block1", build_rockbox(54, 54, -16, 72, 301)),
                       ("rock_block2", build_rockbox(54, 54, -16, 112, 303)),
                       ("rock_block3", build_rockbox(54, 54, -16, 160, 304, grid=5)),
                       ("rock_ledge", build_rockbox(54, 54, -16, 16, 302, grid=4, bump=2.5))):
        write_md3(os.path.join(OUT, name + ".md3"), "models/shinobi/" + name + ".md3", *mesh)
        print("models/shinobi/" + name + ".md3", len(mesh[0]), "verts", len(mesh[3]), "tris")
    for i in range(1, 4):
        verts, uvs, normals, tris = build_pillar(seed=100 + i, sides=6 + i % 3)
        name = "models/shinobi/earth_pillar%d.md3" % i
        write_md3(os.path.join(OUT, "earth_pillar%d.md3" % i), name, verts, uvs, normals, tris)
        print(name, len(verts), "verts", len(tris), "tris")


if __name__ == "__main__":
    main()
