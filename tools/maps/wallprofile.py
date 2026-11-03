"""Horizontal raycasts from the hall's centre line outwards: wall x per y at given heights.
    python3 tools/maps/wallprofile.py <out dir>"""
import bpy, sys, os, json
from mathutils.bvhtree import BVHTree
from mathutils import Vector
out = sys.argv[-1]
bpy.ops.wm.open_mainfile(filepath=os.path.join(out, "model.blend"))
o = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
bvh = BVHTree.FromObject(o, bpy.context.evaluated_depsgraph_get())
res = {}
for z in (40, 100, 150, 230, 300, 360):
    rows = []
    for y in range(-640, 641, 16):
        r = []
        for d in (-1, 1):
            h = bvh.ray_cast(Vector((0, y, z)), Vector((d, 0, 0)), 1000)
            r.append(None if h[0] is None else round(h[0].x, 1))
        # along y from centre
        rows.append((y, r))
    res[z] = rows
for z in (100, 230):
    print("z", z)
    for y, r in res[z]:
        if y % 32 == 0: print(f"  y {y:5d}: L {r[0]}  R {r[1]}")
# end walls along y at x=0 and x=+-150
for z in (40, 230):
    for x in (-300, -150, 0, 150, 300):
        a = bvh.ray_cast(Vector((x, 0, z)), Vector((0, 1, 0)), 1000); b = bvh.ray_cast(Vector((x, 0, z)), Vector((0, -1, 0)), 1000)
        print(f"z {z} x {x}: +Y {None if a[0] is None else round(a[0].y,1)}  -Y {None if b[0] is None else round(b[0].y,1)}")
# ceiling
for x, y in ((0, 0), (300, 0), (0, -500), (0, 500)):
    h = bvh.ray_cast(Vector((x, y, 50)), Vector((0, 0, 1)), 1000); print("ceiling at", x, y, None if h[0] is None else round(h[0].z, 1))
json.dump(res, open(os.path.join(out, "walls.json"), "w"))
