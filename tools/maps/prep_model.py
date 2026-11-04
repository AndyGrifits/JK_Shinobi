"""Prepare an environment model for a JK_Shinobi map (runs in Blender's Python: `pip install bpy`).

    python3 tools/maps/prep_model.py maps/<name>/map.json <source.fbx> <texture dir> <out dir>

Reads the map's map.json ("model" block), and:
  * imports the FBX, applies object transforms,
  * moves it so the play area's floor centre sits at the origin and scales it to game units,
  * merges materials that share a texture and renames them to their shader path
    (textures/<shader_dir>/tNN), so q3map2 picks the shader straight from the OBJ,
  * writes <out>/model.obj (game units, triangulated, OBJ's usual Y-up) for misc_model,
    and <out>/model.blend (Z-up game space) for the other tools,
  * writes <out>/textures/<shader_dir>/tNN.jpg (opaque) / .tga (with real alpha),
  * writes <out>/materials.json (shader -> texture file, has alpha, triangle count).
"""
import json, math, os, sys

import bpy
import mathutils
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python -I leaves the script dir out
import props as props_mod          # noqa: E402
import textures_gen                # noqa: E402

cfg_path, fbx, texdir, out = sys.argv[-4:]
cfg = json.load(open(cfg_path))
mdl = cfg["model"]
shader_dir = cfg["shader_dir"]

if fbx.lower().endswith(".blend"):
    # a .blend source (e.g. an XPS import): take the meshes as they are, drop armatures and their modifiers
    bpy.ops.wm.open_mainfile(filepath=fbx)
    for o in list(bpy.context.scene.objects):
        if o.type == 'MESH':
            for md in list(o.modifiers):
                o.modifiers.remove(md)
    for o in list(bpy.context.scene.objects):
        if o.type != 'MESH':
            bpy.data.objects.remove(o, do_unlink=True)
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=fbx)

meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
# objects to leave out entirely (map.json "model": {"drop_objects": [names]}), e.g. 2D grass cards
drop = set(mdl.get("drop_objects", []))
missing = drop - {o.name for o in meshes}
if missing:
    print("DROP: not in the model:", sorted(missing))
for o in [o for o in meshes if o.name in drop]:
    bpy.data.objects.remove(o, do_unlink=True)
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
if drop:
    print(f"DROP {len(drop) - len(missing)} objects")
for o in bpy.context.scene.objects:
    o.select_set(o.type == 'MESH')
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

# model space -> game space: (p - origin) * scale
s = mdl["scale"]
ox, oy, oz = mdl["origin"]
m = mathutils.Matrix.Scale(s, 4) @ mathutils.Matrix.Translation((-ox, -oy, -oz))
for o in meshes:
    o.data.transform(m)
    o.data.update()

# scenery too far to matter (map.json "model": {"drop_beyond": r}): faces farther than r from the origin go
# (an island's sea out to the horizon would only blow up the hull and coarsen the light grid)
if mdl.get("drop_beyond"):
    import bmesh
    R_ = mdl["drop_beyond"]; gone = 0
    for o in meshes:
        bm = bmesh.new(); bm.from_mesh(o.data)
        kill = [f for f in bm.faces if f.calc_center_median().xy.length > R_]
        bmesh.ops.delete(bm, geom=kill, context='FACES'); gone += len(kill)
        bm.to_mesh(o.data); bm.free(); o.data.update()
    print(f"DROP {gone} faces beyond {R_}")

# one shader per texture image
def tex_of(mat):
    for n in mat.node_tree.nodes:
        if n.type == 'TEX_IMAGE' and n.image:
            return os.path.basename(n.image.filepath)
    return None

tex_names = sorted({tex_of(s.material) for o in meshes for s in o.material_slots if s.material} - {None},
                   key=lambda t: int(''.join(c for c in t if c.isdigit()) or 0))
if mdl.get("shader_names") == "basename":   # e.g. SM_DaoJu_Tex_01_b.png -> sm_daoju_tex_01_b
    import re
    shader_for_tex = {t: f"textures/{shader_dir}/" + re.sub(r"[^a-z0-9]+", "_", os.path.splitext(t)[0].lower()).strip("_")
                      for t in tex_names}
    for k_, v_ in list(shader_for_tex.items()):          # "shader_rename": {basename: short}
        b_ = os.path.basename(v_)
        if b_ in mdl.get("shader_rename", {}):
            shader_for_tex[k_] = f"textures/{shader_dir}/" + mdl["shader_rename"][b_]
else:                                       # digits: ntxr0201.png -> t201
    shader_for_tex = {t: f"textures/{shader_dir}/t{int(''.join(c for c in t if c.isdigit())):02d}" for t in tex_names}
merged = {}
for t, sh in shader_for_tex.items():
    mat = bpy.data.materials.new(sh)
    merged[t] = mat
for o in meshes:
    for slot in o.material_slots:
        t = tex_of(slot.material) if slot.material else None
        if t:
            slot.material = merged[t]

tex_path_of = {sh: os.path.join(texdir, t) for t, sh in shader_for_tex.items()}
def _slot(o, full):
    """material slot index of shader `full` on object o (added if missing)"""
    for i_, sl in enumerate(o.material_slots):
        if sl.material and sl.material.name == full:
            return i_
    m_ = bpy.data.materials.get(full) or bpy.data.materials.new(full)
    o.data.materials.append(m_)
    return len(o.material_slots) - 1

