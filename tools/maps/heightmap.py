"""Raycast a top-down heightmap of a prepared model (model.blend from prep_model.py).
    python3 tools/maps/heightmap.py <out dir> [step]   -> <out>/heightmap.json  (+ ascii dump)"""
import bpy, sys, json, os
from mathutils.bvhtree import BVHTree
from mathutils import Vector
out = sys.argv[-2] if len(sys.argv) > 2 and sys.argv[-1].isdigit() else sys.argv[-1]
step = int(sys.argv[-1]) if sys.argv[-1].isdigit() else 8
bpy.ops.wm.open_mainfile(filepath=os.path.join(out, "model.blend"))
o = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
dg = bpy.context.evaluated_depsgraph_get()
bvh = BVHTree.FromObject(o, dg)
vs = [o.matrix_world @ v.co for v in o.data.vertices]
lo = [int(min(v[i] for v in vs)) // step * step for i in range(2)]; hi = [int(max(v[i] for v in vs)) for i in range(2)]
xs = range(lo[0], hi[0] + 1, step); ys = range(lo[1], hi[1] + 1, step)
top = max(v.z for v in vs) - 6   # just under the ceiling
grid = {}
for y in ys:
    for x in xs:
        hit = bvh.ray_cast(Vector((x, y, top)), Vector((0, 0, -1)), 4000)
        if hit[0] is None: grid[f"{x},{y}"] = None
        else: grid[f"{x},{y}"] = [round(hit[0].z, 1), round(hit[1].z, 2), o.material_slots[o.data.polygons[hit[2]].material_index].material.name.split('/')[-1]]
json.dump({"step": step, "top": top, "grid": grid}, open(os.path.join(out, "heightmap.json"), "w"))
print("HM OK", len(grid))
