"""Stepped clip boxes for props (statues, bushes...) whose own triangles are too dense to autoclip.

    python3 tools/maps/propclip.py maps/<name>/map.json <build dir>   (Blender python, after prep_model.py)

Raycasts down onto the triangles of the shaders listed in map.json "prop_clip" only, on a
grid of "cell" units; each cell gets a box from the floor up to the highest point in it
(rounded up to 8). Equal-height neighbours are merged into rectangles. -> <build>/propclip.json
"""
import json, os, sys
import bpy
import bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree

cfg_path, build = sys.argv[-2:]
cfg = json.load(open(cfg_path))
pc = cfg["prop_clip"]
cell = pc.get("cell", 16)
bpy.ops.wm.open_mainfile(filepath=os.path.join(build, "model.blend"))
obj = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]

groups = []
for group in pc["groups"]:
    shaders = {f"textures/{cfg['shader_dir']}/{s}" for s in group["shaders"]}
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    keep = {i for i, s in enumerate(obj.material_slots) if s.material and s.material.name in shaders}
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index not in keep], context='FACES')
    if not bm.faces:
        continue
    bvh = BVHTree.FromBMesh(bm)
    xs = [v.co.x for v in bm.verts]; ys = [v.co.y for v in bm.verts]
    x0 = int(min(xs) // cell) * cell; x1 = int(max(xs) // cell + 1) * cell
    y0 = int(min(ys) // cell) * cell; y1 = int(max(ys) // cell + 1) * cell
    floor = group.get("floor", 0)
    hmap = {}
    sub = 4
    rx0, ry0, rx1, ry1 = group.get("region", [-1e9, -1e9, 1e9, 1e9])
    for cy in range(y0, y1, cell):
        for cx in range(x0, x1, cell):
            if not (rx0 <= cx and cx + cell <= rx1 and ry0 <= cy and cy + cell <= ry1):
                continue
            top = None
            for i in range(sub + 1):
                for j in range(sub + 1):
                    p = Vector((cx + cell * i / sub, cy + cell * j / sub, 2000))
                    h = bvh.ray_cast(p, Vector((0, 0, -1)), 4000)
                    if h[0] is not None and (top is None or h[0].z > top):
                        top = h[0].z
            if top is not None and top > floor + group.get("min_height", 10):
                hmap[(cx, cy)] = int(-(-top // 8) * 8)
    # greedy merge: rows first, then stack equal rows
    boxes = []
    used = set()
    for (cx, cy), h in sorted(hmap.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        if (cx, cy) in used:
            continue
        w = 1
        while hmap.get((cx + w * cell, cy)) == h and (cx + w * cell, cy) not in used:
            w += 1
        d = 1
        while all(hmap.get((cx + k * cell, cy + d * cell)) == h and (cx + k * cell, cy + d * cell) not in used
                  for k in range(w)):
            d += 1
        for k in range(w):
            for m in range(d):
                used.add((cx + k * cell, cy + m * cell))
        boxes.append([cx, cy, floor - 8, cx + w * cell, cy + d * cell, h])
    groups.append({"shader": group["clip_shader"], "boxes": boxes})
    bm.free()
    print(f"PROPCLIP {group['clip_shader']}: {len(hmap)} cells -> {len(boxes)} boxes")
json.dump(groups, open(os.path.join(build, "propclip.json"), "w"), indent=0)
