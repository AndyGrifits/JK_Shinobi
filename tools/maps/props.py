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
        used[ob.material_slots[0].material.name.split("/")[-1]] = None
    return {s: TEXTURES[s] for s in used}
