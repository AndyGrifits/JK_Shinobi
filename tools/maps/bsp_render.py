"""Render a compiled RBSP map roughly the way the game draws it: texture x lightmap (vertex colour for
vertex-lit surfaces), alpha-tested shaders cut out, sky = background gradient. For checking a build
without starting the game.

    python3 -I tools/maps/bsp_render.py <map.bsp> <base dir> <out.png> <cams json>
    cams: [{"from": [x,y,z], "to": [x,y,z], "lens": 18, "name": "a.png"}]  or {"top": true, ...}
"""
import json, os, struct, sys
import numpy as np
import bpy
from mathutils import Vector

bsp, base, outdir, cams = sys.argv[-4], sys.argv[-3], sys.argv[-2], json.loads(sys.argv[-1])
d = open(bsp, "rb").read()
L = [struct.unpack_from("<ii", d, 8 + i * 8) for i in range(18)]
shaders = [(s[:64].split(b"\0")[0].decode(), sf) for s, sf in
           (struct.unpack_from("<64si", d, L[1][0] + i) for i in range(0, L[1][1], 72))]
V = np.frombuffer(d[L[10][0]:L[10][0] + L[10][1]], np.uint8).reshape(-1, 80)
xyz = V[:, 0:12].copy().view("<f4").reshape(-1, 3)
st = V[:, 12:20].copy().view("<f4").reshape(-1, 2)
lmuv = V[:, 20:28].copy().view("<f4").reshape(-1, 2)
col = V[:, 64:68].astype(np.float32) / 255
I = np.frombuffer(d[L[11][0]:L[11][0] + L[11][1]], "<i4")
o, l = L[14]
lms = np.frombuffer(d[o:o + l], np.uint8).reshape(-1, 128, 128, 3)

bpy.ops.wm.read_factory_settings(use_empty=True)
lm_imgs = []
for k, m in enumerate(lms):
    img = bpy.data.images.new(f"lm{k}", 128, 128)
    rgba = np.concatenate([m[::-1].astype(np.float32) / 255, np.ones((128, 128, 1), np.float32)], 2)
    img.pixels = rgba.ravel(); img.pack(); lm_imgs.append(img)

def find_tex(name):
    for ext in (".jpg", ".tga", ".png"):
        p = os.path.join(base, name + ext)
        if os.path.exists(p):
            return p
    return None

# unlit shaders (nolightmap and no $lightmap stage: sky domes, horizon cards) draw the texture as is
import glob, re
fullbright = set()
for shf in glob.glob(os.path.join(base, "shaders", "*.shader")):
    for nm, body in re.findall(r"^(\S+)\s*\n\{(.*?)\n\}", open(shf).read(), re.S | re.M):
        if "nolightmap" in body and "$lightmap" not in body and "nodraw" not in body:
            fullbright.add(nm)

# group triangles by (shader, lightmap)
groups = {}
for i in range(L[13][0], L[13][0] + L[13][1], 148):
    s, fog, typ, fv, nv, fi, ni = struct.unpack_from("<7i", d, i)
    lmn = struct.unpack_from("<i", d, i + 36)[0]
    name, sflags = shaders[s]
    if sflags & 0x200000 or "sky" in name:   # nodraw / sky
        continue
    groups.setdefault((name, lmn), []).append((fv, fi, ni))
mats = {}
verts_all = []; faces_all = []; uv0 = []; uv1 = []; vc = []; mat_idx = []; mat_list = []
for (name, lmn), surfs in groups.items():
    key = (name, lmn)
    mi = len(mat_list); mat_list.append(key)
    for fv, fi, ni in surfs:
        idx = fv + I[fi:fi + ni]
        for t in range(0, ni, 3):
            base_i = len(verts_all)
            tri = idx[t:t + 3]
            for v in tri:
                verts_all.append(tuple(xyz[v])); uv0.append(tuple(st[v])); uv1.append(tuple(lmuv[v])); vc.append(tuple(col[v]))
            faces_all.append((base_i, base_i + 1, base_i + 2)); mat_idx.append(mi)
me = bpy.data.meshes.new("bsp")
me.from_pydata(verts_all, [], faces_all)
u0 = me.uv_layers.new(name="tex"); u1 = me.uv_layers.new(name="lm")
for li, loop in enumerate(me.loops):
    vi = loop.vertex_index
    u0.data[li].uv = (uv0[vi][0], 1 - uv0[vi][1]); u1.data[li].uv = (uv1[vi][0], 1 - uv1[vi][1])