# atlas faces that sample cut-out texels (foliage cards) -> their own alpha-tested shader, so the rest of
# the atlas stays an opaque JPG (map.json "model": {"uv_alpha_split": [{"shader": s, "alpha_shader": a}]})
for us in mdl.get("uv_alpha_split", []):
    import bmesh, numpy as np
    full = f"textures/{shader_dir}/{us['shader']}"
    A = np.asarray(Image.open(tex_path_of[full]).convert("RGBA"))[..., 3]; Hh, Ww = A.shape
    moved = 0
    for o in meshes:
        if not any(sl.material and sl.material.name == full for sl in o.material_slots): continue
        si = _slot(o, full); ai = _slot(o, f"textures/{shader_dir}/{us['alpha_shader']}")
        bm = bmesh.new(); bm.from_mesh(o.data); uvl = bm.loops.layers.uv.active
        for f in bm.faces:
            if f.material_index != si: continue
            uvs = [l[uvl].uv.copy() for l in f.loops]; c = sum(uvs, mathutils.Vector((0, 0))) / len(uvs)
            smp = [c] + [c * 0.35 + u * 0.65 for u in uvs]
            a = min(A[min(Hh - 1, int((1 - (q.y % 1.0)) * Hh)), min(Ww - 1, int((q.x % 1.0) * Ww))] for q in smp)
            if a < 128:
                f.material_index = ai; moved += 1
        bm.to_mesh(o.data); bm.free(); o.data.update()
    print(f"UV_ALPHA {moved} faces -> {us['alpha_shader']}")

