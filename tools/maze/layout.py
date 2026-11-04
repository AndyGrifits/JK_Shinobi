"""Layout generator of the procedural Orochimaru maze: grows a maze of catalogue blocks on a cell grid.

    python3 tools/maze/layout.py <mz_blocks.json> <out dir> [count=300] [preview count=6]

Writes <out>/NNN.txt (one layout each), <out>/count.txt, and <out>/preview_NNN.png for the first few.
The game (codemp/game/g_shinobi_maze.c) reads one layout at map load and assembles it:

    b <block> <x> <y> <yaw> <depth>          misc_bsp instance (origin = block centre, floor z 0)
    w <id> <x> <y> <z> <link ids...>         nav waypoint, edges listed once (<= 4 per line)
    s <x> <y> <z> <yaw>                      player spawn
    i <classname> <x> <y> <z>                item
    n <role> <x> <y> <z> <yaw> <depth>       NPC slot (role: patrol, guard, ambush, boss)

Growth: start room, then a "growing tree" over the open doorways (mostly the newest one, sometimes a random
one), each step trying block types by weight (with quotas) in every yaw and every doorway of the new block that
can face the chosen one. A block's other doorways that land on a neighbour's doorway connect (loops); the ones
that end at a wall or the grid edge get a cap (a closed door). The goal chamber goes on the open doorway
deepest from the start. Corridors try not to end at a cap (a dead-end niche block goes there when it fits).
"""
import json, math, os, random, sys
from collections import deque

cat = json.load(open(sys.argv[1])); out = sys.argv[2]
COUNT = int(sys.argv[3]) if len(sys.argv) > 3 else 300
NPREV = int(sys.argv[4]) if len(sys.argv) > 4 else 6
CELL = 512; G = 9
WEIGHTS = {"c_i": 24, "c_l": 16, "c_t": 10, "c_x": 4, "r_study": 4, "r_cell": 4, "h_pillars": 4, "h_flood": 3, "h_lab": 3}
QUOTA = {"c_x": 2, "r_study": 2, "r_cell": 2, "h_pillars": 1, "h_flood": 1, "h_lab": 1, "c_end": 3}
CORRIDOR = {"c_i", "c_l", "c_t", "c_x", "c_end"}

def rot(x, y, yaw):
    c, s = round(math.cos(math.radians(yaw))), round(math.sin(math.radians(yaw)))
    return x * c - y * s, x * s + y * c

def dvec(d):
    return round(math.cos(math.radians(d))), round(math.sin(math.radians(d)))

