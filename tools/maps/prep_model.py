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

cfg_path, fbx, texdir, out = sys.argv[-4:]
cfg = json.load(open(cfg_path))
mdl = cfg["model"]
shader_dir = cfg["shader_dir"]

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
for t, sh in shader_for_tex.items():
    im = Image.open(os.path.join(texdir, t))
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
