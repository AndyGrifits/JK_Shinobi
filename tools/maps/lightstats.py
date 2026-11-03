#!/usr/bin/env python3
"""Lightmap brightness per shader of a compiled RBSP map (sampled at each surface's vertices).
    python3 tools/maps/lightstats.py <map.bsp>      (0..255; 255 = texture at full brightness in JA)"""
import struct, sys
from collections import defaultdict
import numpy as np
d = open(sys.argv[1], "rb").read()
L = [struct.unpack_from("<ii", d, 8 + i * 8) for i in range(18)]
sh = [struct.unpack_from("<64s", d, L[1][0] + i)[0].split(b"\0")[0].decode().split("/")[-1] for i in range(0, L[1][1], 72)]
o, l = L[14]; lm = np.frombuffer(d[o:o + l], np.uint8).reshape(-1, 128, 128, 3).mean(axis=3)
vo = L[10][0]
acc = defaultdict(list)
so, sl = L[13]
for i in range(so, so + sl, 148):
    s, fog, typ, fv, nv = struct.unpack_from("<5i", d, i)
    lmn = struct.unpack_from("<i", d, i + 36)[0]
    if lmn < 0:
        continue
    for v in range(fv, fv + nv):
        u, w = struct.unpack_from("<2f", d, vo + v * 80 + 20)
        acc[sh[s]].append(lm[lmn, min(127, int(w * 128)), min(127, int(u * 128))])
for k, v in sorted(acc.items(), key=lambda kv: -len(kv[1])):
    v = np.array(v)
    print(f"{k:14s} verts {len(v):6d}  mean {v.mean():5.0f}  p10 {np.percentile(v, 10):5.0f}  p50 {np.percentile(v, 50):5.0f}  p90 {np.percentile(v, 90):5.0f}")