class Layout:
    def __init__(self, rng):
        self.rng = rng
        self.occ = {}                    # cell -> block index
        self.blocks = []                 # dict(name, ox, oy, yaw, socks=[(px, py, dir)])
        self.sock = {}                   # (px, py, dir) -> block index    (doorways of placed blocks)

    def footprint(self, name, ox, oy):
        if cat[name]["size"] == 1:
            return [(ox // CELL, oy // CELL)]
        x0, y0 = (ox - CELL // 2) // CELL, (oy - CELL // 2) // CELL
        return [(x0, y0), (x0 + 1, y0), (x0, y0 + 1), (x0 + 1, y0 + 1)]

    def world_socks(self, name, ox, oy, yaw):
        return [(ox + rot(x, y, yaw)[0], oy + rot(x, y, yaw)[1], (d + yaw) % 360) for x, y, d in cat[name]["sockets"]]

    def cell_beyond(self, s):
        dx, dy = dvec(s[2])
        return ((s[0] + dx * CELL // 2) // CELL, (s[1] + dy * CELL // 2) // CELL)

    def fits(self, name, ox, oy, yaw):
        """None if it does not fit, else (connections, caps) its doorways would make"""
        cells = self.footprint(name, ox, oy)
        if any(not (0 <= cx < G and 0 <= cy < G) or (cx, cy) in self.occ for cx, cy in cells):
            return None
        conn = caps = 0
        for s in self.world_socks(name, ox, oy, yaw):
            cx, cy = self.cell_beyond(s)
            if not (0 <= cx < G and 0 <= cy < G):
                caps += 1
            elif (cx, cy) in self.occ:
                if (s[0], s[1], (s[2] + 180) % 360) in self.sock: conn += 1
                else: caps += 1
        # doorways of neighbours that point into the new block's cells but have no doorway of ours to meet
        mine = {(s[0], s[1]) for s in self.world_socks(name, ox, oy, yaw)}
        for (px, py, d), bi in self.sock.items():
            if self.cell_beyond((px, py, d)) in cells and (px, py) not in mine:
                caps += 1
        return conn, caps

    def place(self, name, ox, oy, yaw):
        bi = len(self.blocks)
        socks = self.world_socks(name, ox, oy, yaw)
        self.blocks.append({"name": name, "ox": ox, "oy": oy, "yaw": yaw, "socks": socks})
        for c in self.footprint(name, ox, oy): self.occ[c] = bi
        for s in socks: self.sock[s] = bi
        return bi

    def open_socks(self):
        """doorways whose cell beyond is free and inside the grid"""
        res = []
        for s, bi in self.sock.items():
            cx, cy = self.cell_beyond(s)
            if 0 <= cx < G and 0 <= cy < G and (cx, cy) not in self.occ:
                res.append(s)
        return res

    def candidates(self, s, names):
        """placements of the given block types with one doorway facing doorway s"""
        want = (s[2] + 180) % 360; res = []
        for name in names:
            for yaw in (0, 90, 180, 270):
                for x, y, d in cat[name]["sockets"]:
                    if (d + yaw) % 360 != want: continue
                    rx, ry = rot(x, y, yaw)
                    ox, oy = s[0] - rx, s[1] - ry
                    r = self.fits(name, ox, oy, yaw)
                    if r is not None: res.append((name, ox, oy, yaw, r))
        return res

    def links(self):
        """block graph: (bi, bj) for every pair of facing doorways"""
        e = set()
        for (px, py, d), bi in self.sock.items():
            bj = self.sock.get((px, py, (d + 180) % 360))
            if bj is not None and bi < bj: e.add((bi, bj, px, py))
        return e

    def depths(self):
        adj = {i: [] for i in range(len(self.blocks))}
        for bi, bj, _, _ in self.links(): adj[bi].append(bj); adj[bj].append(bi)
        dep = {0: 0}; q = deque([0])
        while q:
            u = q.popleft()
            for v in adj[u]:
                if v not in dep: dep[v] = dep[u] + 1; q.append(v)
        return dep

def generate(seed, target):
    rng = random.Random(seed)
    L = Layout(rng)
    # start room near the south-west, facing anywhere
    L.place("s_start", rng.randrange(0, 3) * CELL, rng.randrange(0, 3) * CELL, rng.choice((0, 90, 180, 270)))
    used = {}
    frontier = list(L.open_socks()); rng.shuffle(frontier)
    stall = 0
    while len(L.blocks) < target and stall < 400:
        frontier = [s for s in frontier if s in set(L.open_socks())]
        if not frontier:
            break
        s = frontier[-1] if rng.random() < 0.65 else rng.choice(frontier)
        names = [n for n in WEIGHTS if used.get(n, 0) < QUOTA.get(n, 99)]
        cands = L.candidates(s, names)
        if not cands:
            frontier.remove(s); stall += 1; continue
        # weight: type weight, fewer caps on corridors, a bonus for closing a loop now and then
        def w(c):
            name, _, _, _, (conn, caps) = c
            k = WEIGHTS[name] / max(1, sum(1 for c2 in cands if c2[0] == name))
            if name in CORRIDOR: k *= 0.25 ** caps
            else: k *= 0.7 ** caps
            return k * (1.6 if conn > 1 else 1)
        c = rng.choices(cands, weights=[w(c) for c in cands])[0]
        name, ox, oy, yaw, _ = c
        L.place(name, ox, oy, yaw); used[name] = used.get(name, 0) + 1
        new = [x for x in L.open_socks() if L.sock[x] == len(L.blocks) - 1]
        rng.shuffle(new); frontier += new
    # goal chamber on the deepest open doorway that takes it
    dep = L.depths()
    opens = sorted(L.open_socks(), key=lambda s: -dep.get(L.sock[s], -1))
    goal = None
    for s in opens:
        cands = L.candidates(s, ["s_goal"])
        if cands:
            name, ox, oy, yaw, _ = cands[0]; goal = L.place(name, ox, oy, yaw); break
    if goal is None:
        return None
    dep = L.depths()
    if dep.get(goal, 0) < 6:
        return None
    # corridor doorways still open: a dead-end niche where it fits
    for s in list(L.open_socks()):
        if L.blocks[L.sock[s]]["name"] in CORRIDOR and used.get("c_end", 0) < QUOTA["c_end"]:
            cands = [c for c in L.candidates(s, ["c_end"]) if c[4][1] == 0]
            if cands:
                name, ox, oy, yaw, _ = cands[0]; L.place(name, ox, oy, yaw); used["c_end"] = used.get("c_end", 0) + 1
    return L

def assemble(L):
    """caps, nav graph, slots, spawns, items in world coordinates"""
    dep = L.depths()
    links = L.links()
    connected = {(px, py) for _, _, px, py in links}
    caps = []
    for (px, py, d), bi in L.sock.items():
        if (px, py) not in connected:
            caps.append((px, py, d))
    nodes = {}; node_id = {}; edges = set()
    def nid(key, pos):
        if key not in node_id:
            node_id[key] = len(nodes); nodes[node_id[key]] = pos
        return node_id[key]
    for bi, b in enumerate(L.blocks):
        c = cat[b["name"]]
        if bi not in dep: continue
        local = {}
        for n, (x, y, z) in c["nodes"].items():
            rx, ry = rot(x, y, b["yaw"]); wx, wy = b["ox"] + rx, b["oy"] + ry
            if n.startswith("s"):                                 # doorway node: shared, only if connected
                if (wx, wy) not in connected: continue
                local[n] = nid(("s", wx, wy), (wx, wy, z))
            else:
                local[n] = nid((bi, n), (wx, wy, z))
        for a, bnode in c["edges"]:
            if a in local and bnode in local:
                edges.add(tuple(sorted((local[a], local[bnode]))))
    # drop nodes not connected to the start (unreachable pockets)
    adj = {i: set() for i in nodes}
    for a, bb in edges: adj[a].add(bb); adj[bb].add(a)
    start_nodes = [node_id[k] for k in node_id if k[0] == 0]
    seen = set(start_nodes); q = deque(start_nodes)
    while q:
        u = q.popleft()
        for v in adj[u]:
            if v not in seen: seen.add(v); q.append(v)
    lost = len(nodes) - len(seen)
    # spread every edge onto one of its ends, <= 4 listed per waypoint
    listed = {i: [] for i in nodes}
    for a, bb in sorted(edges):
        (listed[a] if len(listed[a]) <= len(listed[bb]) else listed[bb]).append(bb if len(listed[a]) <= len(listed[bb]) else a)
    over = [i for i in listed if len(listed[i]) > 4]
    slots, spawns, items = [], [], []
    for bi, b in enumerate(L.blocks):
        if bi not in dep: continue
        c = cat[b["name"]]; yaw = b["yaw"]
        for x, y, z, a, role in c["slots"]:
            rx, ry = rot(x, y, yaw); slots.append((role, b["ox"] + rx, b["oy"] + ry, z, (a + yaw) % 360, dep[bi]))
        for x, y, z, a in c["spawns"]:
            rx, ry = rot(x, y, yaw); spawns.append((b["ox"] + rx, b["oy"] + ry, z, (a + yaw) % 360))
        for cls, x, y, z in c["items"]:
            rx, ry = rot(x, y, yaw); items.append((cls, b["ox"] + rx, b["oy"] + ry, z))
    return dict(dep=dep, links=links, caps=caps, nodes=nodes, edges=edges, listed=listed, lost=lost, over=over,
                slots=slots, spawns=spawns, items=items)

def write(L, A, path, idx, seed):
    lines = [f"// layout {idx} seed {seed}: {len(L.blocks)} blocks, {len(A['caps'])} caps, {len(A['nodes'])} waypoints, "
             f"{len(A['links'])} doorways, depth {max(A['dep'].values())}"]
    for bi, b in enumerate(L.blocks):
        lines.append(f"b {b['name']} {b['ox']} {b['oy']} {b['yaw']} {A['dep'].get(bi, -1)}")
    for px, py, d in A["caps"]:
        lines.append(f"b cap {px} {py} {d} -1")
    for i, (x, y, z) in A["nodes"].items():
        lines.append(f"w {i} {x} {y} {z} " + " ".join(map(str, A["listed"][i])))
    for x, y, z, a in A["spawns"]: lines.append(f"s {x} {y} {z} {a}")
    for cls, x, y, z in A["items"]: lines.append(f"i {cls} {x} {y} {z}")
    for role, x, y, z, a, d in A["slots"]: lines.append(f"n {role} {x} {y} {z} {a} {d}")
    open(path, "w").write("\n".join(lines) + "\n")

def preview(L, A, path):
    from PIL import Image, ImageDraw
    S = 0.16; M = 40; W = int(G * CELL * S) + 2 * M
    im = Image.new("RGB", (W, W), (24, 20, 28)); d = ImageDraw.Draw(im)
    P = lambda x, y: (M + (x + CELL / 2) * S, W - M - (y + CELL / 2) * S)
    COL = {"c": (110, 100, 120), "r": (140, 110, 80), "h": (90, 120, 150), "s": (170, 80, 170)}
    for bi, b in enumerate(L.blocks):
        cells = L.footprint(b["name"], b["ox"], b["oy"])
        x0 = min(c[0] for c in cells) * CELL - CELL / 2; y0 = min(c[1] for c in cells) * CELL - CELL / 2
        x1 = max(c[0] for c in cells) * CELL + CELL / 2; y1 = max(c[1] for c in cells) * CELL + CELL / 2
        a, bb = P(x0, y0), P(x1, y1)
        d.rectangle([a[0] + 2, bb[1] + 2, bb[0] - 2, a[1] - 2], fill=COL[b["name"][0]] if b["name"] != "s_start" else (60, 160, 80))
        cx, cy = P(b["ox"], b["oy"])
        d.text((cx - 18, cy - 6), f"{b['name']}:{A['dep'].get(bi, '?')}", fill=(255, 255, 255))
    for px, py, dd in A["caps"]:
        cx, cy = P(px, py); d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=(220, 60, 50))
    for a, bb in A["edges"]:
        p, q = P(*A["nodes"][a][:2]), P(*A["nodes"][bb][:2]); d.line([p, q], fill=(240, 220, 120), width=1)
    RC = {"patrol": (80, 200, 255), "guard": (255, 160, 60), "ambush": (255, 60, 200), "boss": (255, 0, 0)}
    for role, x, y, z, a, dd in A["slots"]:
        cx, cy = P(x, y); d.rectangle([cx - 3, cy - 3, cx + 3, cy + 3], fill=RC[role])
    im.save(path)

os.makedirs(out, exist_ok=True)
made = 0; seed = 0; stats = []
while made < COUNT:
    seed += 1
    rng = random.Random(seed * 7919)
    L = generate(seed, rng.randrange(22, 32))
    if L is None: continue
    A = assemble(L)
    if A["lost"] or A["over"]: continue
    write(L, A, f"{out}/{made:03d}.txt", made, seed)
    if made < NPREV: preview(L, A, f"{out}/preview_{made:03d}.png")
    stats.append((len(L.blocks), len(A["caps"]), len(A["nodes"]), max(A["dep"].values()), len(A["links"]) - len(L.blocks) + 1))
    made += 1
open(f"{out}/count.txt", "w").write(f"{COUNT}\n")
avg = [sum(s[i] for s in stats) / len(stats) for i in range(5)]
print(f"LAYOUTS OK {COUNT} (tried {seed} seeds): avg {avg[0]:.1f} blocks, {avg[1]:.1f} caps, {avg[2]:.0f} waypoints, "
      f"depth {avg[3]:.1f}, loops {avg[4]:.1f}; max waypoints {max(s[2] for s in stats)}")
