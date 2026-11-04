"""Loading-screen picture for a model-based map: flat-lit render of model.blend.

    python3 tools/maps/levelshot.py maps/<name>/map.json <build dir>
    -> <build>/base/levelshots/<name>.jpg   (Blender python; replace with an in-game shot later)
"""
import json, os, sys
import bpy
from mathutils import Vector

cfg_path, build = sys.argv[-2:]
cfg = json.load(open(cfg_path))
ls = cfg.get("levelshot", {"from": [0, -560, 90], "to": [0, 450, 110], "lens": 16})
bpy.ops.wm.open_mainfile(filepath=os.path.join(build, "model.blend"))
obj = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
texdir = os.path.join(build, "textures", cfg["shader_dir"])
for slot in obj.material_slots:
    mat = slot.material; mat.use_nodes = True; nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial'); em = nt.nodes.new('ShaderNodeEmission')
    short = mat.name.split('/')[-1]
    cand = [short]
    for suf in ("_far", "_d"):                 # derived shaders share their parent's texture file
        if cand[-1].endswith(suf): cand.append(cand[-1][:-len(suf)])
    fs = [x for c in cand for x in os.listdir(texdir) if x.startswith(c + '.')]
    if not fs:
        nt.links.new(em.outputs[0], out.inputs[0]); continue
    f = fs[0]
    t = nt.nodes.new('ShaderNodeTexImage'); t.image = bpy.data.images.load(os.path.join(texdir, f))
    nt.links.new(t.outputs[0], em.inputs[0])
    if f.endswith(".tga"):   # alpha-tested in game: cut out here too
        tr = nt.nodes.new('ShaderNodeBsdfTransparent'); mix = nt.nodes.new('ShaderNodeMixShader')
        gt = nt.nodes.new('ShaderNodeMath'); gt.operation = 'GREATER_THAN'; gt.inputs[1].default_value = 0.5
        nt.links.new(t.outputs[1], gt.inputs[0]); nt.links.new(gt.outputs[0], mix.inputs[0])
        nt.links.new(tr.outputs[0], mix.inputs[1]); nt.links.new(em.outputs[0], mix.inputs[2])
        nt.links.new(mix.outputs[0], out.inputs[0])
    else:
        nt.links.new(em.outputs[0], out.inputs[0])
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 16; sc.cycles.transparent_max_bounces = 32
if cfg.get("sky"):   # background = the skybox gradient (horizon -> zenith)
    w = bpy.data.worlds.new("sky"); sc.world = w; w.use_nodes = True; wt = w.node_tree
    tc = wt.nodes.new('ShaderNodeTexCoord'); sep = wt.nodes.new('ShaderNodeSeparateXYZ')
    ramp = wt.nodes.new('ShaderNodeValToRGB')
    hor = [c / 255 for c in cfg["sky"].get("horizon", [196, 222, 245])] + [1]
    zen = [c / 255 for c in cfg["sky"].get("zenith", [62, 120, 214])] + [1]
    ramp.color_ramp.elements[0].color = hor; ramp.color_ramp.elements[1].color = zen
    ramp.color_ramp.elements[1].position = 0.8
    wt.links.new(tc.outputs['Generated'], sep.inputs[0]); wt.links.new(sep.outputs[2], ramp.inputs[0])
    wt.links.new(ramp.outputs[0], wt.nodes['Background'].inputs[0])
sc.view_settings.view_transform = 'Standard'
sc.render.resolution_x = 1024; sc.render.resolution_y = 768
sc.render.image_settings.file_format = 'JPEG'; sc.render.image_settings.quality = 88
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam
cam.data.lens = ls.get("lens", 16); cam.data.clip_end = 10000
cam.location = ls["from"]
cam.rotation_euler = (Vector(ls["to"]) - Vector(ls["from"])).to_track_quat('-Z', 'Y').to_euler()
os.makedirs(os.path.join(build, "base", "levelshots"), exist_ok=True)
sc.render.filepath = os.path.join(build, "base", "levelshots", cfg["name"] + ".jpg")
bpy.ops.render.render(write_still=True)
print("LEVELSHOT OK", sc.render.filepath)
