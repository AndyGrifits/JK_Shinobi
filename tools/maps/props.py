"""Procedural cover / elevation props for model-based maps (Blender bpy, game units, floor = z 0).
Called by prep_model.py for map.json "props": [{"type": ..., "at": [x, y], ...}, ...]

types (sizes in game units):
  boulder  size [sx, sy, sz], yaw, seed     - lumpy rock, flattened top (standable), sunk 10 u
  pillar   r, h, seed                       - tall rock column, tapering, flat top (a perch)
  mesa     size [sx, sy, h], yaw, seed      - flat-topped rock slab: cover + a platform
  log      len, r, yaw                      - fallen log lying on the ground
  stump    r, h                             - tree stump, flared foot, flat cut top
  post     r, h                             - dressed wooden training post
Shaders: p_rock (boulder, pillar, mesa), p_bark (log, stump), p_post (post).
"""
import math
import bmesh
import bpy
from mathutils import Matrix, Vector, noise

TILE = {"p_rock": 256.0, "p_bark": 192.0, "p_post": 128.0}
UV_NAME = "UVMap"   # set by prep_model.py to the model's UV layer name, so the join keeps one layer
GROUND = None       # prep_model.py: ground_z(x, y) of the terrain (crates sit on the lowest corner)
STRUCT = None       # prep_model.py: BVH of the model's structures (ledges hug a wall)


def _noise(v, seed, f):
    return noise.noise(Vector((v.x * f + seed * 17.1, v.y * f + seed * 5.3, v.z * f + seed * 11.7)))


def _box_uv(bm, tile):
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        n = f.normal; ax = max(range(3), key=lambda i: abs(n[i]))
        for l in f.loops:
            c = l.vert.co
            l[uv].uv = ((c.y, c.z) if ax == 0 else (c.x, c.z) if ax == 1 else (c.x, c.y))
            l[uv].uv = (l[uv].uv[0] / tile, l[uv].uv[1] / tile)


def _cyl_uv(bm, tile, r, axis):
    """cylindrical: u around (in world length), v along the axis (vertical=z, log=x before rotation)"""
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        cap = abs(f.normal[axis]) > 0.9
        for l in f.loops:
            c = l.vert.co
            if cap:
                a, b = [c[i] for i in range(3) if i != axis]
                l[uv].uv = (a / tile, b / tile)
            else:
                a, b = [c[i] for i in range(3) if i != axis]
                ang = math.atan2(b, a)
                l[uv].uv = (ang * r / tile, c[axis] / tile)
        # fix the seam: keep u continuous within a face
        us = [l[uv].uv[0] for l in f.loops]
        if not cap and max(us) - min(us) > math.pi * r / tile:
            for l in f.loops:
                if l[uv].uv[0] < 0:
                    l[uv].uv = (l[uv].uv[0] + 2 * math.pi * r / tile, l[uv].uv[1])


def _finish(bm, name, shader, mat_for, at, yaw=0.0, z=0.0):
    # every prop is star-shaped about its centre: make each face point away from it (recalc_face_normals
    # got some of the subdivided cylinders inside out -> black, back-face culled logs in game)
    bm.normal_update()
    c = sum((v.co for v in bm.verts), Vector()) / len(bm.verts)
    for f in bm.faces:
        if (f.calc_center_median() - c).dot(f.normal) < 0:
            f.normal_flip()
    bm.normal_update()
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    if me.uv_layers:
        me.uv_layers[0].name = UV_NAME
    me.transform(Matrix.Translation((at[0], at[1], z)) @ Matrix.Rotation(math.radians(yaw), 4, 'Z'))
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    me.materials.append(mat_for(shader))
    return ob


def boulder(p, mat_for, i):
    sx, sy, sz = p["size"]; seed = p.get("seed", i)
    bm = bmesh.new(); bmesh.ops.create_icosphere(bm, subdivisions=3, radius=1.0)
    for v in bm.verts:
        d = v.co.normalized()
        k = 1 + 0.42 * _noise(d, seed, 1.4) + 0.16 * _noise(d, seed + 3, 3.5) + 0.06 * _noise(d, seed + 9, 8.0)
        c = d * k
        if c.z > 0.55: c.z = 0.55 + (c.z - 0.55) * 0.35          # flatter top you can stand on
        if c.z < -0.35: c.z = -0.35                               # flat bottom
        v.co = Vector((c.x * sx / 2, c.y * sy / 2, (c.z + 0.35) / 0.9 * sz - 10))
    _box_uv(bm, TILE["p_rock"])
    return _finish(bm, f"prop_boulder_{i}", "p_rock", mat_for, p["at"], p.get("yaw", 0))