# terrain textured with a blurry overview image (map.json "model": {"uv_terrain": [{"shader": s, "uv_box":
# [u0, v0, u1, v1], "region": [x0, y0, x1, y1], "target": short, "size": px, "classes": {kind: [[r, g, b], ...]},
# "tile": {kind: u}}]}): faces whose UVs lie in uv_box move to the "target" shader with world-planar UVs over
# region, and a colour map for it is painted: each texel takes the overview colour under it (bilinear, via
# the world -> overview UV mapping fitted on those faces), turns it into soft weights of the classes and
# mixes their tiled textures (textures_gen kinds). gen_map lays a tiled grain over it ("raw" stages).
terrain_maps = {}
for ut in mdl.get("uv_terrain", []):
    import bmesh, numpy as np
    full = f"textures/{shader_dir}/{ut['shader']}"; tgt = f"textures/{shader_dir}/{ut['target']}"
    SRC = np.asarray(Image.open(tex_path_of[full]).convert("RGB")).astype(float); Hh, Ww = SRC.shape[:2]
    u0, v0, u1, v1 = ut["uv_box"]; R = ut["region"]
    P = []; Q = []; nmoved = 0
    for o in meshes:
        if not any(sl.material and sl.material.name == full for sl in o.material_slots): continue
        si = _slot(o, full); ti = _slot(o, tgt)
        bm = bmesh.new(); bm.from_mesh(o.data); uvl = bm.loops.layers.uv.active
        for f in bm.faces:
            if f.material_index != si: continue
            c = sum((l[uvl].uv for l in f.loops), mathutils.Vector((0, 0))) / len(f.loops)
            if not (u0 <= c.x <= u1 and v0 <= c.y <= v1): continue
            xs_ = [v.co.x for v in f.verts]; ys_ = [v.co.y for v in f.verts]
            if min(xs_) < R[0] or max(xs_) > R[2] or min(ys_) < R[1] or max(ys_) > R[3]: continue   # backdrop keeps it
            for l in f.loops:
                P.append((l.vert.co.x, l.vert.co.y, 1.0)); Q.append((l[uvl].uv.x, l[uvl].uv.y))
                l[uvl].uv = ((l.vert.co.x - R[0]) / (R[2] - R[0]), (l.vert.co.y - R[1]) / (R[3] - R[1]))
            f.material_index = ti; nmoved += 1
        bm.to_mesh(o.data); bm.free(); o.data.update()
    M, *_ = np.linalg.lstsq(np.array(P), np.array(Q), rcond=None)     # world (x, y, 1) -> overview uv
    n = ut.get("size", 2048)
    gx = R[0] + (np.arange(n) + 0.5) / n * (R[2] - R[0]); gy = R[3] - (np.arange(n) + 0.5) / n * (R[3] - R[1])
    X, Y = np.meshgrid(gx, gy)
    U = X * M[0, 0] + Y * M[1, 0] + M[2, 0]; V = X * M[0, 1] + Y * M[1, 1] + M[2, 1]
    fx = (U % 1.0) * Ww - 0.5; fy = (1 - V % 1.0) * Hh - 0.5                  # bilinear overview sample
    x0_ = np.clip(np.floor(fx).astype(int), 0, Ww - 2); y0_ = np.clip(np.floor(fy).astype(int), 0, Hh - 2)
    ax_ = np.clip(fx - x0_, 0, 1)[..., None]; ay_ = np.clip(fy - y0_, 0, 1)[..., None]
    col_ = (SRC[y0_, x0_] * (1 - ax_) + SRC[y0_, x0_ + 1] * ax_) * (1 - ay_) + (SRC[y0_ + 1, x0_] * (1 - ax_) + SRC[y0_ + 1, x0_ + 1] * ax_) * ay_
    from PIL import ImageFilter
    col_ = np.asarray(Image.fromarray(col_.clip(0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(n / 400))).astype(float)
    kinds = list(ut["classes"]); dists = []
    for k_ in kinds:
        refs = np.array(ut["classes"][k_], float)
        dists.append(np.min(((col_[None] - refs[:, None, None]) ** 2).sum(-1), axis=0))
    D = np.stack(dists); W_ = np.exp(-(D - D.min(0)) / ut.get("softness", 900.0)); W_ /= W_.sum(0)
    out_img = np.zeros((n, n, 3))
    for k_, kind in enumerate(kinds):           # each class: its tiled texture resampled to world positions
        os.makedirs(os.path.join(out, "gen"), exist_ok=True)
        tp = textures_gen.make(kind, os.path.join(out, "gen", f"_tile_{kind}.jpg"))
        T_ = np.asarray(Image.open(tp).convert("RGB")).astype(float); th, tw = T_.shape[:2]
        tl = ut.get("tile", {}).get(kind, 384)
        tx = ((X / tl) % 1.0 * tw).astype(int) % tw; ty = ((-Y / tl) % 1.0 * th).astype(int) % th
        out_img += T_[ty, tx] * W_[k_][..., None]
    cp = os.path.join(out, "gen", ut["target"] + ".jpg"); os.makedirs(os.path.dirname(cp), exist_ok=True)
    Image.fromarray(out_img.clip(0, 255).astype(np.uint8)).save(cp, quality=92)
    terrain_maps[tgt] = cp
    print(f"UV_TERRAIN {nmoved} faces -> {ut['target']} ({n} px colour map over {R})")

# deepen a channel (map.json "model": {"sink": [{"superellipse": [cx, cy, a, b, n], "below": z, "dz": u,
# "shaders": [...]}]}): vertices under "below" inside the area go down by dz (bed and the foot of its walls)
for sk in mdl.get("sink", []):
    shs = {f"textures/{shader_dir}/{x}" for x in sk["shaders"]}; cx_, cy_, A_, B_, N_ = sk["superellipse"]; moved = 0
    for o in meshes:
        idx = {i for i, sl in enumerate(o.material_slots) if sl.material and sl.material.name in shs}
        vs = {v for p_ in o.data.polygons if p_.material_index in idx for v in p_.vertices}
        for vi in vs:
            co = o.data.vertices[vi].co
            if co.z < sk["below"] and abs((co.x - cx_) / A_) ** N_ + abs((co.y - cy_) / B_) ** N_ <= 1:
                co.z -= sk["dz"]; moved += 1
        o.data.update()
    print(f"SINK {moved} vertices by {sk['dz']} below {sk['below']}")

# open up a storey (map.json "model": {"delete_faces": [{"center": [x, y], "r0": u, "r1": u, "z0": u, "z1": u,
# "max_nz": 0.5, "shaders": [...]}]}): wall faces in that ring and height band go (a "gallery" prop adds
# floor, ceiling and posts)
for dl in mdl.get("delete_faces", []):
    import bmesh
    shs = {f"textures/{shader_dir}/{x}" for x in dl["shaders"]}; cx_, cy_ = dl["center"]; gone = 0
    for o in meshes:
        names_ = [sl.material.name if sl.material else "" for sl in o.material_slots]
        bm = bmesh.new(); bm.from_mesh(o.data)
        kill = []
        for f in bm.faces:
            if names_[f.material_index] not in shs: continue
            c = f.calc_center_median(); r = math.hypot(c.x - cx_, c.y - cy_)
            if dl["r0"] <= r <= dl["r1"] and dl["z0"] <= c.z <= dl["z1"] and abs(f.normal.z) <= dl.get("max_nz", 0.5):
                kill.append(f)
        bmesh.ops.delete(bm, geom=kill, context='FACES'); gone += len(kill)
        bm.to_mesh(o.data); bm.free(); o.data.update()
    print(f"DELETE_FACES {gone} at {dl['center']} z {dl['z0']}..{dl['z1']}")

# holes in the ground for sunken things (map.json "cuts": [{"poly": [[x, y], ...] convex ccw, "shaders": [...]}];
# a "waterway" prop adds its own outline): faces are split along the outline and the inside removed
def _cut_polys():
    polys = [(c["poly"], c["shaders"]) for c in cfg.get("cuts", [])]
    for pr in cfg.get("props", []):
        if pr.get("type") != "waterway": continue
        e = pr.get("width", 260) / 2 + 32 + 20; shs = pr.get("cut_shaders", cfg.get("ground_shaders", []))
        pts = pr["path"]
        for (ax, ay), (bx, by) in zip(pts[:-1], pts[1:]):
            L = math.hypot(bx - ax, by - ay); ux, uy = (bx - ax) / L, (by - ay) / L
            ax, ay, bx, by = ax - ux * 30, ay - uy * 30, bx + ux * 40, by + uy * 40
            polys.append(([(ax + uy * e, ay - ux * e), (bx + uy * e, by - ux * e), (bx - uy * e, by + ux * e), (ax - uy * e, ay + ux * e)], shs))
        if pr.get("pond"):
            R = pr["pond"]["r"] + 32 + 20; cx_, cy_ = pts[0]
            polys.append(([(cx_ + R / math.cos(math.pi / 16) * math.cos(2 * math.pi * k / 16), cy_ + R / math.cos(math.pi / 16) * math.sin(2 * math.pi * k / 16)) for k in range(16)], shs))
    return polys
for poly, shs_ in _cut_polys():
    import bmesh
    shs = {f"textures/{shader_dir}/{x}" for x in shs_}
    xs_ = [q[0] for q in poly]; ys_ = [q[1] for q in poly]
    def inside(x, y):
        n_ = len(poly)
        for k in range(n_):
            (ax, ay), (bx, by) = poly[k], poly[(k + 1) % n_]
            if (bx - ax) * (y - ay) - (by - ay) * (x - ax) < 0: return False
        return True
    cut = 0
    for o in meshes:
        names_ = [sl.material.name if sl.material else "" for sl in o.material_slots]
        bm = bmesh.new(); bm.from_mesh(o.data)
        def near():
            out_ = []
            for f in bm.faces:
                if names_[f.material_index] not in shs: continue
                fx = [v.co.x for v in f.verts]; fy = [v.co.y for v in f.verts]
                if max(fx) >= min(xs_) and min(fx) <= max(xs_) and max(fy) >= min(ys_) and min(fy) <= max(ys_):
                    out_.append(f)
            return out_
        for k in range(len(poly)):
            (ax, ay), (bx, by) = poly[k], poly[(k + 1) % len(poly)]
            fs = near()
            if not fs: break
            geom = fs + list({e for f in fs for e in f.edges}) + list({v for f in fs for v in f.verts})
            bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(ax, ay, 0), plane_no=(by - ay, -(bx - ax), 0))
        kill = [f for f in near() if inside(f.calc_center_median().x, f.calc_center_median().y)]
        bmesh.ops.delete(bm, geom=kill, context='FACES'); cut += len(kill)
        bm.to_mesh(o.data); bm.free(); o.data.update()
    print(f"CUT {cut} faces inside a {len(poly)}-gon at ({sum(xs_) / len(xs_):.0f}, {sum(ys_) / len(ys_):.0f})")

# props that keep their own size when the hall is scaled (map.json "rescale"):
# scale the objects of those shaders by "factor" about an anchor on their joint bbox
# ("anchor" = fractions of the bbox: [0.5, 1, 0] = centre in x, back in +y, bottom)
# ("within" = [x0, y0, x1, y1]: only objects whose bbox centre is inside; "point" = fixed anchor in game units)
def bbox(objs):
    vs = [v.co for o in objs for v in o.data.vertices]
    return (mathutils.Vector([min(v[i] for v in vs) for i in range(3)]),
            mathutils.Vector([max(v[i] for v in vs) for i in range(3)]))
