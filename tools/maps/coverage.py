#!/usr/bin/env python3
"""Holes in a compiled map's floor: rasterise the BSP's (drawn) triangles below z_max onto a grid and
list grid cells inside a circle / box that nothing covers.
    python3 tools/maps/coverage.py <map.bsp> <z_max> <step> circle <cx> <cy> <r>
"""
import struct, sys
import numpy as np
d = open(sys.argv[1], "rb").read()
zmax, step = float(sys.argv[2]), float(sys.argv[3]); cx, cy, R = map(float, sys.argv[5:8])
L = [struct.unpack_from("<ii", d, 8 + i * 8) for i in range(18)]
sh = [struct.unpack_from("<64si", d, L[1][0] + i) for i in range(0, L[1][1], 72)]
V = np.frombuffer(d[L[10][0]:L[10][0] + L[10][1]], dtype=np.uint8).reshape(-1, 80)
xyz = V[:, :12].copy().view("<f4").reshape(-1, 3)
I = np.frombuffer(d[L[11][0]:L[11][0] + L[11][1]], "<i4")
n = int(2 * R / step) + 1
xs = cx - R + np.arange(n) * step; ys = cy - R + np.arange(n) * step
cov = np.zeros((n, n), bool); owner = {}
for i in range(L[13][0], L[13][0] + L[13][1], 148):
    s, fog, typ, fv, nv, fi, ni = struct.unpack_from("<7i", d, i)
    name = sh[s][0].split(b"\0")[0].decode()
    if sh[s][1] & 0x200000:   # nodraw
        continue
    tri = xyz[fv + I[fi:fi + ni]].reshape(-1, 3, 3)
    for t in tri:
        if t[:, 2].max() > zmax:
            continue
        x0, x1 = t[:, 0].min(), t[:, 0].max(); y0, y1 = t[:, 1].min(), t[:, 1].max()
        i0 = max(0, int((x0 - xs[0]) / step)); i1 = min(n - 1, int((x1 - xs[0]) / step) + 1)
        j0 = max(0, int((y0 - ys[0]) / step)); j1 = min(n - 1, int((y1 - ys[0]) / step) + 1)
        if i1 < i0 or j1 < j0:
            continue
        PX, PY = np.meshgrid(xs[i0:i1 + 1], ys[j0:j1 + 1], indexing="ij")
        (ax, ay), (bx, by), (qx, qy) = t[:, :2]
        def side(px, py, ux, uy, vx, vy): return (vx - ux) * (py - uy) - (vy - uy) * (px - ux)
        s1 = side(PX, PY, ax, ay, bx, by); s2 = side(PX, PY, bx, by, qx, qy); s3 = side(PX, PY, qx, qy, ax, ay)
        inside = ((s1 >= 0) & (s2 >= 0) & (s3 >= 0)) | ((s1 <= 0) & (s2 <= 0) & (s3 <= 0))
        cov[i0:i1 + 1, j0:j1 + 1] |= inside
GX, GY = np.meshgrid(xs, ys, indexing="ij")
want = (GX - cx) ** 2 + (GY - cy) ** 2 <= R * R
holes = want & ~cov
print(f"cells {want.sum()}, uncovered {holes.sum()} ({100 * holes.sum() / want.sum():.2f}%)")
if holes.any():
    from scipy import ndimage
    lab, k = ndimage.label(holes)
    for c in range(1, k + 1):
        m = lab == c
        if m.sum() < 2: continue
        print(f"  hole {c}: {m.sum() * step * step:.0f} u^2 at x {GX[m].min():.0f}..{GX[m].max():.0f} y {GY[m].min():.0f}..{GY[m].max():.0f}")
