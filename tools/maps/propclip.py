"""Stepped clip boxes for props (statues, bushes...) whose own triangles are too dense to autoclip.

    python3 tools/maps/propclip.py maps/<name>/map.json <build dir>   (Blender python, after prep_model.py)

Raycasts down onto the triangles of the shaders listed in map.json "prop_clip" only, on a
grid of "cell" units; each cell gets a box from the floor up to the highest point in it
(rounded up to 8). Equal-height neighbours are merged into rectangles. -> <build>/propclip.json
"mode": "trunk": one octagonal column per tree trunk up to the fork (roots and leaves stay walk-through).
"mode": "shell" (e.g. trees): the box starts at the lowest point seen from below instead of the
floor, so a canopy is a slab you can stand on and walk under, and the trunk stays a column.
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

def trunks(group, obj):
    """mode "trunk": one octagonal column per tree trunk, from below the floor up to the fork.
    Trunk = cluster of the group's vertices inside the height "band" (above the roots); radius =
    90th percentile there (>= min_r); fork = first 24 u slice above the band whose spread > fork_ratio x r."""
    import math
    import numpy as np
    shaders = {f"textures/{cfg['shader_dir']}/{s}" for s in group["shaders"]}
    idx = {i for i, s in enumerate(obj.material_slots) if s.material and s.material.name in shaders}
    vs = {v for p in obj.data.polygons if p.material_index in idx for v in p.vertices}
    P = np.array([obj.data.vertices[i].co[:] for i in vs])
    b0, b1 = group.get("band", [40, 120])
    low = P[(P[:, 2] > b0) & (P[:, 2] < b1)]
    cl = []
    for p in low:
        for c in cl:
            if math.hypot(p[0] - c[0][0], p[1] - c[0][1]) < 120:
                c[1].append(p); c[0] = np.mean(c[1], 0)[:2]; break
        else:
            cl.append([p[:2].copy(), [p]])
    prisms = []
    for c, pts in cl:
        q = np.array(pts); ctr = q[:, :2].mean(0)
        r = max(group.get("min_r", 16), float(np.percentile(np.hypot(*(q[:, :2] - ctr).T), 90)) + 2)
        h = group.get("max_h", 200)
        for z in range(int(b1), int(group.get("max_h", 200)), 8):
            sl = P[(P[:, 2] >= z) & (P[:, 2] < z + 24) & (np.hypot(P[:, 0] - ctr[0], P[:, 1] - ctr[1]) < 150)]
            if len(sl) and np.percentile(np.hypot(sl[:, 0] - ctr[0], sl[:, 1] - ctr[1]), 90) > group.get("fork_ratio", 2.5) * r:
                h = z + 8; break
        poly = [(float(ctr[0] + r * math.cos(math.pi / 8 + k * math.pi / 4)), float(ctr[1] + r * math.sin(math.pi / 8 + k * math.pi / 4)))
                for k in range(8)]
        prisms.append({"poly": poly, "z0": group.get("floor", 0) - 8, "z1": int(h)})
        tr = group.get("top_r")
        if tr and tr > r:   # a small landing at the fork (the branches spread there anyway)
            top = [(float(ctr[0] + tr * math.cos(math.pi / 8 + k * math.pi / 4)), float(ctr[1] + tr * math.sin(math.pi / 8 + k * math.pi / 4)))
                   for k in range(8)]
            prisms.append({"poly": top, "z0": int(h) - 8, "z1": int(h)})
        print(f"TRUNK at ({ctr[0]:.0f},{ctr[1]:.0f}) r {r:.0f} up to {h}")
    return prisms

groups = []
for group in pc["groups"]:
    if group.get("mode") == "trunk":
        groups.append({"shader": group["clip_shader"], "boxes": [], "prisms": trunks(group, obj)})
        print(f"PROPCLIP {group['clip_shader']}: {len(groups[-1]['prisms'])} trunk columns")
        continue
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
    shell = group.get("mode") == "shell"
    hmap = {}
    sub = 4
    rx0, ry0, rx1, ry1 = group.get("region", [-1e9, -1e9, 1e9, 1e9])
    for cy in range(y0, y1, cell):
        for cx in range(x0, x1, cell):
            if not (rx0 <= cx and cx + cell <= rx1 and ry0 <= cy and cy + cell <= ry1):
                continue
            top = None; bottom = None
            for i in range(sub + 1):
                for j in range(sub + 1):
                    p = Vector((cx + cell * i / sub, cy + cell * j / sub, 20000))
                    h = bvh.ray_cast(p, Vector((0, 0, -1)), 40000)
                    if h[0] is not None and (top is None or h[0].z > top):
                        top = h[0].z
                    if shell:
                        h = bvh.ray_cast(Vector((p.x, p.y, floor - 200)), Vector((0, 0, 1)), 40000)
                        if h[0] is not None and (bottom is None or h[0].z < bottom):
                            bottom = h[0].z
            if top is not None and top > floor + group.get("min_height", 10):
                b = floor - 8
                if shell and bottom is not None and bottom > floor + 24:
                    b = int(bottom // 8 * 8)
                hmap[(cx, cy)] = (b, int(-(-top // 8) * 8))
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
        boxes.append([cx, cy, h[0], cx + w * cell, cy + d * cell, h[1]])
    groups.append({"shader": group["clip_shader"], "boxes": boxes})
    bm.free()
    print(f"PROPCLIP {group['clip_shader']}: {len(hmap)} cells -> {len(boxes)} boxes")
json.dump(groups, open(os.path.join(build, "propclip.json"), "w"), indent=0)