for rs in mdl.get("rescale", []):
    names = {f"textures/{shader_dir}/{s}" for s in rs["shaders"]}
    objs = [o for o in meshes if o.material_slots and all(s.material and s.material.name in names for s in o.material_slots)]
    if "within" in rs:
        x0, y0, x1, y1 = rs["within"]
        objs = [o for o in objs if x0 <= sum(bbox([o]), mathutils.Vector())[0] / 2 <= x1
                and y0 <= sum(bbox([o]), mathutils.Vector())[1] / 2 <= y1]
    if not objs:
        print("RESCALE: no objects for", rs["shaders"]); continue
    lo, hi = bbox(objs)
    a = mathutils.Vector(rs["point"]) if "point" in rs else \
        mathutils.Vector([lo[i] + (hi[i] - lo[i]) * rs.get("anchor", [0.5, 0.5, 0])[i] for i in range(3)])
    sm = mathutils.Matrix.Translation(a) @ mathutils.Matrix.Scale(rs["factor"], 4) @ mathutils.Matrix.Translation(-a)
    for o in objs:
        o.data.transform(sm); o.data.update()
    print(f"RESCALE {rs['shaders']} x{rs['factor']} about {tuple(round(c) for c in a)}: {len(objs)} objects")

# raise a storey (map.json "model": {"stretch": [{"center": [x, y], "r": u, "z0": u, "z1": u, "dz": u,
# "shaders": [...]}]}): vertices of those shaders within r of the centre move up by dz above z1 and by a
# proportional part of it between z0 and z1 (columns stretch, the roof above lifts, the floor stays)
for st in mdl.get("stretch", []):
    shs = {f"textures/{shader_dir}/{s}" for s in st["shaders"]}
    cx, cy = st["center"]; z0, z1, dz = st["z0"], st["z1"], st["dz"]; moved = 0
    for o in meshes:
        idx = {i for i, sl in enumerate(o.material_slots) if sl.material and sl.material.name in shs}
        vs = {v for p in o.data.polygons if p.material_index in idx for v in p.vertices}
        for vi in vs:
            co = o.data.vertices[vi].co
            if (co.x - cx) ** 2 + (co.y - cy) ** 2 < st["r"] ** 2 and co.z > z0:
                co.z += dz * min(1.0, (co.z - z0) / (z1 - z0)); moved += 1
        o.data.update()
    print(f"STRETCH {moved} vertices up to {dz} u at {st['center']}")

# flat plaza in the terrain (map.json "plaza": [{"rect": [x0, y0, x1, y1], "z": u, "margin": u, "step": u,
# "shaders": [...], "hole": [x0, y0, x1, y1]}]): slices the coarse terrain into a fine grid around the
# rect, levels it to z inside and blends back to the old height over the margin; "hole" is cut out
# (a sunken pool is built in it by the props)
for pz in cfg.get("plaza", []):
    import bmesh
    x0, y0, x1, y1 = pz["rect"]; Z = pz["z"]; M = pz.get("margin", 300); step = pz.get("step", 100)
    E = (x0 - M, y0 - M, x1 + M, y1 + M)
    shs = {f"textures/{shader_dir}/{s}" for s in pz["shaders"]}
    hole = pz.get("hole")
    cuts_x = sorted(set([E[0] + k * step for k in range(int((E[2] - E[0]) / step) + 1)] + [x0, x1] + ([hole[0], hole[2]] if hole else [])))
    cuts_y = sorted(set([E[1] + k * step for k in range(int((E[3] - E[1]) / step) + 1)] + [y0, y1] + ([hole[1], hole[3]] if hole else [])))
    for o in meshes:
        idx = {i for i, sl in enumerate(o.material_slots) if sl.material and sl.material.name in shs}
        if not idx: continue
        bm = bmesh.new(); bm.from_mesh(o.data)
        def region():
            out_ = []
            for f in bm.faces:
                if f.material_index not in idx: continue
                xs = [v.co.x for v in f.verts]; ys = [v.co.y for v in f.verts]
                if max(xs) >= E[0] and min(xs) <= E[2] and max(ys) >= E[1] and min(ys) <= E[3]:
                    out_.append(f)
            return out_
        for axis, cuts in ((0, cuts_x), (1, cuts_y)):
            for c in cuts:
                fs = region()
                geom = fs + list({e for f in fs for e in f.edges}) + list({v for f in fs for v in f.verts})
                no = (1, 0, 0) if axis == 0 else (0, 1, 0); co = (c, 0, 0) if axis == 0 else (0, c, 0)
                bmesh.ops.bisect_plane(bm, geom=geom, plane_co=co, plane_no=no)
        fs = region(); vs = {v for f in fs for v in f.verts}
        for v in vs:
            dx = max(x0 - v.co.x, 0, v.co.x - x1); dy = max(y0 - v.co.y, 0, v.co.y - y1)
            w = min(1.0, (dx * dx + dy * dy) ** 0.5 / M); w = w * w * (3 - 2 * w)
            v.co.z = Z + (v.co.z - Z) * w
        cut = []
        if hole:
            cut = [f for f in fs if hole[0] < f.calc_center_median().x < hole[2] and hole[1] < f.calc_center_median().y < hole[3]]
            bmesh.ops.delete(bm, geom=cut, context='FACES')
        bm.to_mesh(o.data); bm.free(); o.data.update()
        me = o.data   # vertex normals of the levelled faces: their own (up) face normal (q3map2 takes the plane from them)
        if me.has_custom_normals:
            loop_n = [tuple(c.vector) for c in me.corner_normals]
            for poly in me.polygons:
                if poly.material_index in idx and E[0] <= poly.center.x <= E[2] and E[1] <= poly.center.y <= E[3]:
                    for li in poly.loop_indices:
                        loop_n[li] = tuple(poly.normal)
            me.normals_split_custom_set(loop_n)
        print(f"PLAZA {pz['rect']} z {Z}: {len(fs)} terrain faces levelled, {len(cut)} cut for the hole")

