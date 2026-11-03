#!/usr/bin/env python3
"""Where the map.json "items" end up: replays FinishSpawningItem (box -8 -8 0 .. 8 8 16, dropped
straight down onto MASK_SOLID - playerclip does not stop items) against the compiled BSP.

    python3 tools/maps/itemcheck.py maps/<name>/map.json <map.bsp>
Prints the landing height and what it lands on; STARTSOLID items are removed by the game.
"""
import json, struct, sys
import numpy as np

cfg = json.load(open(sys.argv[1])); d = open(sys.argv[2], "rb").read()
L = [struct.unpack_from("<ii", d, 8 + i * 8) for i in range(18)]
sh = [struct.unpack_from("<64sii", d, L[1][0] + i) for i in range(0, L[1][1], 72)]
planes = np.frombuffer(d[L[2][0]:L[2][0] + L[2][1]], "<f4").reshape(-1, 4)
br = np.frombuffer(d[L[8][0]:L[8][0] + L[8][1]], "<i4").reshape(-1, 3)
sides = np.frombuffer(d[L[9][0]:L[9][0] + L[9][1]], "<i4").reshape(-1, 3)
SOLID = 1
solid = [(planes[sides[f:f + n, 0]], sh[s][0].split(b"\0")[0].decode().split("/")[-1]) for f, n, s in br if sh[s][2] & SOLID]

def column_top(x, y, z):
    """highest solid surface at or below z in the vertical line (x, y); returns (z, shader)"""
    best = (-1e9, None)
    for pl, name in solid:
        lo, hi, ok = -1e9, 1e9, True
        for nx, ny, nz, dd in pl:
            r = dd - nx * x - ny * y
            if abs(nz) < 1e-6:
                if r < 0: ok = False; break
            elif nz > 0: hi = min(hi, r / nz)
            else: lo = max(lo, r / nz)
        if ok and hi > lo and lo < z:
            top = min(hi, z)
            if top > best[0]: best = (top, name)
    return best

bad = 0
for it in cfg.get("items", []):
    if "class" not in it: continue
    x, y, z = it["at"]
    # start solid: any of the box's sample points inside a solid brush
    samples = [(x + dx, y + dy, z + dz) for dx in (-8, 0, 8) for dy in (-8, 0, 8) for dz in (0.1, 8, 15.9)]
    start = any(np.all(pl[:, :3] @ np.array(p) - pl[:, 3] < 0) for p in samples for pl, _ in solid)
    hits = [column_top(x + dx, y + dy, z) for dx in (-8, 0, 8) for dy in (-8, 0, 8)]
    land = max(hits)
    flag = "STARTSOLID (removed)" if start else ""
    bad += bool(start) or land[0] < -1e8
    print(f"{it['class']:24s} at ({x:5.0f},{y:5.0f},{z:4.0f}) -> lands z {land[0]:6.1f} on {land[1]}  {flag}  {it.get('_', '')}")
print("PROBLEMS" if bad else "ITEMS OK")
