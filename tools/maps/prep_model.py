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
import json, os, sys

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
# [u0, v0, u1, v1], "region": [x0, y0, x1, y1], "step": u, "classes": {short: [r, g, b]}, "tile": {short: u}}]}):
# faces whose UVs lie in uv_box are sliced into a step grid inside region, each piece gets the class whose
# colour is nearest to the overview texel under it, and world-planar UVs for that class's tiled texture
for ut in mdl.get("uv_terrain", []):
    import bmesh, numpy as np
    full = f"textures/{shader_dir}/{ut['shader']}"
    IMG = np.asarray(Image.open(tex_path_of[full]).convert("RGB")).astype(float); Hh, Ww = IMG.shape[:2]
    u0, v0, u1, v1 = ut["uv_box"]; R = ut["region"]; step = ut.get("step", 128)
    names_ = list(ut["classes"]); ref = []; ref_cls = []     # a class may list several reference colours
    for k_, n_ in enumerate(names_):
        cl = ut["classes"][n_]; cl = cl if isinstance(cl[0], list) else [cl]
        ref += cl; ref_cls += [k_] * len(cl)
    cols = np.array(ref, float)
    for o in meshes:
        if not any(sl.material and sl.material.name == full for sl in o.material_slots): continue
        si = _slot(o, full); ci = [_slot(o, f"textures/{shader_dir}/{n_}") for n_ in names_]
        bm = bmesh.new(); bm.from_mesh(o.data); uvl = bm.loops.layers.uv.active
        def terr():
            out_ = []
            for f in bm.faces:
                if f.material_index != si: continue
                xs_ = [v.co.x for v in f.verts]; ys_ = [v.co.y for v in f.verts]
                if max(xs_) < R[0] or min(xs_) > R[2] or max(ys_) < R[1] or min(ys_) > R[3]: continue
                c = sum((l[uvl].uv for l in f.loops), mathutils.Vector((0, 0))) / len(f.loops)
                if u0 <= c.x <= u1 and v0 <= c.y <= v1:
                    out_.append(f)
            return out_
        for axis in (0, 1):
            for k_ in range(int((R[2 + axis] - R[axis]) / step) + 1):
                cc = R[axis] + k_ * step; fs = terr()
                geom = fs + list({e for f in fs for e in f.edges}) + list({v for f in fs for v in f.verts})
                bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(cc, 0, 0) if axis == 0 else (0, cc, 0),
                                       plane_no=(1, 0, 0) if axis == 0 else (0, 1, 0))
        fs = terr(); cnt = [0] * len(names_)
        for f in fs:
            c = sum((l[uvl].uv for l in f.loops), mathutils.Vector((0, 0))) / len(f.loops)
            px = IMG[min(Hh - 1, int((1 - c.y % 1.0) * Hh)), min(Ww - 1, int((c.x % 1.0) * Ww))]
            k_ = ref_cls[int(np.argmin(((cols - px) ** 2).sum(1)))]; cnt[k_] += 1
            f.material_index = ci[k_]; tl = ut.get("tile", {}).get(names_[k_], 256)
            for l in f.loops:
                l[uvl].uv = (l.vert.co.x / tl, l.vert.co.y / tl)
        bm.to_mesh(o.data); bm.free(); o.data.update()
        print(f"UV_TERRAIN {len(fs)} pieces: " + ", ".join(f"{n_} {c_}" for n_, c_ in zip(names_, cnt)))

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
        props_mod.GROUND = ground_z; props_mod.STRUCT = struct   # crates on slopes, ledges hugging a wall
        if cfg.get("ring"):
            ring = props_mod.plan_ring(cfg["ring"], occupied)
            cfg["props"] = list(cfg.get("props", [])) + ring
            print(f"RING {len(ring)} elements, {sum(1 for e in ring if e.get('_pushed'))} pushed out of the way")
            json.dump(ring, open(os.path.join(out, "ring.json"), "w"), indent=0)
        for pr in cfg["props"]:
            if "type" not in pr or pr.get("ground") is False or pr["type"] in ("ground_plane", "ledges", "pool", "dust_ring"):
                continue
            rad = pr.get("r") or (max(pr["size"][:2]) / 2 if "size" in pr else pr.get("len", 100) / 2)
            zs = [ground_z(pr["at"][0] + dx * rad, pr["at"][1] + dy * rad)
                  for dx in (-0.7, 0, 0.7) for dy in (-0.7, 0, 0.7)]
            pr["_zmin"], pr["_zmax"] = min(zs), max(zs) if pr["type"] not in ("adobe_tower", "adobe_block", "adobe_wall") else sorted(zs)[4]
    gen_tex.update(props_mod.build(cfg["props"], mat_for))
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