ca = me.color_attributes.new("vc", 'FLOAT_COLOR', 'POINT')
for vi, c in enumerate(vc):
    ca.data[vi].color = (c[0], c[1], c[2], 1)
for p, mi in zip(me.polygons, mat_idx):
    p.material_index = mi
for name, lmn in mat_list:
    m = bpy.data.materials.new(f"{name}|{lmn}"); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes): nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial'); em = nt.nodes.new('ShaderNodeEmission')
    mul = nt.nodes.new('ShaderNodeMixRGB'); mul.blend_type = 'MULTIPLY'; mul.inputs[0].default_value = 1
    p = find_tex(name)
    alpha = None
    if p:
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = bpy.data.images.load(p)
        uvn = nt.nodes.new('ShaderNodeUVMap'); uvn.uv_map = "tex"; nt.links.new(uvn.outputs[0], t.inputs[0])
        t.extension = 'REPEAT'; nt.links.new(t.outputs[0], mul.inputs[1]); alpha = t.outputs[1] if p.endswith(".tga") else None
    else:
        mul.inputs[1].default_value = (1, 0, 1, 1)   # missing texture: magenta
    if name in fullbright:
        mul.inputs[2].default_value = (1, 1, 1, 1)
    elif lmn >= 0:
        lt = nt.nodes.new('ShaderNodeTexImage'); lt.image = lm_imgs[lmn]; lt.interpolation = 'Linear'
        uvl = nt.nodes.new('ShaderNodeUVMap'); uvl.uv_map = "lm"; nt.links.new(uvl.outputs[0], lt.inputs[0])
        nt.links.new(lt.outputs[0], mul.inputs[2])
    else:
        a = nt.nodes.new('ShaderNodeVertexColor'); a.layer_name = "vc"; nt.links.new(a.outputs[0], mul.inputs[2])
    nt.links.new(mul.outputs[0], em.inputs[0])
    if alpha is not None:
        tr = nt.nodes.new('ShaderNodeBsdfTransparent'); mix = nt.nodes.new('ShaderNodeMixShader')
        gt = nt.nodes.new('ShaderNodeMath'); gt.operation = 'GREATER_THAN'; gt.inputs[1].default_value = 0.5
        nt.links.new(alpha, gt.inputs[0]); nt.links.new(gt.outputs[0], mix.inputs[0])
        nt.links.new(tr.outputs[0], mix.inputs[1]); nt.links.new(em.outputs[0], mix.inputs[2]); nt.links.new(mix.outputs[0], out.inputs[0])
    else:
        nt.links.new(em.outputs[0], out.inputs[0])
    me.materials.append(m)
ob = bpy.data.objects.new("bsp", me); bpy.context.scene.collection.objects.link(ob)
sc = bpy.context.scene
sc.render.engine = 'CYCLES'; sc.cycles.samples = 8; sc.cycles.transparent_max_bounces = 32; sc.cycles.max_bounces = 0
sc.view_settings.view_transform = 'Standard'
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
w.node_tree.nodes['Background'].inputs[0].default_value = (0.55, 0.72, 0.95, 1)
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam')); sc.collection.objects.link(cam); sc.camera = cam
cam.data.clip_end = 1e6; cam.data.clip_start = 2
os.makedirs(outdir, exist_ok=True)
for c in cams:
    sc.render.resolution_x = c.get("w", 1100); sc.render.resolution_y = c.get("h", 700)
    if c.get("top"):
        cam.data.type = 'ORTHO'; cam.data.ortho_scale = c["size"]; cam.location = (c["center"][0], c["center"][1], c.get("z", 5000))
        cam.rotation_euler = (0, 0, 0); cam.data.clip_start = c.get("clip", 2)
    else:
        cam.data.type = 'PERSP'; cam.data.lens = c.get("lens", 18); cam.location = c["from"]; cam.data.clip_start = 2
        cam.rotation_euler = (Vector(c["to"]) - Vector(c["from"])).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = os.path.join(outdir, c["name"]); bpy.ops.render.render(write_still=True)
print("BSP RENDER OK", len(faces_all), "tris", len(mat_list), "materials")
