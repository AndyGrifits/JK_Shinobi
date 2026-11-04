"""Flat renders of a PREPARED model (build/model.blend + its textures) before the q3map2 compile: quick
look at retexturing, props and layout. Cameras as in bsp_render.py.

    python3 -I tools/maps/preview_prep.py <build dir> <out dir> '<cams json>' [hide shader,shader...]
    cams: [{"from": [x,y,z], "to": [x,y,z], "lens": 18, "name": "a.png"}]  or {"top": true, "center": [x,y], "size": u}
"""
import json, os, sys
import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
build, outdir, cams = args[0], args[1], json.loads(args[2])
hide = set(args[3].split(",")) if len(args) > 3 else set()
bpy.ops.wm.open_mainfile(filepath=os.path.join(build, "model.blend"))
o = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
if hide:
    import bmesh
    names = [s.material.name.split("/")[-1] for s in o.material_slots]
    bm = bmesh.new(); bm.from_mesh(o.data)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if names[f.material_index] in hide], context='FACES')
    bm.to_mesh(o.data)
for sl in o.material_slots:
    m = sl.material
    if not m: continue
    m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes): nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial'); em = nt.nodes.new('ShaderNodeEmission')
    p = None
    for ext in (".jpg", ".tga", ".png"):
        q = os.path.join(build, m.name + ext)
        if os.path.exists(q): p = q; break
    if p is None and m.name.endswith(("_far", "_d")):
        base = m.name[:-4] if m.name.endswith("_far") else m.name[:-2]
        for ext in (".jpg", ".tga"):
            q = os.path.join(build, base + ext)
            if os.path.exists(q): p = q; break
    if p:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = bpy.data.images.load(p); nt.links.new(t.outputs[0], em.inputs[0])
        if p.endswith(".tga"):
            tr = nt.nodes.new('ShaderNodeBsdfTransparent'); mix = nt.nodes.new('ShaderNodeMixShader')
            gt = nt.nodes.new('ShaderNodeMath'); gt.operation = 'GREATER_THAN'; gt.inputs[1].default_value = 0.5
            nt.links.new(t.outputs[1], gt.inputs[0]); nt.links.new(gt.outputs[0], mix.inputs[0])
            nt.links.new(tr.outputs[0], mix.inputs[1]); nt.links.new(em.outputs[0], mix.inputs[2]); nt.links.new(mix.outputs[0], out.inputs[0])
            continue
    else:
        em.inputs[0].default_value = (1, 0, 1, 1)
    nt.links.new(em.outputs[0], out.inputs[0])
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.samples = 6; sc.cycles.transparent_max_bounces = 16; sc.cycles.max_bounces = 0
sc.view_settings.view_transform = 'Standard'
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
w.node_tree.nodes['Background'].inputs[0].default_value = (0.5, 0.68, 0.92, 1)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam
cam.data.clip_end = 1e6
os.makedirs(outdir, exist_ok=True)
for c in cams:
    sc.render.resolution_x = c.get("w", 1000); sc.render.resolution_y = c.get("h", 600)
    if c.get("top"):
        cam.data.type = 'ORTHO'; cam.data.ortho_scale = c["size"]; cam.location = (c["center"][0], c["center"][1], c.get("z", 9000))
        cam.rotation_euler = (0, 0, 0); cam.data.clip_start = 2
    else:
        cam.data.type = 'PERSP'; cam.data.lens = c.get("lens", 18); cam.location = c["from"]; cam.data.clip_start = 2
        cam.rotation_euler = (Vector(c["to"]) - Vector(c["from"])).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = os.path.join(outdir, c["name"]); bpy.ops.render.render(write_still=True)
print("PREVIEW OK")
