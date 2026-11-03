"""Find staircases in a prepared model and cover each with one smooth ramp brush (gen_map "slabs"), so
players glide up instead of hopping step by step (the model and the camera jitter on real steps).

    python3 -I tools/maps/stairs.py maps/<name>/map.json <build dir>   -> <build>/stairs.json

map.json "stairs": {"shaders": [...], "area": [x0, y0, x1, y1], "cell": 8, "min_cells": 16,
                    "skip": [[x, y, r], ...]}
A cell is "stair" when the top surface (of those shaders) around it climbs at 12..45 deg in flat treads
and risers of 5..24 u; connected stair cells become one ramp: a plane fitted to the treads, raised to the
step nosings, running from the floor at the foot to the landing at the top.
"""
import json, math, os, sys
import bpy, bmesh
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

cfg_path, build = sys.argv[-2:]
cfg = json.load(open(cfg_path)); sc = cfg.get("stairs")
out_path = os.path.join(build, "stairs.json")
if not sc:
    json.dump([], open(out_path, "w")); print("STAIRS off"); sys.exit(0)
bpy.ops.wm.open_mainfile(filepath=os.path.join(build, "model.blend"))
o = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
names = [s.material.name.split('/')[-1] for s in o.material_slots]
solid = {k for k, v in cfg["shaders"].items() if v.get("solid")}
bm = bmesh.new(); bm.from_mesh(o.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if names[f.material_index] not in solid], context='FACES')
bm.faces.ensure_lookup_table()
bvh = BVHTree.FromBMesh(bm)
want = set(sc["shaders"])
C = sc.get("cell", 8); x0, y0, x1, y1 = sc["area"]
xs = np.arange(x0, x1, C); ys = np.arange(y0, y1, C)
H = np.full((len(ys), len(xs)), np.nan); OK = np.zeros(H.shape, bool)
for j, y in enumerate(ys):
    for i, x in enumerate(xs):
        h = bvh.ray_cast(Vector((x, y, 9000)), Vector((0, 0, -1)), 20000)
        if h[0] is not None:
            H[j, i] = h[0].z; OK[j, i] = bm.faces[h[2]].normal.z > 0.9 and \
                names[bm.faces[h[2]].material_index] in want

# stair cells: a 5 x 5 window of treads (flat neighbours) and risers (5..24), climbing 12..45 deg overall
R = 2
st = np.zeros(H.shape, bool)
for j in range(R, len(ys) - R):
    for i in range(R, len(xs) - R):
        w = H[j - R:j + R + 1, i - R:i + R + 1]
        if np.isnan(w).any() or not OK[j - R:j + R + 1, i - R:i + R + 1].all():
            continue
        d = np.concatenate([np.abs(np.diff(w, axis=0)).ravel(), np.abs(np.diff(w, axis=1)).ravel()])
        if d.max() > 24 or (d > 4.5).sum() < 2 or (d < 1.0).mean() < 0.45:
            continue
        gy, gx = np.polyfit(np.arange(-R, R + 1), w.mean(axis=1), 1)[0], np.polyfit(np.arange(-R, R + 1), w.mean(axis=0), 1)[0]
        slope = math.hypot(gx, gy) / C
        if 0.21 <= slope <= 1.0:
            st[j, i] = True

# connected components (8-neighbour)
lab = np.zeros(H.shape, int); n = 0
for j, i in zip(*np.nonzero(st)):
    if lab[j, i]: continue
    n += 1; stack = [(j, i)]; lab[j, i] = n
    while stack:
        a, b = stack.pop()
        for da in (-1, 0, 1):
            for db in (-1, 0, 1):
                p, q = a + da, b + db
                if 0 <= p < H.shape[0] and 0 <= q < H.shape[1] and st[p, q] and not lab[p, q]:
                    lab[p, q] = n; stack.append((p, q))