def pillar(p, mat_for, i):
    r, h = p["r"], p["h"]; seed = p.get("seed", i)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=16, radius1=1.0, radius2=1.0, depth=1.0)
    bmesh.ops.subdivide_edges(bm, edges=[e for e in bm.edges if abs(e.verts[0].co.z - e.verts[1].co.z) > 0.5],
                              cuts=11, use_grid_fill=False)
    for v in bm.verts:
        t = v.co.z + 0.5                                         # 0 at the foot, 1 at the top
        rad = Vector((v.co.x, v.co.y, 0))
        if rad.length > 1e-4:
            ledge = 0.08 * math.floor(t * 4 + 0.3 * _noise(Vector((v.co.x, v.co.y, 0)), seed + 2, 2.0))   # stepped strata
            k = (1.2 - 0.38 * t - ledge * 0.5 + 0.08) * (1 + 0.3 * _noise(Vector((v.co.x * 1.5, v.co.y * 1.5, t * 4)), seed, 1.0)
                                                        + 0.1 * _noise(Vector((v.co.x, v.co.y, t * 9)), seed + 4, 2.5))
            rad = rad.normalized() * rad.length * k
        z = t * h - 12 + (6 * _noise(v.co, seed + 7, 3.0) if t > 0.99 else 0)
        v.co = Vector((rad.x * r, rad.y * r, z))
    _box_uv(bm, TILE["p_rock"])
    return _finish(bm, f"prop_pillar_{i}", "p_rock", mat_for, p["at"], p.get("yaw", 0))


def mesa(p, mat_for, i):
    sx, sy, h = p["size"]; seed = p.get("seed", i)
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=6, use_grid_fill=True)
    for v in bm.verts:
        c = v.co.copy(); t = c.z + 0.5
        edge = max(abs(c.x), abs(c.y)) * 2                          # 1 on the sides
        rim = max(0.0, t - 0.8) / 0.2 * max(0.0, edge - 0.6) / 0.4  # chamfer the top rim
        taper = 1.1 - 0.2 * t - 0.12 * rim
        wob = 1 + 0.25 * _noise(Vector((c.x * 2, c.y * 2, t * 2)), seed, 1.2) + 0.08 * _noise(c, seed + 1, 5.0)
        x = c.x * sx * taper * wob; y = c.y * sy * taper * wob
        z = t * h - 10 - rim * 18
        if t > 0.99:
            z += 7 * _noise(c, seed + 5, 3.0)                       # uneven but walkable top
        else:
            z += 10 * _noise(c, seed + 6, 2.5) * t
        v.co = Vector((x, y, z))
    _box_uv(bm, TILE["p_rock"])
    return _finish(bm, f"prop_mesa_{i}", "p_rock", mat_for, p["at"], p.get("yaw", 0))


def log(p, mat_for, i):
    L, r = p["len"], p["r"]
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=14, radius1=r, radius2=r * 0.9, depth=L)
    bmesh.ops.subdivide_edges(bm, edges=[e for e in bm.edges if abs(e.verts[0].co.z - e.verts[1].co.z) > L / 2],
                              cuts=4, use_grid_fill=False)
    for v in bm.verts:
        rad = Vector((v.co.x, v.co.y, 0))
        if rad.length > 1e-3:
            v.co = Vector((*(rad * (1 + 0.06 * _noise(v.co, i, 0.02))).xy, v.co.z))
    _cyl_uv(bm, TILE["p_bark"], r, 2)
    bm.transform(Matrix.Rotation(math.radians(90), 4, 'Y'))   # lie along x
    return _finish(bm, f"prop_log_{i}", "p_bark", mat_for, p["at"], p.get("yaw", 0), z=r - 6)


def stump(p, mat_for, i):
    r, h = p["r"], p["h"]
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=16, radius1=r * 1.35, radius2=r, depth=h + 10)
    for v in bm.verts:
        rad = Vector((v.co.x, v.co.y, 0))
        if rad.length > 1e-3 and v.co.z < 0:
            v.co = Vector((*(rad * (1 + 0.15 * _noise(v.co, i, 0.05))).xy, v.co.z))
    bm.transform(Matrix.Translation((0, 0, (h + 10) / 2 - 10)))
    _cyl_uv(bm, TILE["p_bark"], r, 2)
    return _finish(bm, f"prop_stump_{i}", "p_bark", mat_for, p["at"])


def post(p, mat_for, i):
    r, h = p["r"], p["h"]
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=12, radius1=r, radius2=r, depth=h + 10)
    bm.transform(Matrix.Translation((0, 0, (h + 10) / 2 - 10)))
    _cyl_uv(bm, TILE["p_post"], r, 2)
    return _finish(bm, f"prop_post_{i}", "p_post", mat_for, p["at"])


BUILDERS = {"boulder": boulder, "pillar": pillar, "mesa": mesa, "log": log, "stump": stump, "post": post}
TEXTURES = {"p_rock": "rock", "p_bark": "bark", "p_post": "wood"}