def find_rooms(rc, bvh):
    """indoor spaces of the model (map.json "rooms": {"superellipse": [...], "cell": u, "min_cells": n,
    "skip": [[x, y, r], ...]}): grid columns where a floor has a ceiling 100..700 above it and walls all
    around at waist height; cells of one floor height join into rooms; each room gets a light and a few
    schematic furnishings (futon by a wall, low table with cushions, a cabinet) where they fit"""
    import random
    cx_, cy_, A_, B_, N_ = rc["superellipse"]; C_ = rc.get("cell", 48); rnd = random.Random(5)
    down = mathutils.Vector((0, 0, -1)); cells = {}
    def hits(x, y):
        out_ = []; o_ = mathutils.Vector((x, y, 6000))
        for _ in range(12):
            h = bvh.ray_cast(o_, down, 12000)
            if h[0] is None: break
            out_.append((h[0].z, h[1].z)); o_ = h[0] + down * 0.5
        return out_
    xs_ = [cx_ - A_ + C_ * k for k in range(int(2 * A_ / C_) + 1)]; ys_ = [cy_ - B_ + C_ * k for k in range(int(2 * B_ / C_) + 1)]
    for x in xs_:
        for y in ys_:
            if abs((x - cx_) / A_) ** N_ + abs((y - cy_) / B_) ** N_ > 1: continue
            if any(math.hypot(x - a, y - b) < r for a, b, r in rc.get("skip", [])): continue
            hs = hits(x, y)
            for k in range(1, len(hs)):
                zc, nc = hs[k - 1]; zf, nf = hs[k]
                if nf > 0.7 and 100 <= zc - zf <= 700:          # a floor with something overhead
                    zm = zf + 48; walls = 0
                    for a in range(0, 360, 45):
                        d_ = mathutils.Vector((math.cos(math.radians(a)), math.sin(math.radians(a)), 0))
                        h = bvh.ray_cast(mathutils.Vector((x, y, zm)), d_, 900)
                        walls += h[0] is not None
                    if walls >= 7:
                        cells[(round((x - xs_[0]) / C_), round((y - ys_[0]) / C_), round(zf / 16))] = (x, y, zf, zc)
    seen = set(); rooms = []
    for key in cells:
        if key in seen: continue
        st = [key]; seen.add(key); cur = []
        while st:
            k_ = st.pop(); cur.append(k_)
            for d_ in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                for dz in (-1, 0, 1):
                    q = (k_[0] + d_[0], k_[1] + d_[1], k_[2] + dz)
                    if q in cells and q not in seen: seen.add(q); st.append(q)
        if len(cur) >= rc.get("min_cells", 10): rooms.append(cur)
    props_ = []; lights_ = []
    for room in rooms:
        pts = [cells[k_] for k_ in room]; ij = {(k_[0], k_[1]) for k_ in room}
        zf = sorted(p_[2] for p_ in pts)[len(pts) // 2]; zc = min(p_[3] for p_ in pts)
        mx = sum(p_[0] for p_ in pts) / len(pts); my = sum(p_[1] for p_ in pts) / len(pts)
        ctr = min(pts, key=lambda p_: (p_[0] - mx) ** 2 + (p_[1] - my) ** 2)
        area = len(pts) * C_ * C_
        lights_.append({"origin": [round(ctr[0]), round(ctr[1]), round(zf + min(0.75 * (zc - zf), 120))],
                        "light": int(min(rc.get("light_max", 900), rc.get("light_base", 380) + area / 300)), "color": [1, 0.86, 0.66]})
        up_ = mathutils.Vector((0, 0, 1))
        def fits(x, y, hw, hd, yaw, hgt=40):  # footprint inside the room, on this floor, nothing of the model in it
            ca, sa = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
            for a_ in (-1, -0.5, 0, 0.5, 1):
                for b_ in (-1, -0.5, 0, 0.5, 1):
                    px_ = x + (a_ * (hw + 16)) * ca - (b_ * (hd + 16)) * sa; py_ = y + (a_ * (hw + 16)) * sa + (b_ * (hd + 16)) * ca
                    if abs(a_) == 1 or abs(b_) == 1:
                        if (round((px_ - xs_[0]) / C_), round((py_ - ys_[0]) / C_)) not in ij: return False
                    h = bvh.ray_cast(mathutils.Vector((px_, py_, zf + hgt + 20)), down, hgt + 40)
                    if h[0] is None or abs(h[0].z - zf) > 6: return False        # a step, a hole or clutter
                    h = bvh.ray_cast(mathutils.Vector((px_, py_, zf + 4)), up_, hgt + 20)
                    if h[0] is not None: return False                            # a shelf, a beam, a stair above
            return True
        if zf < rc.get("furnish_above", -40):    # under bridges / along the canal: light only, softer
            lights_[-1]["light"] = int(lights_[-1]["light"] * 0.6)
            continue
        if zf < rc.get("living_above", 120):     # ground floors (shops, halls): light only
            continue
        used = []
        def free(x, y, r): return all(math.hypot(x - u, y - v) > r + rr for u, v, rr in used)
        # wall cells: a 4-neighbour missing; the wall's inward normal from the missing side
        edge = []
        for (i_, j_) in ij:
            for (di, dj) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if (i_ + di, j_ + dj) not in ij:
                    edge.append((xs_[0] + i_ * C_, ys_[0] + j_ * C_, math.degrees(math.atan2(-dj, -di))))
        rnd.shuffle(edge)
        plan = [("futon", 60, 100, "p_futon", 30), ("shelf", 75, 20, None, 150)]
        if area > 120000: plan.append(("futon", 60, 100, "p_futon", 30))
        for kind, hw, hd, cloth, hgt in plan:
            for (x, y, nyaw) in edge:
                # futon: long side against the wall; shelf: back to the wall, facing in
                yaw = nyaw + 90 if kind == "futon" else nyaw - 90
                off = (hw if kind == "futon" else hd) + 20
                px_, py_ = x + math.cos(math.radians(nyaw)) * off, y + math.sin(math.radians(nyaw)) * off
                if fits(px_, py_, hw, hd, yaw, hgt) and free(px_, py_, max(hw, hd) + 30):
                    sz = [2 * hw, 2 * hd, 22] if kind == "futon" else [2 * hw, 2 * hd + 4, min(140, zc - zf - 30)]
                    props_.append({"type": "furniture", "kind": kind, "at": [round(px_), round(py_)], "z": round(zf), "size": sz,
                                   "yaw": round(yaw), "ground": False, **({"cloth": cloth} if cloth else {})})
                    used.append((px_, py_, max(hw, hd))); break
        if area > 50000 and fits(ctr[0], ctr[1], 90, 90, 0, 50) and free(ctr[0], ctr[1], 130):
            props_.append({"type": "furniture", "kind": "lowtable", "at": [round(ctr[0]), round(ctr[1])], "z": round(zf), "size": [110, 110, 34], "ground": False})
    return {"props": props_, "lights": lights_, "n": len(rooms)}


# generated cover / elevation props (map.json "props"), see props.py
gen_tex = {}   # shader short name -> textures_gen kind
if cfg.get("props"):
    def mat_for(short):
        full = f"textures/{shader_dir}/{short}"
        return bpy.data.materials.get(full) or bpy.data.materials.new(full)
    props_mod.UV_NAME = meshes[0].data.uv_layers.active.name if meshes[0].data.uv_layers else "UVMap"
    # terrain under each prop (map.json "ground_shaders"): sets _zmin / _zmax over its footprint
    gsh = {f"textures/{shader_dir}/{x}" for x in cfg.get("ground_shaders", [])}
    if gsh or cfg.get("ring"):
        import bmesh
        from mathutils.bvhtree import BVHTree
        bt = bmesh.new(); bs = bmesh.new()
        for o in meshes:
            m2 = o.data.copy(); m2.transform(o.matrix_world)
            names = [sl.material.name if sl.material else "" for sl in o.material_slots]
            tmp = bmesh.new(); tmp.from_mesh(m2)
            g = [f for f in tmp.faces if f.material_index < len(names) and names[f.material_index] in gsh]
            other = tmp.copy()
            bmesh.ops.delete(tmp, geom=[f for f in tmp.faces if f not in set(g)], context='FACES')
            gi = {f.index for f in g}
            bmesh.ops.delete(other, geom=[f for f in other.faces if f.index in gi or (f.material_index < len(names)
                             and names[f.material_index].split("/")[-1] in cfg.get("ignore_shaders", []))], context='FACES')
            mt = bpy.data.meshes.new("t"); tmp.to_mesh(mt); bt.from_mesh(mt)
            mo = bpy.data.meshes.new("o"); other.to_mesh(mo); bs.from_mesh(mo)
            tmp.free(); other.free()
        terrain = BVHTree.FromBMesh(bt); struct = BVHTree.FromBMesh(bs)
        ball = bt.copy(); mtmp = bpy.data.meshes.new("all"); bs.to_mesh(mtmp); ball.from_mesh(mtmp)
        solid_all = BVHTree.FromBMesh(ball)          # ground + structures: what ledges and rooms work against
        down = mathutils.Vector((0, 0, -1))
        def ground_z(x, y):
            h = terrain.ray_cast(mathutils.Vector((x, y, 30000)), down, 60000)
            return h[0].z if h[0] is not None else cfg.get("ground_default_z", 0)
        def occupied(x, y, r):
            for dx, dy in ((0, 0), (r, 0), (-r, 0), (0, r), (0, -r)):
                g = ground_z(x + dx, y + dy)
                h = struct.ray_cast(mathutils.Vector((x + dx, y + dy, 30000)), down, 60000)
                if h[0] is not None and h[0].z > g + 40:
                    return True
            return False
        props_mod.GROUND = ground_z; props_mod.STRUCT = solid_all   # crates on slopes, ledges hugging a wall
        if cfg.get("rooms"):
            rooms_found = find_rooms(cfg["rooms"], solid_all)
            cfg["props"] = list(cfg.get("props", [])) + rooms_found["props"]
            json.dump(rooms_found["lights"], open(os.path.join(out, "room_lights.json"), "w"), indent=0)
            print(f"ROOMS {rooms_found['n']} rooms: {len(rooms_found['props'])} furniture pieces, {len(rooms_found['lights'])} lights")
        if cfg.get("ring"):
            ring = props_mod.plan_ring(cfg["ring"], occupied)
            cfg["props"] = list(cfg.get("props", [])) + ring
            print(f"RING {len(ring)} elements, {sum(1 for e in ring if e.get('_pushed'))} pushed out of the way")
            json.dump(ring, open(os.path.join(out, "ring.json"), "w"), indent=0)
        for pr in cfg["props"]:
            if "type" not in pr or pr.get("ground") is False or "at" not in pr or pr["type"] in ("ground_plane", "ledges", "pool", "dust_ring", "waterway", "horizon_ring", "stones"):
                continue
            rad = pr.get("r") or (max(pr["size"][:2]) / 2 if "size" in pr else pr.get("len", 100) / 2)
            zs = [ground_z(pr["at"][0] + dx * rad, pr["at"][1] + dy * rad)
                  for dx in (-0.7, 0, 0.7) for dy in (-0.7, 0, 0.7)]
            pr["_zmin"], pr["_zmax"] = min(zs), max(zs) if pr["type"] not in ("adobe_tower", "adobe_block", "adobe_wall") else sorted(zs)[4]
    gen_tex.update(props_mod.build(cfg["props"], mat_for))
    json.dump(props_mod.SLABS, open(os.path.join(out, "slabs.json"), "w"), indent=0)     # walkway clips (bridges)
    json.dump(props_mod.WATER, open(os.path.join(out, "water.json"), "w"), indent=0)     # water volumes (streams)
    for short, kind in cfg.get("prop_textures", {}).items():   # e.g. {"p_rock": "sandrock"}
        if short in gen_tex:
            gen_tex[short] = kind
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    print(f"PROPS {sum('type' in p for p in cfg['props'])} built, shaders {sorted(gen_tex)}")

# join into one object, triangulate
bpy.ops.object.select_all(action='DESELECT')
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
obj.name = cfg["name"]
mod = obj.modifiers.new("tri", 'TRIANGULATE')
bpy.ops.object.modifier_apply(modifier=mod.name)

# everything outside the play area is backdrop (map.json "far": {"superellipse": [cx, cy, a, b, n], "margin": u,
# "exclude": [shaders]}): its faces move to "<shader>_far" (no collision, vertex lit: gen_map)
if cfg.get("far"):
    fc = cfg["far"]; cx_, cy_, A_, B_, N_ = fc["superellipse"]; mg = fc.get("margin", 0)
    excl = {f"textures/{shader_dir}/{x}" for x in fc.get("exclude", [])}
    me = obj.data; names_ = [sl.material.name for sl in obj.material_slots]; far_idx = {}; n_far = 0
    for poly in me.polygons:
        nm = names_[poly.material_index]
        if nm in excl or nm.endswith("_far"): continue
        c = poly.center
        if abs((c.x - cx_) / (A_ + mg)) ** N_ + abs((c.y - cy_) / (B_ + mg)) ** N_ > 1:
            if nm not in far_idx:
                m_ = bpy.data.materials.get(nm + "_far") or bpy.data.materials.new(nm + "_far")
                me.materials.append(m_); far_idx[nm] = len(me.materials) - 1
            poly.material_index = far_idx[nm]; n_far += 1
    me.update()
    print(f"FAR {n_far} faces -> backdrop shaders {sorted(os.path.basename(k) + '_far' for k in far_idx)}")

# small / thin loose parts of a shader (lamps, wires, signs, barrels on a building atlas) go to a copy of
# the shader "<name>_d" that has no collision: map.json "model": {"split_details": {"shaders": [...],
# "max_dim": u, "thin": u, "ribbon": u}} - a part is detail if its largest size < max_dim, or if it is
# thinner than "thin" and narrower than "ribbon" (wires, poles, railings, strings of flags); thin but
# wide parts (single-plane floors and roofs) and flat horizontal pieces (steps, wall tops) stay solid
if mdl.get("split_details"):
    import bmesh
    sd_cfg = mdl["split_details"]
    me = obj.data
    bm = bmesh.new(); bm.from_mesh(me); bm.faces.ensure_lookup_table()
    names = [sl.material.name for sl in obj.material_slots]
    targets = {i for i, n in enumerate(names) if n.split("/")[-1] in sd_cfg["shaders"]}
    copies = {}
    for i in targets:
        dn = names[i] + "_d"
        m2 = bpy.data.materials.get(dn) or bpy.data.materials.new(dn)
        me.materials.append(m2); copies[i] = len(me.materials) - 1
    seen = set(); moved = parts = 0
    for f in bm.faces:
        if f.index in seen or f.material_index not in targets: continue
        isl = []; st = [f]; seen.add(f.index)
        while st:
            g = st.pop(); isl.append(g)
            for e in g.edges:
                for h in e.link_faces:
                    if h.index not in seen and h.material_index == f.material_index:
                        seen.add(h.index); st.append(h)
        co = [v.co for g in isl for v in g.verts]
        ext = [max(c[k] for c in co) - min(c[k] for c in co) for k in range(3)]
        dims = sorted(ext)
        parts += 1
        # a flat horizontal piece (wall top strip, step, ledge) is walkable: never a detail
        sheet = ext[2] == dims[0] and min(ext[0], ext[1]) >= 16 and max(ext[0], ext[1]) >= 40
        if not sheet and (dims[2] < sd_cfg.get("max_dim", 64) or (dims[0] < sd_cfg.get("thin", 6) and dims[1] < sd_cfg.get("ribbon", 64))):
            for g in isl: g.material_index = copies[g.material_index]
            moved += 1
            continue
        # "regions": [{"within": [x0, y0, z0, x1, y1, z1], "above": z}]: parts lying wholly inside the box
        # lose their collision above z (a bumpy bridge deck: gen_map "slabs" give it a flat clip instead)
        for rg in sd_cfg.get("regions", []):
            b = rg["within"]
            if all(b[k] <= min(c[k] for c in co) and max(c[k] for c in co) <= b[k + 3] for k in range(3)):
                for g in isl:
                    if g.calc_center_median().z > rg["above"]:
                        g.material_index = copies[g.material_index]
                moved += 1
    bm.to_mesh(me); bm.free(); me.update()
    print(f"SPLIT {moved} of {parts} loose parts -> no-collision detail shaders")

# ground that must face up (map.json "model": {"face_up": [shaders]}): a few flipped triangles seed a
# whole meta surface that the game then back-face culls (a hole) and q3map2 leaves unlit
if mdl.get("face_up"):
    import bmesh
    names = {f"textures/{shader_dir}/{s}" for s in mdl["face_up"]}
    idx = {i for i, sl in enumerate(obj.material_slots) if sl.material and sl.material.name in names}
    bm = bmesh.new(); bm.from_mesh(obj.data)
    flip = [f for f in bm.faces if f.material_index in idx and f.normal.z < 0]
    bmesh.ops.reverse_faces(bm, faces=flip)
    bm.to_mesh(obj.data); bm.free(); obj.data.update()
    # the OBJ also carries the FBX's custom vertex normals, and q3map2 takes a model surface's plane from
    # them: give these faces their own (up) face normal, keep everyone else's normals as they are
    me = obj.data
    loop_n = [tuple(c.vector) for c in me.corner_normals]
    fixed = 0
    for poly in me.polygons:
        if poly.material_index in idx:
            n = tuple(poly.normal)
            for li in poly.loop_indices:
                if loop_n[li][2] < 0.5:
                    fixed += 1
                loop_n[li] = n
    me.normals_split_custom_set(loop_n)
    print(f"FACE_UP flipped {len(flip)} triangles, reset {fixed} downward vertex normals")

os.makedirs(out, exist_ok=True)
bpy.ops.wm.obj_export(filepath=os.path.join(out, "model.obj"), export_selected_objects=True,
                      # standard OBJ axes (Y up): q3map2 rotates assimp models from Y-up to Z-up itself
                      forward_axis='NEGATIVE_Z', up_axis='Y', export_materials=True, export_normals=True,
                      export_uv=True, path_mode='STRIP', apply_modifiers=True)

# textures: real alpha (min alpha < 128) -> TGA with alpha, else JPG
info = {}
tdir = os.path.join(out, "textures", shader_dir)
os.makedirs(tdir, exist_ok=True)
tri_count = {}
for p in obj.data.polygons:
    sh = obj.material_slots[p.material_index].material.name
    tri_count[sh] = tri_count.get(sh, 0) + 1
override = cfg.get("texture_override", {})     # shader short name -> textures_gen kind (e.g. concrete)
gen_dir = os.path.join(out, "gen"); os.makedirs(gen_dir, exist_ok=True)
sources = {}
for t, sh in shader_for_tex.items():
    short = os.path.basename(sh)
    sources[sh] = (textures_gen.make(override[short], os.path.join(gen_dir, short + ".jpg")), f"gen:{override[short]}") \
        if short in override else (os.path.join(texdir, t), t)
def _parent(n):                              # "<shader>_d", "<shader>_far", "<shader>_d_far" -> "<shader>"
    for suf in ("_far", "_d"):
        if n.endswith(suf): n = n[:-len(suf)]
    return n
for full_, p_ in terrain_maps.items():
    sources[full_] = (p_, "uv_terrain colour map")
for short, kind in cfg.get("gen_textures", {}).items():     # generated textures for model shaders (terrain)
    ext = ".png" if kind in textures_gen.ALPHA else ".jpg"
    sources[f"textures/{shader_dir}/{short}"] = (textures_gen.make(kind, os.path.join(gen_dir, short + ext)), f"gen:{kind}")
for short, spec in cfg.get("import_textures", {}).items():  # other images: {"path": p, "crop": [x0, y0, x1, y1]}
    spec = {"path": spec} if isinstance(spec, str) else spec
    im_ = Image.open(spec["path"])
    if "crop" in spec:
        im_ = im_.crop(tuple(spec["crop"]))
    if "resize" in spec:
        im_ = im_.resize(tuple(spec["resize"]), Image.LANCZOS)
    if spec.get("key_sky"):                     # sky above a panorama -> transparent (per column, from the top)
        import numpy as np
        a_ = np.asarray(im_.convert("RGBA")).copy(); r_, g_, b_ = (a_[..., k].astype(int) for k in range(3))
        sky = ((b_ - r_) > spec["key_sky"][0]) & (b_ > spec["key_sky"][1])
        keep = np.cumsum(~sky, axis=0) > 0
        a_[..., 3] = np.where(keep, 255, 0); im_ = Image.fromarray(a_, "RGBA")
    p_ = os.path.join(gen_dir, short + ".png"); im_.save(p_)
    sources[f"textures/{shader_dir}/{short}"] = (p_, spec["path"])
for short, parent in cfg.get("texture_copy", {}).items():   # (also done below: before the suffix copies here)
    full = f"textures/{shader_dir}/{short}"; pf = f"textures/{shader_dir}/{parent}"
    if pf in sources:
        sources[full] = sources[pf]
for sl in obj.material_slots:               # detail / backdrop copies use their parent's texture
    n = sl.material.name
    if n != _parent(n) and _parent(n) in sources:
        sources[n] = sources[_parent(n)]
for short, kind in gen_tex.items():
    ext = ".png" if kind in textures_gen.ALPHA else ".jpg"     # dust etc. keep their alpha -> TGA
    sources[f"textures/{shader_dir}/{short}"] = (textures_gen.make(kind, os.path.join(gen_dir, short + ext)), f"gen:{kind}")
for short, parent in cfg.get("texture_copy", {}).items():   # a generated shader reusing a model texture
    full = f"textures/{shader_dir}/{short}"; pf = f"textures/{shader_dir}/{parent}"
    if pf in sources:
        sources[full] = sources[pf]
recolor = cfg.get("texture_recolor", {})                  # short -> [[dark rgb], [light rgb]] (keeps alpha)
for sh, (src, t) in sources.items():
    pfull = f"textures/{shader_dir}/{_parent(os.path.basename(sh))}"
    if pfull != sh and pfull in info:                     # detail / backdrop copies share the parent's file
        info[sh] = dict(info[pfull], tris=tri_count.get(sh, 0)); continue
    im = Image.open(src)
    if os.path.basename(sh) in recolor:
        import numpy as np
        dk, lt = (np.array(c, float) for c in recolor[os.path.basename(sh)])
        a_ = np.asarray(im.convert("RGBA")).astype(float)
        lum = (a_[..., :3] @ [0.3, 0.59, 0.11]) / 255.0
        lum = (lum - lum.min()) / max(1e-6, lum.max() - lum.min())
        rgb = dk + (lt - dk) * lum[..., None]
        im = Image.fromarray(np.concatenate([rgb, a_[..., 3:]], 2).clip(0, 255).astype(np.uint8), "RGBA")
    alpha = im.mode in ('RGBA', 'LA', 'P') and im.convert('RGBA').getchannel('A').getextrema()[0] < 128
    base = os.path.basename(sh)
    if _parent(base) in cfg.get("texture_opaque", []):
        alpha = False                                     # atlas: cut-out texels went to their own shader
    tsz = cfg.get("texture_size", {}).get(base) or cfg.get("texture_size", {}).get(_parent(base))
    if tsz and im.size != (tsz, tsz):
        im = im.convert("RGBA").resize((tsz, tsz), Image.LANCZOS)
    if alpha:
        fn = base + ".tga"
        im.convert('RGBA').save(os.path.join(tdir, fn))
    else:
        fn = base + ".jpg"
        im.convert('RGB').save(os.path.join(tdir, fn), quality=90)
    info[sh] = {"source": t, "file": f"textures/{shader_dir}/{fn}", "alpha": bool(alpha),
                "size": list(im.size), "tris": tri_count.get(sh, 0)}
json.dump(info, open(os.path.join(out, "materials.json"), "w"), indent=1)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "model.blend"))
print("PREP OK", len(obj.data.polygons), "tris,", len(info), "shaders")
