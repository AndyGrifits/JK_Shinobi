#!/usr/bin/env python3
"""Compare a compiled map's collision (solid brushes in the BSP) with the visible model.

    python3 tools/maps/collision_check.py <map.bsp> <build dir> [cx cy r]   (r: only inside that circle)

Uses <build>/heightmap.json (tools/maps/heightmap.py: what you SEE from above) and computes the
same grid from the BSP brushes (what you STAND on). Prints an ASCII diff and the worst spots:
  '.' match (<= 10u)   '^' collision higher than the visual   'v' collision lower / missing
  ' ' outside the model
"""
import json, os, struct, sys
import numpy as np

bsp, build = sys.argv[1:3]
circle = [float(v) for v in sys.argv[3:6]] if len(sys.argv) >= 6 else None   # optional: only cx cy r
data = open(bsp, "rb").read()
lumps = [struct.unpack_from("<ii", data, 8 + i * 8) for i in range(18)]
def lump(i): o, l = lumps[i]; return data[o:o + l]
shaders = [struct.unpack_from("<64sii", lump(1), i) for i in range(0, len(lump(1)), 72)]
contents = [s[2] for s in shaders]
planes = np.frombuffer(lump(2), dtype="<f4").reshape(-1, 4)
brushes = np.frombuffer(lump(8), dtype="<i4").reshape(-1, 3)        # firstSide numSides shader
sides = np.frombuffer(lump(9), dtype="<i4").reshape(-1, 3)          # plane shader drawSurf (RBSP)
SOLID, PLAYERCLIP = 1, 0x10

hm = json.load(open(os.path.join(build, "heightmap.json")))
keys = list(hm["grid"].keys())
pts = np.array([[float(v) for v in k.split(",")] for k in keys])
vis = np.array([np.nan if hm["grid"][k] is None else hm["grid"][k][0] for k in keys])
if circle:
    vis[(pts[:, 0] - circle[0]) ** 2 + (pts[:, 1] - circle[1]) ** 2 > circle[2] ** 2] = np.nan
START = hm.get("top", 370.0)
top = np.full(len(pts), -1e9)
n_used = 0
for first, num, sh in brushes:
    if not (contents[sh] & (SOLID | PLAYERCLIP)):
        continue
    P = planes[sides[first:first + num, 0]]
    n = P[:, :3]; d = P[:, 3]
    # vertical extent of the brush along the column at each (x, y)
    lo = np.full(len(pts), -1e9); hi = np.full(len(pts), 1e9); ok = np.ones(len(pts), bool)
    for (nx, ny, nz), dd in zip(n, d):
        rhs = dd - nx * pts[:, 0] - ny * pts[:, 1]
        if abs(nz) < 1e-6:
            ok &= rhs >= -0.01
        elif nz > 0:
            hi = np.minimum(hi, rhs / nz)
        else:
            lo = np.maximum(lo, rhs / nz)
    ok &= hi > lo
    # first surface hit going down from START: the brush's top below START (or START if we start inside)
    hit = ok & (lo < START)
    z = np.where(hi < START, hi, START)
    top = np.where(hit & (z > top), z, top)
    n_used += 1
col = np.where(top < -1e8, np.nan, top)
diff = col - vis
print(f"solid brushes {n_used}; grid {len(pts)} pts, step {hm['step']}")
step = hm["step"]
xs = sorted({p[0] for p in pts}); ys = sorted({p[1] for p in pts}, reverse=True)
idx = {k: i for i, k in enumerate(keys)}
bad = []
rows = []
for y in ys[::2]:
    line = ""
    for x in xs[::2]:
        i = idx[f"{int(x)},{int(y)}"]
        if np.isnan(vis[i]):
            line += " "
        elif np.isnan(col[i]) or diff[i] < -10:
            line += "v"; bad.append((diff[i] if not np.isnan(diff[i]) else -999, x, y, vis[i], col[i]))
        elif diff[i] > 10:
            line += "^"; bad.append((diff[i], x, y, vis[i], col[i]))
        else:
            line += "."
    rows.append(f"{int(y):5d} {line}")
print("\n".join(rows))
inside = ~np.isnan(vis)
m = inside & ~np.isnan(col) & (np.abs(diff) <= 10)
print(f"match {m.sum()}/{inside.sum()} ({100 * m.sum() / inside.sum():.1f}%)")
bad.sort(key=lambda b: -abs(b[0]))
for dz, x, y, v, c in [b for b in bad if b[0] < 0][:10]:
    print(f"  LOW ({x:5.0f},{y:5.0f}) visual {v:6.1f}  collision {c:6.1f}  d {dz:+.0f}")
for dz, x, y, v, c in bad[:15]:
    print(f"  ({x:5.0f},{y:5.0f}) visual {v:6.1f}  collision {c:6.1f}  d {dz:+.0f}")
