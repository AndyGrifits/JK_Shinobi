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
if wf.get("mode") == "segments":
    # few convex prisms along the channel instead of many strips (each can be a fog volume): wet cells ->
    # largest connected component -> centre line by bins along its main axis -> mitred quads of its width
    import numpy as np, math
    cells = set()
    for b in boxes:
        x0_, y0_, _, x1_, _, _ = b["box"]
        for x in np.arange(x0_, x1_, C):
            cells.add((int(round((x - x0) / C)), int(round((b["box"][1] - y0) / C))))
    comp = []; seen = set()
    for c in cells:
        if c in seen: continue
        st = [c]; seen.add(c); cur = []
        while st:
            a = st.pop(); cur.append(a)
            for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                q = (a[0] + d[0], a[1] + d[1])
                if q in cells and q not in seen: seen.add(q); st.append(q)
        comp.append(cur)
    big = max(comp, key=len)
    P = np.array([[x0 + (i + 0.5) * C, y0 + (j + 0.5) * C] for i, j in big])
    m = P.mean(0); U, S_, Vt = np.linalg.svd(P - m, full_matrices=False); ax = Vt[0]; pr = np.array([-ax[1], ax[0]])
    t = (P - m) @ ax; s_ = (P - m) @ pr; nb = wf.get("bins", 8)
    edges = np.linspace(t.min(), t.max(), nb + 1); cen = []; half = []
    for k in range(nb):
        sel = (t >= edges[k]) & (t <= edges[k + 1])
        if sel.sum() < 3: continue
        cen.append(P[sel].mean(0)); half.append(np.abs((P[sel] - P[sel].mean(0)) @ pr).max() + C)
    cen[0] = cen[0] - ax * (t.min() - ((cen[0] - m) @ ax)) * 0 - ax * C    # reach the very ends
    cen[-1] = cen[-1] + ax * C
    W = wf.get("half_width") or max(half) + wf.get("pad", 120)
    def norm(v): return v / np.linalg.norm(v)
    dirs = [norm(cen[k + 1] - cen[k]) for k in range(len(cen) - 1)]
    def side(k):                                   # mitred offset direction at centre point k
        if k == 0: d = dirs[0]
        elif k == len(cen) - 1: d = dirs[-1]
        else: d = norm(dirs[k - 1] + dirs[k])
        n_ = np.array([-d[1], d[0]]); dd = dirs[min(k, len(dirs) - 1)]
        return n_ / max(0.5, abs(n_ @ np.array([-dd[1], dd[0]])))
    segs = []
    for k in range(len(cen) - 1):
        a, b = cen[k], cen[k + 1]; na, nb_ = side(k) * W, side(k + 1) * W
        quad = [a - na, b - nb_, b + nb_, a + na]
        area = sum(quad[i][0] * quad[(i + 1) % 4][1] - quad[(i + 1) % 4][0] * quad[i][1] for i in range(4))
        if area < 0: quad = quad[::-1]
        segs.append({"poly": [[round(float(q[0]), 1), round(float(q[1]), 1)] for q in quad], "z0": F, "z1": Z})
    boxes = segs
json.dump(boxes, open(out, "w"), indent=0)
print(f"WATERFILL {len(boxes)} {'segments' if wf.get('mode') == 'segments' else 'strips'} at z {Z}")