def top(x, y):
    h = bvh.ray_cast(Vector((x, y, 9000)), Vector((0, 0, -1)), 20000)
    return h[0].z if h[0] is not None else None

ramps = []
for k in range(1, n + 1):
    jj, ii = np.nonzero(lab == k)
    if len(jj) < sc.get("min_cells", 16): continue
    # grow by the window radius: the detector marks the centres of stair windows only
    P = np.array([[xs[i], ys[j], H[j, i]] for j, i in zip(jj, ii)])
    if any(math.hypot(P[:, 0].mean() - sx, P[:, 1].mean() - sy) < sr for sx, sy, sr in sc.get("skip", [])):
        continue
    A = np.c_[P[:, 0], P[:, 1], np.ones(len(P))]
    (a, b, c), *_ = np.linalg.lstsq(A, P[:, 2], rcond=None)
    g = np.array([a, b]); gl = np.linalg.norm(g)
    if gl < 0.2: continue
    u = g / gl; v = np.array([-u[1], u[0]])
    t = P[:, :2] @ u; s = P[:, :2] @ v
    t0, t1 = t.min() - R * C, t.max() + R * C
    s0, s1 = np.percentile(s, 3) - C, np.percentile(s, 97) + C
    plane = lambda tt, ss: a * (tt * u[0] + ss * v[0]) + b * (tt * u[1] + ss * v[1]) + c
    res = P[:, 2] - (A @ np.array([a, b, c]))
    lift = float(min(14, max(0, res.max())))          # through the nosings: no step edge sticks out
    sm = (s0 + s1) / 2
    # foot and landing: extend along the axis until the ramp meets the floor below / the landing above
    def ground_at(tt):
        p = tt * u + sm * v
        return top(p[0], p[1])
    lo_t, hi_t = t0, t1
    for _ in range(12):
        z = ground_at(lo_t - 4)
        if z is None or plane(lo_t, sm) + lift <= z + 2: break
        lo_t -= 8
    for _ in range(12):
        z = ground_at(hi_t + 4)
        if z is None or plane(hi_t, sm) + lift >= z - 2: break
        hi_t += 8
    za = plane(lo_t, sm) + lift; zb = plane(hi_t, sm) + lift
    pa = lo_t * u + sm * v; pb = hi_t * u + sm * v
    deg = math.degrees(math.atan2(zb - za, hi_t - lo_t))
    ramps.append({"from": [round(float(pa[0]), 1), round(float(pa[1]), 1), round(float(za), 1)], "to": [round(float(pb[0]), 1), round(float(pb[1]), 1), round(float(zb), 1)],
                  "width": round(float(s1 - s0), 1), "thick": round(float(zb - za + 24), 1), "shader": sc.get("clip", "clip_stairs"),
                  "_cells": int(len(jj)), "_deg": round(deg, 1), "_rms": round(float(np.sqrt((res ** 2).mean())), 1)})
# drop smooth slopes (no treads) and small detections inside a bigger ramp's footprint
ramps = [r for r in ramps if r["_rms"] >= 1.0]
def inside(r, big):
    a, b = np.array(big["from"][:2]), np.array(big["to"][:2]); L = np.linalg.norm(b - a); u = (b - a) / L; v = np.array([-u[1], u[0]])
    c = (np.array(r["from"][:2]) + np.array(r["to"][:2])) / 2 - a
    return -8 <= c @ u <= L + 8 and abs(c @ v) <= big["width"] / 2 + 8
ramps.sort(key=lambda r: -r["_cells"])
keep = []
for r in ramps:
    if not any(inside(r, k_) for k_ in keep):
        keep.append(r)
ramps = keep
json.dump(ramps, open(out_path, "w"), indent=1)
for r in ramps:
    print(f"  ramp {r['from']} -> {r['to']} width {r['width']} {r['_deg']} deg ({r['_cells']} cells, rms {r['_rms']})")
print(f"STAIRS {len(ramps)} ramps")