def build(props, mat_for):
    """create the props; returns {shader short name: textures_gen kind} for the ones used"""
    used = {}
    for i, p in enumerate(props):
        if "type" not in p:          # {"_": "comment"} entries
            continue
        ob = BUILDERS[p["type"]](p, mat_for, i)
        if p["type"] in ("boulder", "pillar", "mesa", "log", "stump", "post") and "_zmin" in p:
            ob.data.transform(Matrix.Translation((0, 0, p["_zmin"])))   # these are built on z 0
        for sl in ob.material_slots:
            used[sl.material.name.split("/")[-1]] = None
    return {s: TEXTURES[s] for s in used if s in TEXTURES}


# --------------------------------------------------------------------------------------------------
# Desert-town pieces (Tatooine-like adobe): built with explicit outward winding (no centroid pass),
# several materials per object: wall plaster (p_adobe), dark window / door holes (p_dark).
# Ground: prep_model.py fills p["_zmin"] / p["_zmax"] (terrain under the footprint) when "ground" is on.
# --------------------------------------------------------------------------------------------------
TILE.update({"p_adobe": 320.0, "p_crate": 64.0, "p_dark": 64.0})
TEXTURES.update({"p_adobe": "adobe", "p_dark": "dark", "p_crate": "crate"})


def _rrect(w, d, rc, seg=4):
    """rounded rectangle outline (ccw, centred), w along x, d along y"""
    rc = min(rc, w / 2 - 1, d / 2 - 1)
    pts = []
    for cx, cy, a0 in ((w / 2 - rc, d / 2 - rc, 0), (-w / 2 + rc, d / 2 - rc, 90), (-w / 2 + rc, -d / 2 + rc, 180), (w / 2 - rc, -d / 2 + rc, 270)):
        for k in range(seg + 1):
            a = math.radians(a0 + 90 * k / seg)
            pts.append((cx + rc * math.cos(a), cy + rc * math.sin(a)))
    return pts


def _prism(bm, outline, z0, z1, mat=0, top=True, bottom=True, scale_top=1.0):
    """closed vertical prism over a ccw outline; returns (bottom verts, top verts, top face)"""
    lo = [bm.verts.new((x, y, z0)) for x, y in outline]
    hi = [bm.verts.new((x * scale_top, y * scale_top, z1)) for x, y in outline]
    n = len(outline)
    for i in range(n):
        j = (i + 1) % n
        f = bm.faces.new((lo[i], lo[j], hi[j], hi[i])); f.material_index = mat
    tf = None
    if top:
        tf = bm.faces.new(hi); tf.material_index = mat
    if bottom:
        bf = bm.faces.new(lo[::-1]); bf.material_index = mat
    return lo, hi, tf


def _quad(bm, c, t, u, w, h, mat):
    """flat quad centred at c, spanning t (width) and u (height); normal = t x u"""
    c, t, u = Vector(c), Vector(t).normalized(), Vector(u).normalized()
    vs = [bm.verts.new(c + t * sx * w / 2 + u * sy * h / 2) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    f = bm.faces.new(vs); f.material_index = mat
    return f


def _arch(bm, c, t, u, w, h, mat, seg=6):
    """door: rectangle with a round top (an n-gon); normal = t x u"""
    c, t, u = Vector(c), Vector(t).normalized(), Vector(u).normalized()
    r = w / 2; pts = [c - t * r - u * h / 2, c + t * r - u * h / 2]
    for k in range(seg + 1):
        a = math.pi * k / seg
        pts.append(c + t * r * math.cos(a) + u * (h / 2 - r + r * math.sin(a)))
    f = bm.faces.new([bm.verts.new(p) for p in pts]); f.material_index = mat
    return f


def _finish_multi(bm, name, shaders, mat_for, at, yaw, uv_tiles):
    bm.normal_update()
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        tile = uv_tiles[f.material_index]
        n = f.normal; ax = max(range(3), key=lambda i: abs(n[i]))
        for l in f.loops:
            c = l.vert.co
            l[uv].uv = ((c.y / tile, c.z / tile) if ax == 0 else (c.x / tile, c.z / tile) if ax == 1 else (c.x / tile, c.y / tile))
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    if me.uv_layers:
        me.uv_layers[0].name = UV_NAME
    me.transform(Matrix.Translation((at[0], at[1], 0)) @ Matrix.Rotation(math.radians(yaw), 4, 'Z'))
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    for s in shaders:
        me.materials.append(mat_for(s))
    return ob


def _zs(p, h):
    z0 = p.get("_zmin", 0) - 40                      # sink below the lowest ground under it
    return z0, p.get("_zmax", 0) + h                  # height counted from the highest ground


def adobe_tower(p, mat_for, i):
    """round tapering tower with bands, a dome and rings of small dark windows; door faces local +y"""
    r, h = p["r"], p["h"]; seed = p.get("seed", i)
    z0, z1 = _zs(p, h); zg = p.get("_zmax", 0)
    bm = bmesh.new(); seg = 20
    ring = [(r * math.cos(2 * math.pi * k / seg), r * math.sin(2 * math.pi * k / seg)) for k in range(seg)]
    _prism(bm, ring, z0, z1, 0, scale_top=0.88)
    for bz in p.get("bands", [0.42, 0.78]):           # protruding plaster bands
        zc = zg + h * bz; rr = r * (1 - 0.12 * bz) + 14
        band = [(rr * math.cos(2 * math.pi * k / seg), rr * math.sin(2 * math.pi * k / seg)) for k in range(seg)]
        _prism(bm, band, zc - 14, zc + 14, 0)
    if p.get("dome", True):
        dr = r * 0.88 * 0.82
        sph = bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=8, radius=1.0)
        dv = sph["verts"]
        bmesh.ops.delete(bm, geom=[v for v in dv if v.co.z < -1e-4], context='VERTS')
        for v in [v for v in bm.verts if v in set(dv) and v.is_valid]:
            v.co = Vector((v.co.x * dr, v.co.y * dr, z1 + v.co.z * dr * 0.62))
    rows = p.get("windows", [0.25, 0.58, 0.9])
    for wz in rows:                                   # dark round-ish windows around
        zc = zg + h * wz
        if zc > z1 - 30: continue
        rr = r * (1 - 0.12 * (zc - z0) / (z1 - z0)) + 2
        nwin = max(4, int(2 * math.pi * rr / 170))
        off = _noise(Vector((wz, 0, 0)), seed, 3.0) * 0.5
        for k in range(nwin):
            a = 2 * math.pi * (k + off) / nwin
            c = (rr * math.cos(a), rr * math.sin(a), zc)
            _quad(bm, c, (-math.sin(a), math.cos(a), 0), (0, 0, 1), 34, 46, 1)
    # door at the foot, facing local +y
    _arch(bm, (0, r + 2, zg + 75), (-1, 0, 0), (0, 0, 1), 90, 150, 1)
    return _finish_multi(bm, f"prop_tower_{i}", ["p_adobe", "p_dark"], mat_for, p["at"], p.get("yaw", 0),
                         [TILE["p_adobe"], TILE["p_dark"]])


