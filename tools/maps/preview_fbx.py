"""Flat-lit preview renders of a raw FBX or .blend (before prep): top ortho + 3 perspectives + per-object labels.
    python3 tools/maps/preview_fbx.py <model.fbx> <texture dir> <out dir> [cam json]"""
import bpy, sys, os, json, math, mathutils
args = sys.argv[1:]
fbx, texdir, out = args[:3]
cams = json.loads(args[3]) if len(args) > 3 else None
os.makedirs(out, exist_ok=True)
if fbx.lower().endswith(".blend"):
    bpy.ops.wm.open_mainfile(filepath=fbx)
    for o in list(bpy.context.scene.objects):
        if o.type == 'ARMATURE': bpy.data.objects.remove(o, do_unlink=True)
        elif o.type == 'MESH':
            for m in list(o.modifiers): o.modifiers.remove(m)
    skip = set(cams[0].get("hide", [])) if cams and isinstance(cams, list) and cams and "hide" in cams[0] else set()
    for o in list(bpy.context.scene.objects):
        if o.name in skip: bpy.data.objects.remove(o, do_unlink=True)
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=fbx)
for mat in bpy.data.materials:
    if not mat.node_tree: continue
    nt = mat.node_tree; img = None
    for n in nt.nodes:
        if n.type == 'TEX_IMAGE' and n.image: img = n.image
    if img:
        p = os.path.join(texdir, os.path.basename(img.filepath))
        if os.path.exists(p): img.filepath = p; img.reload()
    for n in list(nt.nodes): nt.nodes.remove(n)
    o = nt.nodes.new('ShaderNodeOutputMaterial'); em = nt.nodes.new('ShaderNodeEmission')
    tr = nt.nodes.new('ShaderNodeBsdfTransparent'); mix = nt.nodes.new('ShaderNodeMixShader')
    if img:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = img; nt.links.new(t.outputs[0], em.inputs[0])
        nt.links.new(t.outputs[1], mix.inputs[0]); nt.links.new(tr.outputs[0], mix.inputs[1]); nt.links.new(em.outputs[0], mix.inputs[2])
    else:
        mix.inputs[0].default_value = 1; nt.links.new(em.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], o.inputs[0])
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
mn = mathutils.Vector((1e9,) * 3); mx = -mn
for o in meshes:
    for c in o.bound_box:
        w = o.matrix_world @ mathutils.Vector(c); mn = mathutils.Vector(map(min, mn, w)); mx = mathutils.Vector(map(max, mx, w))
ctr = (mn + mx) / 2; size = mx - mn
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 6; sc.cycles.transparent_max_bounces = 16
sc.view_settings.view_transform = 'Standard'
sc.render.resolution_x = 1100; sc.render.resolution_y = 1000
w = bpy.data.worlds.new('w'); sc.world = w; w.use_nodes = True; w.node_tree.nodes['Background'].inputs[0].default_value = (0.35, 0.5, 0.75, 1)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam
cam.data.clip_end = 1e6; cam.data.clip_start = 0.01 * max(size)
cam.data.type = 'ORTHO'; cam.data.ortho_scale = max(size.x, size.y) * 1.05
cam.location = (ctr.x, ctr.y, mx.z + size.z); cam.rotation_euler = (0, 0, 0)
sc.render.filepath = os.path.join(out, 'top.png'); bpy.ops.render.render(write_still=True)
cam.data.type = 'PERSP'; sc.render.resolution_x = 1200; sc.render.resolution_y = 750
def look(name, loc, tgt, lens=20):
    cam.location = loc; cam.data.lens = lens
    cam.rotation_euler = (mathutils.Vector(tgt) - mathutils.Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = os.path.join(out, name); bpy.ops.render.render(write_still=True)
if cams:
    for c in cams:
        if "name" in c: look(c["name"], c["from"], c["to"], c.get("lens", 20))
else:
    look('persp.png', (ctr.x + size.x * 0.9, ctr.y - size.y * 0.9, mx.z + size.z * 1.5), tuple(ctr), 22)
print("CTR", tuple(round(c, 1) for c in ctr), "SIZE", tuple(round(c, 1) for c in size))
