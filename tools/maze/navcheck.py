"""Offline nav check of the maze blocks: every catalogue waypoint and nav edge must be clear of solid brushes for a
player box (30 x 30, from 2 above the floor to 56 up).

    python3 tools/maze/navcheck.py <base dir>          (after blocks.py: reads maps/mz/*.map and mz_blocks.json)
"""
import json, re, sys
import numpy as np

base = sys.argv[1]
cat = json.load(open(f"{base}/mz_blocks.json"))
NONSOLID = ("mz_water", "mz_flame")

def brushes(path):
    txt = open(path).read()
    k = txt.find('\n{\n"classname"', 1)
    ws = txt if k < 0 else txt[:k]                             # worldspawn only
    out = []
    for b in re.findall(r"\{\n(\(.*?)\n\}", ws, re.S):
        planes = []; solid = True
        for line in b.split("\n"):
            m = re.match(r"\(\s*(\S+) (\S+) (\S+)\s*\)\s*\(\s*(\S+) (\S+) (\S+)\s*\)\s*\(\s*(\S+) (\S+) (\S+)\s*\)\s*(\S+)", line)
            if not m: continue
            p = np.array(list(map(float, m.groups()[:9]))).reshape(3, 3)
            if any(s in m.group(10) for s in NONSOLID): solid = False
            n = np.cross(p[2] - p[0], p[1] - p[0]); n /= np.linalg.norm(n)
            planes.append((n, n @ p[0]))
        if solid and planes: out.append(planes)
    return out

def blocked(bs, pts):
    """any of the points inside any brush (strictly, 0.5 u margin)"""
    for planes in bs:
        N = np.array([n for n, _ in planes]); D = np.array([d for _, d in planes])
        inside = np.all(pts @ N.T - D < -0.5, axis=1)
        if inside.any(): return pts[inside][0]
    return None

box = np.array([[x, y, z] for x in np.linspace(-15, 15, 5) for y in np.linspace(-15, 15, 5) for z in range(2, 57, 6)], float)
ebox = box[box[:, 2] >= 18]          # the navigator's edge test box starts a step (18) above the feet
bad = 0
for name, c in cat.items():
    bs = brushes(f"{base}/maps/mz/{name}.map")
    floor = lambda p: np.array([p[0], p[1], p[2] - 24])            # waypoint z 24 = player origin; feet at -24
    for nid, p in c["nodes"].items():
        hit = blocked(bs, floor(p) + box)
        if hit is not None: print(f"{name}: node {nid} {p} in solid at {hit.round()}"); bad += 1
    for a, b in c["edges"]:
        pa, pb = floor(c["nodes"][a]), floor(c["nodes"][b])
        n = max(2, int(np.linalg.norm(pb - pa) / 8))
        for t in np.linspace(0, 1, n):
            hit = blocked(bs, pa + (pb - pa) * t + ebox)
            if hit is not None:
                print(f"{name}: edge {a}-{b} blocked at {hit.round()}"); bad += 1; break
    for cls, x, y, z in c["items"]:
        hit = blocked(bs, np.array([[x, y, z - 10]]))
        if hit is not None: print(f"{name}: item {cls} {[x, y, z]} in solid"); bad += 1
    for x, y, z, yaw, role in c["slots"]:
        if role == "ambush": continue
        hit = blocked(bs, np.array([x, y, z - 24]) + box)
        if hit is not None: print(f"{name}: slot {role} {[x, y, z]} in solid at {hit.round()}"); bad += 1
print(f"NAVCHECK {'OK' if not bad else 'FAIL'}: {bad} problems")
if (bad and not __import__("os").environ.get("NAVCHECK_FORCE")) or len(sys.argv) <= 2:
    sys.exit(1 if bad else 0)

# --- optional: check whole layouts (brushes moved / turned as the game places them) ---------------------------
#   python3 tools/maze/navcheck.py <base dir> <layout.txt>...
if len(sys.argv) > 2:
    import math
    cache = {}
    worst = 0
    for lp in sys.argv[2:]:
        world = []; wp = {}; edges = []
        for line in open(lp):
            t = line.split()
            if not t: continue
            if t[0] == "b":
                name, ox, oy, yaw = t[1], float(t[2]), float(t[3]), float(t[4])
                if name not in cache: cache[name] = brushes(f"{base}/maps/mz/{name}.map")
                c, s_ = round(math.cos(math.radians(yaw))), round(math.sin(math.radians(yaw)))
                Rm = np.array([[c, -s_, 0], [s_, c, 0], [0, 0, 1]], float); o = np.array([ox, oy, 0])
                for planes in cache[name]:
                    world.append([(Rm @ n, d + (Rm @ n) @ o) for n, d in planes])
            elif t[0] == "w":
                i = int(t[1]); wp[i] = np.array(list(map(float, t[2:5])))
                edges += [(i, int(k)) for k in t[5:]]
        # only brushes near the point matter: bucket by bounds
        bad = 0
        floor = lambda p: p - np.array([0, 0, 24])
        for i, p in wp.items():
            hit = blocked(world, floor(p) + box)
            if hit is not None: print(f"{lp}: waypoint {i} {p.round()} in solid at {hit.round()}"); bad += 1
        for a, b in edges:
            pa, pb = floor(wp[a]), floor(wp[b])
            for tt in np.linspace(0, 1, max(2, int(np.linalg.norm(pb - pa) / 8))):
                hit = blocked(world, pa + (pb - pa) * tt + ebox)
                if hit is not None: print(f"{lp}: edge {a}-{b} blocked at {hit.round()}"); bad += 1; break
        print(f"LAYOUT {lp.split('/')[-1]}: {len(wp)} waypoints, {len(edges)} edges, {bad} problems")
        worst = max(worst, bad)
    sys.exit(1 if worst else 0)