def adobe_block(p, mat_for, i):
    """rounded adobe block: flat walkable roof inside a low parapet, optional roof dome, window rows
    on the local +y face (the inner face of the ring) and the ends"""
    w, d, h = p["size"]; seed = p.get("seed", i)
    z0, z1 = _zs(p, h); zg = p.get("_zmax", 0)
    bm = bmesh.new()
    rc = p.get("round", 70)
    lo, hi, _ = _prism(bm, _rrect(w, d, rc), z0, z1, 0, top=False, scale_top=0.97)
    # roof: a 28 u parapet around a flat roof 40 u down, every face wound explicitly (outward / up)
    inner = _rrect(w * 0.97 - 56, d * 0.97 - 56, max(8, rc * 0.97 - 28))
    it = [bm.verts.new((x, y, z1)) for x, y in inner]
    ib = [bm.verts.new((x, y, z1 - 40)) for x, y in inner]
    n = len(inner)
    for k in range(n):
        j = (k + 1) % n
        bm.faces.new((hi[k], hi[j], it[j], it[k])).material_index = 0      # parapet top
        bm.faces.new((it[k], it[j], ib[j], ib[k])).material_index = 0      # parapet inner face
    bm.faces.new(ib).material_index = 0                                    # roof
    if p.get("dome"):
        dr = min(w, d) * 0.22
        sph = bmesh.ops.create_uvsphere(bm, u_segments=14, v_segments=8, radius=1.0)
        dv = sph["verts"]
        bmesh.ops.delete(bm, geom=[v for v in dv if v.co.z < -1e-4], context='VERTS')
        dx = (w * 0.22) * (1 if seed % 2 else -1)
        for v in [v for v in bm.verts if v in set(dv) and v.is_valid]:
            v.co = Vector((dx + v.co.x * dr, v.co.y * dr, z1 - 40 + v.co.z * dr * 0.7))
    rows = [zg + h * f for f in p.get("windows", [0.3, 0.68]) if zg + h * f < z1 - 70]
    flat = w / 2 - p.get("round", 70) - 40
    for zc in rows:
        n = max(1, int(2 * flat / 150))
        for k in range(n):
            x = -flat + (k + 0.5) * 2 * flat / n
            _quad(bm, (x, d / 2 + 2, zc), (-1, 0, 0), (0, 0, 1), 44, 54, 1)        # inner face (+y)
            if k % 2 == 0:
                _quad(bm, (x, -d / 2 - 2, zc), (1, 0, 0), (0, 0, 1), 44, 54, 1)     # outer face
    _arch(bm, (0, d / 2 + 2, zg + 80), (-1, 0, 0), (0, 0, 1), 100, 160, 1)
    return _finish_multi(bm, f"prop_block_{i}", ["p_adobe", "p_dark"], mat_for, p["at"], p.get("yaw", 0),
                         [TILE["p_adobe"], TILE["p_dark"]])


