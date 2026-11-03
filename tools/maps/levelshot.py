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
    f = [x for x in os.listdir(texdir) if x.startswith(short + '.')][0]
    t = nt.nodes.new('ShaderNodeTexImage'); t.image = bpy.data.images.load(os.path.join(texdir, f))
    nt.links.new(t.outputs[0], em.inputs[0]); nt.links.new(em.outputs[0], out.inputs[0])
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 16
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
