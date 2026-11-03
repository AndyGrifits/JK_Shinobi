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
        if cfg.get("ring"):
            ring = props_mod.plan_ring(cfg["ring"], occupied)
            cfg["props"] = list(cfg.get("props", [])) + ring
            print(f"RING {len(ring)} elements, {sum(1 for e in ring if e.get('_pushed'))} pushed out of the way")
            json.dump(ring, open(os.path.join(out, "ring.json"), "w"), indent=0)
        for pr in cfg["props"]:
            if "type" not in pr or pr.get("ground") is False or pr["type"] == "ground_plane":
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
for sl in obj.material_slots:               # "<shader>_d" detail copies use their parent's texture
    n = sl.material.name
    if n.endswith("_d") and n[:-2] in sources:
        sources[n] = sources[n[:-2]]
for short, kind in gen_tex.items():
    sources[f"textures/{shader_dir}/{short}"] = (textures_gen.make(kind, os.path.join(gen_dir, short + ".jpg")), f"gen:{kind}")
for short, parent in cfg.get("texture_copy", {}).items():   # a generated shader reusing a model texture
    full = f"textures/{shader_dir}/{short}"; pf = f"textures/{shader_dir}/{parent}"
    if pf in sources:
        sources[full] = sources[pf]
recolor = cfg.get("texture_recolor", {})                  # short -> [[dark rgb], [light rgb]] (keeps alpha)
for sh, (src, t) in sources.items():
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