def adobe_wall(p, mat_for, i):
    """straight town wall between buildings: battered body + a wider rounded coping"""
    L, t, h = p["len"], p.get("t", 90), p["h"]
    z0, z1 = _zs(p, h)
    bm = bmesh.new()
    _prism(bm, [(-L / 2, -t / 2), (L / 2, -t / 2), (L / 2, t / 2), (-L / 2, t / 2)], z0, z1 - 30, 0)
    _prism(bm, _rrect(L + 10, t + 30, 14, 2), z1 - 30, z1, 0)
    return _finish_multi(bm, f"prop_wall_{i}", ["p_adobe"], mat_for, p["at"], p.get("yaw", 0), [TILE["p_adobe"]])


def ruin(p, mat_for, i):
    """broken adobe wall (L-shape if "len2"): jagged top, cover and a ledge to climb"""
    seed = p.get("seed", i); t = p.get("t", 50); h = p["h"]
    z0 = p.get("_zmin", 0) - 30; zg = p.get("_zmax", 0)
    bm = bmesh.new()
    def segment(L, ox, oy, ang):
        n = max(3, int(L / 60)); pts = [(0, z0), (L, z0)]
        for k in range(n, -1, -1):
            x = L * k / n
            hh = h * (0.45 + 0.55 * (0.5 + 0.5 * _noise(Vector((x * 0.01, seed, 0)), seed, 1.0)))
            pts.append((x, zg + hh))
        ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        def P(x, y, z): return (ox + x * ca - y * sa, oy + x * sa + y * ca, z)
        a = [bm.verts.new(P(x, -t / 2, z)) for x, z in pts]; b = [bm.verts.new(P(x, t / 2, z)) for x, z in pts]
        f = bm.faces.new(a[::-1]); f.material_index = 0
        f = bm.faces.new(b); f.material_index = 0
        m = len(pts)
        for k in range(m):
            j = (k + 1) % m
            f = bm.faces.new((a[k], a[j], b[j], b[k])); f.material_index = 0
    segment(p["len"], 0, 0, 0)
    if p.get("len2"):
        segment(p["len2"], 0, -t / 2, 90)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)   # each segment is a closed prism
    return _finish_multi(bm, f"prop_ruin_{i}", ["p_adobe"], mat_for, p["at"], p.get("yaw", 0), [TILE["p_adobe"]])


def crates(p, mat_for, i):
    """a stack of wooden crates: [[dx, dy, size, yaw], ...] relative to "at", stacked by order"""
    zg = p.get("z", p.get("_zmax", 0)); bm = bmesh.new()
    placed = []
    ya = math.radians(p.get("yaw", 0)); cy_, sy_ = math.cos(ya), math.sin(ya)
    for dx, dy, s, yw in p.get("stack", [[0, 0, 80, 0], [90, 10, 70, 15], [30, 5, 70, -10]]):
        z = zg - 4
        if GROUND and "z" not in p:                  # a crate on a slope sits on the lowest ground under it
            gz = []
            for cx_, cy2 in ((0, 0), (-s / 2, -s / 2), (s / 2, -s / 2), (s / 2, s / 2), (-s / 2, s / 2)):
                lx, ly = dx + cx_, dy + cy2
                gz.append(GROUND(p["at"][0] + lx * cy_ - ly * sy_, p["at"][1] + lx * sy_ + ly * cy_))
            z = min(gz) - 4
        for (px, py, ps, pz) in placed:                # sit on a crate under it
            if abs(px - dx) < (ps + s) / 2 - 10 and abs(py - dy) < (ps + s) / 2 - 10:
                z = max(z, pz)
        ca, sa = math.cos(math.radians(yw)), math.sin(math.radians(yw))
        sq = [(dx + x * ca - y * sa, dy + x * sa + y * ca) for x, y in ((-s / 2, -s / 2), (s / 2, -s / 2), (s / 2, s / 2), (-s / 2, s / 2))]
        _prism(bm, sq, z, z + s, 0)
        placed.append((dx, dy, s, z + s))
    return _finish_multi(bm, f"prop_crates_{i}", ["p_crate"], mat_for, p["at"], p.get("yaw", 0), [TILE["p_crate"]])


def ground_plane(p, mat_for, i):
    """a big flat ground square (fills the horizon around a terrain mesh that ends too early)"""
    s, z = p["size"], p["z"]
    bm = bmesh.new()
    vs = [bm.verts.new((x, y, z)) for x, y in ((-s, -s), (s, -s), (s, s), (-s, s))]
    bm.faces.new(vs)
    return _finish_multi(bm, f"prop_plane_{i}", [p["shader"]], mat_for, p["at"], 0, [p.get("tile", 1024)])


def _to_object(bm, name, shaders, mat_for):
    """mesh object from a bmesh that already has its UVs (no box mapping)"""
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    if me.uv_layers:
        me.uv_layers[0].name = UV_NAME
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    for s_ in shaders:
        me.materials.append(mat_for(s_))
    return ob


