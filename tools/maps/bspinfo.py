#!/usr/bin/env python3
"""Inspect a compiled JKA (RBSP) map: surfaces per shader + lightmapped or not, lightmap count,
and which cluster each light / spawn entity ended up in (cluster -1 = inside solid).

    python3 tools/maps/bspinfo.py <map.bsp> [<map.map>]
"""
import re, struct, sys
from collections import Counter

data = open(sys.argv[1], "rb").read()
ident, ver = struct.unpack_from("<4si", data, 0)
assert ident == b"RBSP", ident
lumps = [struct.unpack_from("<ii", data, 8 + i * 8) for i in range(18)]
def lump(i): o, l = lumps[i]; return data[o:o + l]

shaders = [(s[:64].split(b"\0")[0].decode(), sf, cf) for s, sf, cf in
           (struct.unpack_from("<64sii", lump(1), i) for i in range(0, len(lump(1)), 72))]
planes = [struct.unpack_from("<4f", lump(2), i) for i in range(0, len(lump(2)), 16)]
nodes = [struct.unpack_from("<3i", lump(3), i) for i in range(0, len(lump(3)), 36)]
leafs = [struct.unpack_from("<2i", lump(4), i) for i in range(0, len(lump(4)), 48)]
SURF = 148
types = Counter(); lm = Counter(); per = Counter()
for i in range(0, len(lump(13)), SURF):
    sh, fog, typ = struct.unpack_from("<3i", lump(13), i)
    lmnum = struct.unpack_from("<4i", lump(13), i + 36)[0]
    name = shaders[sh][0]
    types[typ] += 1
    lit = "lightmap" if lmnum >= 0 else ("vertex" if lmnum == -3 else f"lm{lmnum}")
    per[(name, lit)] += 1
print(f"surfaces {sum(types.values())} by type {dict(types)} (1 planar, 2 patch, 3 triangles)")
print(f"lightmaps lump {len(lump(14))} bytes = {len(lump(14)) // (128 * 128 * 3)} x 128^2")
for (name, lit), n in sorted(per.items()):
    print(f"  {n:4d}  {lit:8s} {name}")

def leaf_for(p):
    n = 0
    while n >= 0:
        pl, a, b = nodes[n]
        nx, ny, nz, d = planes[pl]
        n = a if nx * p[0] + ny * p[1] + nz * p[2] - d >= 0 else b
    return -1 - n
ents = lump(0).decode(errors="replace")
for cls, org in re.findall(r'"classname" "(\w+)"\s*\n\s*"origin" "([^"]+)"', ents) + \
        re.findall(r'"origin" "([^"]+)"\s*\n\s*"classname" "(\w+)"', ents)[::1] and []:
    pass
src = open(sys.argv[2]).read() if len(sys.argv) > 2 else ents
for blk in re.findall(r"\{([^{}]*)\}", src):
    kv = dict(re.findall(r'"([^"]+)" "([^"]*)"', blk))
    if "origin" in kv and (kv.get("classname", "").startswith(("light", "info_player"))):
        p = list(map(float, kv["origin"].split()))
        cl = leafs[leaf_for(p)][0]
        flag = "" if cl >= 0 else "   <-- in solid"
        print(f"  {kv['classname']:24s} {kv['origin']:>16s}  cluster {cl}{flag}")
