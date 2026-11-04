"""Fill a model's open channels with water brushes up to a level (map.json "water_fill").

    python3 -I tools/maps/waterfill.py maps/<name>/map.json <build dir>   -> <build>/waterfill.json

"water_fill": {"z": top of the water, "floor": bottom of the brushes, "area": [x0, y0, x1, y1], "cell": u,
               "min_depth": u}
A cell is water when a ray from just under the water level finds solid ground at least min_depth below it
(so a bridge or a tunnel roof above doesn't matter). Cells are merged into row strips (axis-aligned boxes).
"""
import json, os, sys
import bpy, bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree

cfg_path, build = sys.argv[-2:]
cfg = json.load(open(cfg_path)); wf = cfg.get("water_fill")
out = os.path.join(build, "waterfill.json")
if not wf:
    json.dump([], open(out, "w")); print("WATERFILL off"); sys.exit(0)
bpy.ops.wm.open_mainfile(filepath=os.path.join(build, "model.blend"))
o = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
names = [s.material.name.split("/")[-1] for s in o.material_slots]
solid = {k for k, v in cfg["shaders"].items() if v.get("solid") or "q3map_clipModel" in v.get("raw", [])}
bm = bmesh.new(); bm.from_mesh(o.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if names[f.material_index] not in solid], context='FACES')
bvh = BVHTree.FromBMesh(bm)
Z = wf["z"]; F = wf.get("floor", Z - 200); C = wf.get("cell", 64); x0, y0, x1, y1 = wf["area"]; md = wf.get("min_depth", 24)
boxes = []
y = y0
while y < y1:
    run = None; x = x0
    while x <= x1:
        wet = False
        if x < x1:
            h = bvh.ray_cast(Vector((x + C / 2, y + C / 2, Z - 1)), Vector((0, 0, -1)), 4000)
            wet = h[0] is not None and h[0].z < Z - md and h[0].z > F - 400
        if wet and run is None:
            run = x
        elif not wet and run is not None:
            boxes.append({"box": [run, y, F, x, y + C, Z]}); run = None
        x += C
    y += C
json.dump(boxes, open(out, "w"), indent=0)
print(f"WATERFILL {len(boxes)} strips at z {Z}")