def _box(bm, x0, y0, z0, x1, y1, z1, mat):
    _prism(bm, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], z0, z1, mat)


def ledges(p, mat_for, i):
    """platforms hugging a round wall, spiralling up (map.json: "at" = wall centre, "levels": [[top z, a0, a1,
    corbel?], ...] degrees ccw from +x, "depth", "thick"): the inner edge follows the wall (horizontal rays
    from the centre against the model), so they sit flush on a bulging dome"""
    cx, cy = p["at"]; depth = p.get("depth", 150); th = p.get("thick", 24)
    bm = bmesh.new(); seg = p.get("seg", 8)
    def wall_r(a, z):
        d = Vector((math.cos(a), math.sin(a), 0))
        h = STRUCT.ray_cast(Vector((cx, cy, z)), d, 3000) if STRUCT else (None,)
        return (h[0] - Vector((cx, cy, z))).length if h[0] is not None else None
    for lv in p["levels"]:
        zt, a0, a1 = lv[:3]; cb = lv[3] if len(lv) > 3 else 50
        ring = []
        for k in range(seg + 1):
            a = math.radians(a0 + (a1 - a0) * k / seg)
            r0 = next((r for r in (wall_r(a, zt - dz) for dz in (4, 30, 60, 90, 120)) if r), 500)
            rs = [r for r in (wall_r(a, zt + dz) for dz in (8, 40, 72)) if r]
            ri = r0 - 14; ro = max([r0] + rs) + depth
            c, s_ = math.cos(a), math.sin(a)
            P = lambda r, z: bm.verts.new((cx + r * c, cy + r * s_, z))
            ring.append((P(ri, zt), P(ro, zt), P(ro, zt - th), P(ri, zt - th - cb)))
        for k in range(seg):
            A, B = ring[k], ring[k + 1]
            for q in range(4):
                bm.faces.new((A[q], A[(q + 1) % 4], B[(q + 1) % 4], B[q]))
        bm.faces.new(ring[0]); bm.faces.new(ring[-1][::-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _finish_multi(bm, f"prop_ledges_{i}", ["p_adobe"], mat_for, (0, 0), 0, [TILE["p_adobe"]])


def pool(p, mat_for, i):
    """sunken rectangular pool for the hole a "plaza" cut in the terrain: tiled walls and floor, underwater
    steps at the middle of the short sides, an adobe coping over the terrain edge, a low wooden fence with
    gaps (map.json: "rect" = the hole, "z" = plaza level, "depth"; gen_map "water" fills it)"""
    x0, y0, x1, y1 = p["rect"]; Z = p["z"]; D = p.get("depth", 96); w = 24
    bm = bmesh.new()
    _box(bm, x0, y0, Z - D - 16, x1, y1, Z - D, 0)                                   # floor
    zt = Z + 14
    for b in ((x0, y0, x0 + w, y1), (x1 - w, y0, x1, y1), (x0 + w, y0, x1 - w, y0 + w), (x0 + w, y1 - w, x1 - w, y1)):
        _box(bm, b[0], b[1], Z - D - 16, b[2], b[3], Z - 6, 0)                         # walls
    o = 36                                                                            # coping over the terrain edge
    for b in ((x0 - o, y0 - o, x1 + o, y0 + w), (x0 - o, y1 - w, x1 + o, y1 + o), (x0 - o, y0 + w, x0 + w, y1 - w), (x1 - w, y0 + w, x1 + o, y1 - w)):
        _box(bm, b[0], b[1], Z - 6, b[2], b[3], zt, 1)
    long_x = (x1 - x0) >= (y1 - y0); sw = p.get("steps_w", 192); cxm, cym = (x0 + x1) / 2, (y0 + y1) / 2
    ns = int(D / 16)
    for k in range(ns):                                                               # steps out of the water
        top = Z - D + 16 * (k + 1); dep = 24 * (ns - k)
        if long_x:
            _box(bm, x0 + w, cym - sw / 2, Z - D, x0 + w + dep, cym + sw / 2, top, 0)
            _box(bm, x1 - w - dep, cym - sw / 2, Z - D, x1 - w, cym + sw / 2, top, 0)
        else:
            _box(bm, cxm - sw / 2, y0 + w, Z - D, cxm + sw / 2, y0 + w + dep, top, 0)
            _box(bm, cxm - sw / 2, y1 - w - dep, Z - D, cxm + sw / 2, y1 - w, top, 0)
    # fence on the coping's outer half: posts + two rails, a gap in the middle of every side
    fh = p.get("fence_h", 48); gap = p.get("gap", 200); e = o - 8; sp = p.get("post_spacing", 120)
    def fence(ax, ay, bx, by):
        L = math.hypot(bx - ax, by - ay); ux, uy = (bx - ax) / L, (by - ay) / L
        for s0, s1 in ((0, L / 2 - gap / 2), (L / 2 + gap / 2, L)):
            n = max(1, round((s1 - s0) / sp))
            for k in range(n + (0 if s1 >= L else 1)):         # the corner post belongs to the next side
                s_ = s0 + (s1 - s0) * k / n; px, py = ax + ux * s_, ay + uy * s_
                _box(bm, px - 6, py - 6, zt, px + 6, py + 6, zt + fh, 2)
            for rz in (fh * 0.45, fh - 6):
                pa = (ax + ux * s0, ay + uy * s0); pb = (ax + ux * s1, ay + uy * s1)
                _box(bm, min(pa[0], pb[0]) - 3, min(pa[1], pb[1]) - 3, zt + rz - 3, max(pa[0], pb[0]) + 3, max(pa[1], pb[1]) + 3, zt + rz + 3, 2)
    fence(x0 - e, y0 - e, x1 + e, y0 - e); fence(x1 + e, y0 - e, x1 + e, y1 + e)
    fence(x1 + e, y1 + e, x0 - e, y1 + e); fence(x0 - e, y1 + e, x0 - e, y0 - e)
    return _finish_multi(bm, f"prop_pool_{i}", ["p_pooltile", "p_adobe", "p_wood"], mat_for, (0, 0), 0,
                         [TILE["p_pooltile"], TILE["p_adobe"], TILE["p_wood"]])


def dust_ring(p, mat_for, i):
    """cheap dust storm at the map's edge: a few translucent curtains along a superellipse (map.json:
    "at" = centre, "superellipse" [a, b, n], "layers": [[offset, z0, z1], ...], "tile" u per texture repeat).
    One quad strip per layer, u along the curve, v from the foot (0) to the top (1): the texture fades out
    upwards and the shader scrolls it sideways"""
    cx, cy = p["at"]; A, B, N = p["superellipse"]; seg = p.get("segments", 128); tile = p.get("tile", 2048)
    bm = bmesh.new(); uv = bm.loops.layers.uv.verify(); seed = p.get("seed", i)
    for li, (off, z0, z1) in enumerate(p["layers"]):
        pts = superellipse(cx, cy, A + off, B + off, N, seg)
        lo = []; hi = []; us = [0.0]
        for k in range(seg + 1):
            x, y = pts[k % seg]
            if k: us.append(us[-1] + math.dist(pts[k % seg], pts[k - 1]) / tile)
            top = z1 + 260 * _noise(Vector((k / seg * 9, li, 0)), seed, 1.0)
            lo.append(bm.verts.new((x, y, z0))); hi.append(bm.verts.new((x, y, top)))
        scale = max(1, round(us[-1])) / us[-1]          # whole repeats: no seam where the strip closes
        for k in range(seg):
            f = bm.faces.new((lo[k], lo[k + 1], hi[k + 1], hi[k]))
            for lp, (u, v) in zip(f.loops, ((us[k], 0), (us[k + 1], 0), (us[k + 1], 1), (us[k], 1))):
                lp[uv].uv = (u * scale + li * 0.37, v)
    return _to_object(bm, f"prop_dust_{i}", [p.get("shader", "p_dust")], mat_for)


def furniture(p, mat_for, i):
    """indoor pieces at a given floor height ("z"): "desk" (size [w, d, h]) with a few scroll rolls on it,
    "shelf" (size [w, d, h]) - a cabinet with dark pigeon-holes on its front (local -y)"""
    w, d, h = p["size"]; z = p["z"]; bm = bmesh.new(); kind = p["kind"]
    if kind == "desk":
        _box(bm, -w / 2 + 8, -d / 2 + 8, z, w / 2 - 8, d / 2 - 8, z + h - 6, 0)       # body
        _box(bm, -w / 2, -d / 2, z + h - 6, w / 2, d / 2, z + h, 0)                   # top
        for k, (sx, sy) in enumerate(((-0.25, 0.1), (0.05, -0.15), (0.3, 0.2))):     # scrolls lying on it
            r = 5 + k % 2 * 2; L = 34 + 8 * k; px, py = sx * w, sy * d
            ring = [(px - L / 2, py + r * math.cos(2 * math.pi * q / 8)) for q in range(8)]
            sph = [(px - L / 2, py + r * math.cos(2 * math.pi * q / 8), z + h + r + r * math.sin(2 * math.pi * q / 8)) for q in range(8)]
            a = [bm.verts.new(v) for v in sph]; b = [bm.verts.new((v[0] + L, v[1], v[2])) for v in sph]
            for q in range(8):
                f = bm.faces.new((a[q], a[(q + 1) % 8], b[(q + 1) % 8], b[q])); f.material_index = 2
            bm.faces.new(a[::-1]).material_index = 2; bm.faces.new(b).material_index = 2
    else:
        _box(bm, -w / 2, -d / 2, z, w / 2, d / 2, z + h, 0)
        rows = max(2, int(h / 46)); cols = max(2, int(w / 44))
        for r_ in range(rows):
            for c_ in range(cols):
                cx_ = -w / 2 + (c_ + 0.5) * w / cols; cz = z + (r_ + 0.5) * h / rows
                _quad(bm, (cx_, -d / 2 - 1, cz), (1, 0, 0), (0, 0, 1), w / cols - 8, h / rows - 8, 1)
    bmesh.ops.recalc_face_normals(bm, faces=[f for f in bm.faces if f.material_index != 1])
    return _finish_multi(bm, f"prop_{kind}_{i}", ["p_wood", "p_dark", "p_paper"], mat_for, p["at"], p.get("yaw", 0),
                         [TILE["p_wood"], TILE["p_dark"], TILE["p_paper"]])


TILE.update({"p_pooltile": 128.0, "p_wood": 128.0, "p_paper": 64.0})
TEXTURES.update({"p_pooltile": "pooltile", "p_wood": "wood", "p_paper": "paper", "p_dust": "dust"})
BUILDERS.update({"adobe_tower": adobe_tower, "adobe_block": adobe_block, "adobe_wall": adobe_wall,
                 "ruin": ruin, "crates": crates, "ground_plane": ground_plane, "ledges": ledges, "pool": pool,
                 "dust_ring": dust_ring, "furniture": furniture})


def superellipse(cx, cy, a, b, n, k=2000):
    """points (ccw) of |x/a|^n + |y/b|^n = 1"""
    out = []
    for j in range(k):
        t = 2 * math.pi * j / k; c, s = math.cos(t), math.sin(t)
        out.append((cx + a * math.copysign(abs(c) ** (2 / n), c), cy + b * math.copysign(abs(s) ** (2 / n), s)))
    return out


def plan_ring(rc, occupied):
    """ring of adobe buildings and walls along a superellipse (map.json "ring"). occupied(x, y, r) -> True if
    an existing structure is within r of (x, y): an element that hits one is pushed outwards."""
    import random
    rnd = random.Random(rc.get("seed", 1))
    pts = superellipse(rc["center"][0], rc["center"][1], rc["a"], rc["b"], rc.get("n", 3))
    seg = [math.dist(pts[k], pts[(k + 1) % len(pts)]) for k in range(len(pts))]
    total = sum(seg); cum = [0.0]
    for s in seg: cum.append(cum[-1] + s)
    def at(s):
        s %= total
        import bisect
        k = min(bisect.bisect_right(cum, s) - 1, len(pts) - 1)
        f = (s - cum[k]) / max(seg[k], 1e-6); p0 = pts[k]; p1 = pts[(k + 1) % len(pts)]
        x = p0[0] + (p1[0] - p0[0]) * f; y = p0[1] + (p1[1] - p0[1]) * f
        tx, ty = p1[0] - p0[0], p1[1] - p0[1]; L = math.hypot(tx, ty) or 1
        return x, y, tx / L, ty / L
    out = []; s = 0.0; kind = 0
    pattern = rc.get("pattern", ["tower", "wall", "block", "wall", "block", "wall"])
    while s < total - 200:
        k = pattern[kind % len(pattern)]; kind += 1
        if k == "tower":
            r = rnd.uniform(*rc.get("tower_r", [190, 290])); w = 2 * r
            e = {"type": "adobe_tower", "r": round(r), "h": round(rnd.uniform(*rc.get("tower_h", [700, 1150]))), "seed": rnd.randint(1, 999)}
        elif k == "block":
            w = rnd.uniform(*rc.get("block_w", [480, 820]))
            e = {"type": "adobe_block", "size": [round(w), round(rnd.uniform(380, 560)), round(rnd.uniform(*rc.get("block_h", [430, 760])))],
                 "dome": rnd.random() < 0.5, "seed": rnd.randint(1, 999)}
        else:
            w = rnd.uniform(*rc.get("wall_len", [260, 520]))
            e = {"type": "adobe_wall", "len": round(w + 60), "h": round(rnd.uniform(*rc.get("wall_h", [380, 470]))), "t": 90}
        if s + w > total:                        # close the loop with a wall
            w = total - s; e = {"type": "adobe_wall", "len": round(w + 60), "h": 420, "t": 90}
        x, y, tx, ty = at(s + w / 2)
        nx, ny = ty, -tx                         # outward normal of a ccw curve
        depth = e.get("r", 0) * 2 or (e["size"][1] if "size" in e else 90)
        def hits(push):
            cx, cy = x + nx * push, y + ny * push
            probes = [(cx, cy)] + [(cx - nx * depth / 2 + tx * w * f, cy - ny * depth / 2 + ty * w * f) for f in (-0.5, 0, 0.5)]
            return any(occupied(px, py, 70) for px, py in probes)
        push = 0
        while push < 700 and hits(push):
            push += 40
        x += nx * push; y += ny * push
        e["at"] = [round(x), round(y)]
        e["yaw"] = round(math.degrees(math.atan2(ty, tx)), 1)   # local x along the ring, local +y inwards
        if push: e["_pushed"] = push
        out.append(e)
        s += w
    return out
