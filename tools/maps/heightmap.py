"""Raycast a top-down heightmap of a prepared model (model.blend from prep_model.py).
    python3 tools/maps/heightmap.py <out dir> [step]   -> <out>/heightmap.json  (+ ascii dump)
Only shaders marked "solid" in maps/<name>/map.json count (sky dome, dust, details are skipped), so the
result is what you should stand on (collision_check.py compares it with the compiled brushes)."""
import bpy, bmesh, sys, json, os
from mathutils.bvhtree import BVHTree
from mathutils import Vector
out = sys.argv[-2] if len(sys.argv) > 2 and sys.argv[-1].isdigit() else sys.argv[-1]
step = int(sys.argv[-1]) if sys.argv[-1].isdigit() else 8
bpy.ops.wm.open_mainfile(filepath=os.path.join(out, "model.blend"))
o = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
names = [s.material.name.split('/')[-1] for s in o.material_slots]
cfgp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "maps", os.path.basename(os.path.normpath(out)), "map.json")
solid = None
if os.path.exists(cfgp):
    solid = {k for k, v in json.load(open(cfgp)).get("shaders", {}).items() if v.get("solid")}
bm = bmesh.new(); bm.from_mesh(o.data); bm.transform(o.matrix_world)
if solid is not None:
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if names[f.material_index] not in solid], context='FACES')
bm.faces.ensure_lookup_table()
bvh = BVHTree.FromBMesh(bm)
vs = [v.co for v in bm.verts]
lo = [int(min(v[i] for v in vs)) // step * step for i in range(2)]; hi = [int(max(v[i] for v in vs)) for i in range(2)]
xs = range(lo[0], hi[0] + 1, step); ys = range(lo[1], hi[1] + 1, step)
top = max(v.z for v in vs) - 6   # just under the ceiling
grid = {}
for y in ys:
    for x in xs:
        hit = bvh.ray_cast(Vector((x, y, top)), Vector((0, 0, -1)), 8000)
        if hit[0] is None: grid[f"{x},{y}"] = None
        else: grid[f"{x},{y}"] = [round(hit[0].z, 1), round(hit[1].z, 2), names[bm.faces[hit[2]].material_index]]
json.dump({"step": step, "top": top, "grid": grid}, open(os.path.join(out, "heightmap.json"), "w"))
print("HM OK", len(grid))
