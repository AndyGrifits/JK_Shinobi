"""Block library of the procedural Orochimaru maze: every block is its own small BSP (runtime misc_bsp).

    python3 tools/maze/blocks.py <base dir>   -> <base>/maps/mz/<block>.map, <base>/shaders/shinobi_maze.shader,
                                                <base>/textures/shinobi_maze/*, tools-side catalog <base>/mz_blocks.json

Grid: cell 512, a block is 1x1 or 2x2 cells, built around its centre with the floor at z 0. Doorways ("sockets")
sit in the middle of each 512 edge: 160 wide, 224 high, and the doorway line is the cell edge. A block only
builds its own shell; neighbours meet at the doorway line. An unused socket gets a "cap" block (a closed door).

Lightmapped shaders are named per block (<block>_wall, ...): the renderer caches shaders by name + lightmap
number and every sub-BSP numbers its lightmaps from 0 (see tools/maze/README.md). Unlit ones (flames, water,
tank liquid) are shared.

The catalog lists, in block coordinates: sockets [x, y, dir] (dir = outward yaw), nav nodes {id: [x, y, z]}
(socket nodes are "s<i>"), nav edges, NPC slots [x, y, z, yaw, role], player spawns, items.
"""
import json, math, os, shutil, sys

base = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
SD = "shinobi_maze"
FILL = float(os.environ.get("MZ_FILL", "1"))
CELL = 512; H = 256; DW = 80; DH = 224; T = 16
LIT = ["wall", "floor", "rock", "trim", "wood", "rope", "metal"]
TEX = {"wall": ("mz_wall", 0.5), "floor": ("mz_floor", 0.5), "rock": ("mz_rock", 0.5), "trim": ("mz_trim", 0.25),
       "wood": ("mz_wood", 0.25), "rope": ("mz_rope", 0.125), "metal": ("mz_metal", 0.25)}
MAT = {"wall": "rock", "floor": "rock", "rock": "rock", "trim": "rock", "wood": "solidwood", "rope": "fabric",
       "metal": "solidmetal"}

def f(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")

class Block:
    def __init__(self, name, size, sockets, minlight=38):
        self.name, self.size = name, size
        self.sockets = sockets                      # [x, y, dir]
        self.brushes, self.ents = [], []
        self.nodes, self.edges, self.slots, self.spawns, self.items = {}, [], [], [], []
        self.minlight = minlight
        self.used = set()

    def sh(self, mat):
        if mat in LIT:
            self.used.add(mat)
            return f"{SD}/{self.name}_{mat}"
        return f"{SD}/{mat}"

    def box(self, x0, y0, z0, x1, y1, z1, mat):
        if x1 - x0 < 0.5 or y1 - y0 < 0.5 or z1 - z0 < 0.5:
            return
        s = self.sh(mat); sc = TEX.get(mat, (None, 0.5))[1]
        t = f"0 0 0 {sc} {sc} 0 0 0"
        x0, y0, z0, x1, y1, z1 = map(f, (x0, y0, z0, x1, y1, z1))
        self.brushes.append("\n".join(["{",
            f"( {x0} 0 0 ) ( {x0} 1 0 ) ( {x0} 0 1 ) {s} {t}", f"( {x1} 0 0 ) ( {x1} 0 1 ) ( {x1} 1 0 ) {s} {t}",
            f"( 0 {y0} 0 ) ( 0 {y0} 1 ) ( 1 {y0} 0 ) {s} {t}", f"( 0 {y1} 0 ) ( 1 {y1} 0 ) ( 0 {y1} 1 ) {s} {t}",
            f"( 0 0 {z0} ) ( 1 0 {z0} ) ( 0 1 {z0} ) {s} {t}", f"( 0 0 {z1} ) ( 0 1 {z1} ) ( 1 0 {z1} ) {s} {t}", "}"]))

    def prism(self, cx, cy, r, n, z0, z1, mat, rot=0.0):
        """vertical prism, n sides around (cx, cy)"""
        s = self.sh(mat); sc = TEX.get(mat, (None, 0.5))[1]; t = f"0 0 0 {sc} {sc} 0 0 0"
        pts = [(cx + r * math.cos(rot + 2 * math.pi * i / n), cy + r * math.sin(rot + 2 * math.pi * i / n)) for i in range(n)]
        faces = [f"( 0 0 {f(z0)} ) ( 1 0 {f(z0)} ) ( 0 1 {f(z0)} ) {s} {t}", f"( 0 0 {f(z1)} ) ( 0 1 {f(z1)} ) ( 1 0 {f(z1)} ) {s} {t}"]
        for (ax, ay), (bx, by) in zip(pts, pts[1:] + pts[:1]):
            faces.append(f"( {f(ax)} {f(ay)} {f(z0)} ) ( {f(ax)} {f(ay)} {f(z1)} ) ( {f(bx)} {f(by)} {f(z0)} ) {s} {t}")
        self.brushes.append("{\n" + "\n".join(faces) + "\n}")

    def light(self, x, y, z, val, color="1 0.68 0.42"):
        self.ents.append({"classname": "light", "origin": f"{f(x)} {f(y)} {f(z)}", "light": str(val), "_color": color})

    def fill(self, x, y, z, val, color="0.85 0.75 0.9"):
        """broad fill light (linear falloff), keeps the corners out of the dark"""
        self.ents.append({"classname": "light", "origin": f"{f(x)} {f(y)} {f(z)}", "light": str(int(val * FILL)),
                          "_color": color, "spawnflags": "1"})

    def torch(self, x, y, z, nx, ny, val=330, k=3.0):
        """wall torch: bracket, a looping flame effect, a light in front. (nx, ny) = wall normal (into the room)"""
        px, py = -ny, nx
        bx, by = x + nx * 6, y + ny * 6
        self.box(min(x, bx + px * 6) - 3 * abs(px), min(y, by + py * 6) - 3 * abs(py), z - 10,
                 max(x, bx - px * 6) + 3 * abs(px) + 6 * abs(nx), max(y, by - py * 6) + 3 * abs(py) + 6 * abs(ny), z, "metal")
        cx, cy = x + nx * 12, y + ny * 12                   # flame: a looping effect (effects/shinobi/torch.efx)
        self.ents.append({"classname": "fx_runner", "origin": f"{f(cx)} {f(cy)} {f(z + 2)}", "fxFile": "shinobi/torch",
                          "delay": "90"})
        self.light(x + nx * 40, y + ny * 40, z + 18, int(val * k))

    def node(self, nid, x, y, z=24):
        self.nodes[nid] = [x, y, z]

    def edge(self, a, b):
        self.edges.append([a, b])

    def write(self):
        ws = "{\n" + f'"classname" "worldspawn"\n"_minlight" "{self.minlight}"\n"_blocksize" "0"\n' + \
             "\n".join(self.brushes) + "\n}\n"
        es = "".join("{\n" + "".join(f'"{k}" "{v}"\n' for k, v in e.items()) + "}\n" for e in self.ents)
        open(f"{base}/maps/mz/{self.name}.map", "w").write(ws + es)
        return {"size": self.size, "sockets": self.sockets, "nodes": self.nodes, "edges": self.edges,
                "slots": self.slots, "spawns": self.spawns, "items": self.items, "shaders": sorted(self.used)}

# --- corridors -----------------------------------------------------------------------------------------------------
ARMS = {"E": (1, 0, 0), "N": (0, 1, 90), "W": (-1, 0, 180), "S": (0, -1, 270)}

def corridor(name, arms, torch_side=None):
    b = Block(name, 1, [[ARMS[a][0] * H, ARMS[a][1] * H, ARMS[a][2]] for a in arms])
    W = DW + T
    b.box(-W, -W, -T, W, W, 0, "floor"); b.box(-W, -W, DH, W, W, DH + T, "rock")
    for a, (dx, dy, _) in ARMS.items():
        if a in arms:                                           # arm: floor, ceiling, two side walls
            if dx:
                x0, x1 = sorted((dx * W, dx * H))
                b.box(x0, -W, -T, x1, W, 0, "floor"); b.box(x0, -W, DH, x1, W, DH + T, "rock")
                b.box(min(x0, dx * DW), -W, 0, max(x1, dx * DW), -DW, DH, "wall")
                b.box(min(x0, dx * DW), DW, 0, max(x1, dx * DW), W, DH, "wall")
                for px in (dx * 150,):                          # pilasters + a ceiling beam
                    b.box(px - 14, -DW, 0, px + 14, -DW + 10, DH, "trim"); b.box(px - 14, DW - 10, 0, px + 14, DW, DH, "trim")
                    b.box(px - 14, -DW, DH - 18, px + 14, DW, DH, "wood")
            else:
                y0, y1 = sorted((dy * W, dy * H))
                b.box(-W, y0, -T, W, y1, 0, "floor"); b.box(-W, y0, DH, W, y1, DH + T, "rock")
                b.box(-W, min(y0, dy * DW), 0, -DW, max(y1, dy * DW), DH, "wall")
                b.box(DW, min(y0, dy * DW), 0, W, max(y1, dy * DW), DH, "wall")
                for py in (dy * 150,):
                    b.box(-DW, py - 14, 0, -DW + 10, py + 14, DH, "trim"); b.box(DW - 10, py - 14, 0, DW, py + 14, DH, "trim")
                    b.box(-DW, py - 14, DH - 18, DW, py + 14, DH, "wood")
        else:                                                   # closed side of the centre square
            if dx:
                b.box(*sorted((dx * DW, dx * W))[:1], -W, 0, *sorted((dx * DW, dx * W))[1:], W, DH, "wall")
            else:
                y0, y1 = sorted((dy * DW, dy * W)); b.box(-W, y0, 0, W, y1, DH, "wall")
    # torch on a closed side of the centre if there is one, else on an arm wall
    closed = [a for a in ARMS if a not in arms]
    if closed:
        dx, dy, _ = ARMS[torch_side or closed[0]]
        b.torch(dx * DW, dy * DW, 150, -dx, -dy)
    else:
        b.torch(150, DW, 150, 0, -1, 300)
    b.fill(0, 0, 190, 260)
    for a in arms:
        b.fill(ARMS[a][0] * 190, ARMS[a][1] * 190, 190, 200)
    b.node("c", 0, 0)
    for i, a in enumerate(arms):
        dx, dy, _ = ARMS[a]
        b.node(f"s{i}", dx * H, dy * H); b.edge("c", f"s{i}")
    b.slots.append([0, 0, 24, 0, "patrol"])
    return b

def dead_end(name):
    """corridor ending in a small shrine niche with candles (west), open east"""
    b = corridor(name, "E", torch_side="N")
    b.box(-DW - T - 40, -48, -T, -DW, 48, 0, "floor")
    b.box(-DW - 48, -40, 0, -DW - 8, 40, 46, "trim")              # altar in the niche of the west wall
    b.box(-DW - 40, -26, 46, -DW - 30, 26, 52, "wood")
    b.slots[-1] = [-20, 0, 24, 0, "guard"]
    b.items.append(["item_medpac", -50, 0, 70])
    return b

# --- rooms (1x1, 320 high, walls 32 thick) -----------------------------------------------------------------------
def shell(b, half, height, sockets, wall="wall", floor="floor", ceil="rock", th=32):
    """box room -half..half inside, walls `th` thick; doorways at the given sockets (any edge of the footprint)"""
    o = half + th
    b.box(-o, -o, -T, o, o, 0, floor); b.box(-o, -o, height, o, o, height + T, ceil)
    for side in range(4):
        ang = side * 90
        ops = sorted([(-sy if side == 0 else sx if side == 1 else sy if side == 2 else -sx)
                      for sx, sy, d in sockets if d == ang])         # door offsets along the wall (left to right)
        # wall pieces between doorways, along t in [-o, o]
        cuts = [-o] + sum([[c - DW, c + DW] for c in ops], []) + [o]
        for k in range(0, len(cuts), 2):
            t0, t1 = cuts[k], cuts[k + 1]
            seg = [(t0, 0, t1, height)]
            for (a0, z0, a1, z1) in seg:
                _wall(b, side, half, o, a0, a1, z0, z1, wall)
        for c in ops:                                            # lintel + door frame
            _wall(b, side, half, o, c - DW, c + DW, DH, height, wall)
            _wall(b, side, half - 10, half, c - DW - 16, c - DW, 0, DH + 16, "trim")
            _wall(b, side, half - 10, half, c + DW, c + DW + 16, 0, DH + 16, "trim")
            _wall(b, side, half - 10, half, c - DW - 16, c + DW + 16, DH, DH + 16, "trim")

def _wall(b, side, i0, i1, t0, t1, z0, z1, mat):
    """a wall slab on side 0=E 1=N 2=W 3=S between depth i0..i1 (distance from centre) and t0..t1 along it
    (t runs counter-clockwise as seen from inside: E wall +y.. no - left to right facing the wall)"""
    if side == 0: b.box(i0, -t1, z0, i1, -t0, z1, mat)
    elif side == 1: b.box(t0, i0, z0, t1, i1, z1, mat)
    elif side == 2: b.box(-i1, t0, z0, -i0, t1, z1, mat)
    else: b.box(-t1, -i1, z0, -t0, -i0, z1, mat)

def room_fill(b, val=420):
    b.fill(0, 0, 270, val); b.fill(-130, -130, 200, val * 0.6); b.fill(130, 130, 200, val * 0.6)

def room_nodes(b, half, inner=170):
    b.node("c", 0, 0)
    for i, (sx, sy, d) in enumerate(b.sockets):
        nx, ny = round(math.cos(math.radians(d))), round(math.sin(math.radians(d)))
        b.node(f"s{i}", sx, sy); b.node(f"d{i}", sx - nx * (H - inner) if b.size == 1 else sx - nx * 110,
                                        sy - ny * (H - inner) if b.size == 1 else sy - ny * 110)
        b.edge(f"s{i}", f"d{i}")

SOCK4 = [[H, 0, 0], [0, H, 90], [-H, 0, 180], [0, -H, 270]]

def room_study(name):
    """Orochimaru's study: shelves, desk with scrolls, two doors"""
    b = Block(name, 1, [[-H, 0, 180], [0, -H, 270]])
    shell(b, 224, 320, b.sockets)
    room_fill(b)
    b.box(60, 150, 0, 220, 222, 190, "wood"); b.box(150, -220, 0, 222, -60, 190, "wood")          # shelves
    for z in (50, 100, 150): b.box(62, 148, z, 218, 152, z + 4, "metal")
    b.box(40, -10, 0, 150, 70, 44, "wood"); b.box(60, 10, 44, 110, 40, 50, "rope")              # desk + scroll
    b.torch(-224, 140, 160, 1, 0); b.torch(140, 224, 160, 0, -1, 300)
    room_nodes(b, 224)
    for i in range(len(b.sockets)): b.edge(f"d{i}", "c")
    b.slots += [[-60, 60, 24, 315, "guard"]]
    b.items.append(["item_medpac", 100, 30, 76])
    return b

def room_cell(name):
    """prison cell block: iron bars split the room, a gap in the bars, a stone bed inside; one door (west)"""
    b = Block(name, 1, [[-H, 0, 180]])
    shell(b, 224, 320, b.sockets)
    room_fill(b)
    for y in range(-210, 211, 28):                              # bars along x = 60, gap at y -40..40
        if abs(y) > 50: b.box(56, y - 4, 0, 64, y + 4, 320, "metal")
    b.box(56, -224, 300, 64, 224, 320, "metal")
    b.box(56, -224, 0, 64, -66, 8, "metal"); b.box(56, 66, 0, 64, 224, 8, "metal")      # sill, open in the gap
    b.box(150, -200, 0, 222, -60, 36, "trim"); b.box(150, 60, 0, 222, 200, 36, "trim")          # stone beds
    b.torch(-120, 224, 160, 0, -1, 300); b.torch(-120, -224, 160, 0, 1, 260)
    room_nodes(b, 224)
    b.edge("d0", "c"); b.node("in", 140, 0); b.edge("c", "in")
    b.slots += [[110, -20, 24, 180, "guard"], [-80, -140, 260, 90, "ambush"]]
    return b

def room_start(name):
    """the squad's way in: a collapsed entrance (rubble) on the west wall, four spawns, four possible doors"""
    b = Block(name, 1, SOCK4)
    shell(b, 224, 320, b.sockets)
    room_fill(b)
    for i, (x, y, w, h) in enumerate([(-190, -120, 60, 50), (-200, 60, 44, 70), (-170, 140, 70, 40)]):
        b.prism(x, y, w * 0.6, 6, 0, h, "rock", rot=i)
    b.torch(-224, -150, 160, 1, 0); b.torch(150, 224, 160, 0, -1, 300)
    b.node("c", 0, 0)
    for i, (sx, sy, d) in enumerate(b.sockets):
        b.node(f"s{i}", sx, sy); b.edge(f"s{i}", "c")
    b.spawns += [[-60, -60, 40, 0], [-60, 60, 40, 0], [60, -60, 40, 0], [60, 60, 40, 0]]
    b.items.append(["item_medpac", 0, -150, 30])
    return b

# --- halls (2x2, 960 inside, walls 32 thick) ---------------------------------------------------------------------
SOCK8 = [[2 * H, -H, 0], [2 * H, H, 0], [H, 2 * H, 90], [-H, 2 * H, 90],
         [-2 * H, H, 180], [-2 * H, -H, 180], [-H, -2 * H, 270], [H, -2 * H, 270]]

def hall_fill(b, val=520, z=380, color="0.85 0.75 0.9"):
    for x in (-280, 280):
        for y in (-280, 280): b.fill(x, y, z, val, color)
    b.fill(0, 0, z, val, color)

def hall_nodes(b, skip=(), xs=(-320, 0, 320), ys=(-320, 0, 320)):
    g = {}
    for i, x in enumerate(xs):
        for j, y in enumerate(ys):
            if (i, j) in skip: continue
            g[(i, j)] = f"g{i}{j}"; b.node(f"g{i}{j}", x, y)
    for (i, j), n in g.items():
        if (i + 1, j) in g: b.edge(n, g[(i + 1, j)])
        if (i, j + 1) in g: b.edge(n, g[(i, j + 1)])
    for k, (sx, sy, d) in enumerate(b.sockets):
        nx, ny = round(math.cos(math.radians(d))), round(math.sin(math.radians(d)))
        b.node(f"s{k}", sx, sy); b.node(f"d{k}", sx - nx * 120, sy - ny * 120); b.edge(f"s{k}", f"d{k}")
        gi = min(g, key=lambda ij: (xs[ij[0]] - (sx - nx * 120)) ** 2 + (ys[ij[1]] - (sy - ny * 120)) ** 2)
        b.edge(f"d{k}", g[gi])

def hall_pillars(name):
    b = Block(name, 2, SOCK8)
    HT = 448
    shell(b, 480, HT, b.sockets)
    for x in (-240, 240):
        for y in (-240, 240):
            b.box(x - 48, y - 48, 0, x + 48, y + 48, HT, "trim")
            b.box(x - 58, y - 58, 0, x + 58, y + 58, 24, "wall")
            b.box(x - 58, y - 58, HT - 24, x + 58, y + 58, HT, "wall")
            for nx, ny in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if nx * x < 0 or ny * y < 0: continue            # torches on the pillar faces towards the walls
                b.torch(x + nx * 48, y + ny * 48, 170, nx, ny, 360)
    for y in (-240, 240): b.box(-480, y - 16, HT - 40, 480, y + 16, HT, "wood")            # ceiling beams
    for x in (-240, 240): b.box(x - 16, -480, HT - 70, x + 16, 480, HT - 40, "wood")
    b.light(0, 0, 380, 1400, "0.75 0.65 0.9")
    hall_fill(b)
    hall_nodes(b)
    b.slots += [[0, 0, 24, 0, "patrol"], [-320, 320, 24, 315, "guard"], [320, -320, 24, 135, "guard"],
                [0, 240, HT - 90, 270, "ambush"]]
    b.items.append(["ammo_force", -320, -320, 30])
    return b

def hall_flooded(name):
    """the pit from the original map: a pool in the middle (wading deep), walkway around, a bridge across"""
    b = Block(name, 2, SOCK8, minlight=40)
    HT = 448; P = 300; D = 56
    o = 512
    b.box(-o, -o, HT, o, o, HT + T, "rock")
    # floor: ring around the pool at z 0, pool bottom at -D
    b.box(-o, -o, -T, o, -P, 0, "floor"); b.box(-o, P, -T, o, o, 0, "floor")
    b.box(-o, -P, -T, -P, P, 0, "floor"); b.box(P, -P, -T, o, P, 0, "floor")
    b.box(-P, -P, -D - T, P, P, -D, "rock")
    for s in (-1, 1):                                            # pool rims
        b.box(-P, s * P - (8 if s > 0 else 0), -D, P, s * P + (0 if s > 0 else 8), 0, "trim")
        b.box(s * P - (8 if s > 0 else 0), -P, -D, s * P + (0 if s > 0 else 8), P, 0, "trim")
    b.box(-P, -40, -T - 4, P, 40, 0, "wood")                      # plank bridge east-west
    for x in range(-260, 261, 130): b.box(x - 8, -40, -D, x + 8, 40, -T - 4, "wood")
    # walls with the doorways (reuse the shell's walls, but not its floor/ceiling)
    tmp = Block(name, 2, SOCK8); tmp.used = b.used; shell(tmp, 480, HT, SOCK8)
    b.brushes += tmp.brushes[2:]
    b.box(-P + 8, -P + 8, -D, P - 8, -40, -16, "mz_water"); b.box(-P + 8, 40, -D, P - 8, P - 8, -16, "mz_water")
    for x in (-420, 420):                                       # corner pillars, torches facing the pool
        for y in (-420, 420):
            b.box(x - 40, y - 40, 0, x + 40, y + 40, HT, "trim")
            b.torch(x - (40 if x > 0 else -40), y, 170, -1 if x > 0 else 1, 0, 340)
    b.light(0, 0, 200, 1200, "0.45 0.7 0.75")
    hall_fill(b, color="0.6 0.8 0.85")
    hall_nodes(b, skip={(1, 0), (1, 2)})                         # the pool: only the bridge row crosses it
    b.slots += [[-390, 200, 24, 315, "guard"], [390, -200, 24, 135, "patrol"], [0, 0, 24, 90, "patrol"]]
    b.items.append(["item_medpac", 390, 250, 30])
    return b

def hall_lab(name):
    """experiment lab: glowing green specimen tanks, metal tables"""
    b = Block(name, 2, SOCK8, minlight=38)
    HT = 448
    shell(b, 480, HT, b.sockets)
    for x, y in [(-300, -160), (-300, 160), (300, -160), (300, 160), (-120, 0), (120, 0)]:
        b.prism(x, y, 58, 8, 0, 28, "metal", rot=math.pi / 8)
        b.prism(x, y, 44, 8, 28, 300, "mz_liquid", rot=math.pi / 8)
        b.prism(x, y, 58, 8, 300, 330, "metal", rot=math.pi / 8)
        b.box(x - 6, y - 6, 330, x + 6, y + 6, HT, "metal")
    for x, y in [(0, -400), (0, 400)]:
        b.box(x - 120, y - 40, 36, x + 120, y + 40, 44, "metal")
        for lx in (-110, 110):
            for ly in (-30, 30): b.box(x + lx - 4, y + ly - 4, 0, x + lx + 4, y + ly + 4, 36, "metal")
    b.torch(-480, -400, 180, 1, 0, 300); b.torch(480, 400, 180, -1, 0, 300)
    b.light(0, 0, 400, 1000, "0.6 1 0.6")
    hall_fill(b, color="0.6 0.95 0.65")
    hall_nodes(b, skip={(0, 1), (2, 1)})
    b.slots += [[0, -200, 24, 90, "guard"], [0, 200, 24, 270, "patrol"], [-300, 0, HT - 90, 0, "ambush"]]
    b.items.append(["ammo_force", 0, -400, 60])
    return b

def hall_goal(name):
    """Sasuke's chamber: a dais with a throne at the east end, Orochimaru's purple ropes, doors on the west only
    (the other sockets are capped)"""
    b = Block(name, 2, [[-2 * H, H, 180], [-2 * H, -H, 180]], minlight=38)
    HT = 448
    shell(b, 480, HT, b.sockets)
    b.box(240, -300, 0, 480, 300, 16, "trim"); b.box(300, -260, 16, 480, 260, 32, "trim")        # dais, 2 steps
    b.box(400, -60, 32, 460, 60, 124, "wood"); b.box(440, -60, 124, 460, 60, 244, "wood")         # throne
    for y in (-200, 200):                                         # rope across the ceiling, pillars along the hall
        b.box(-480, y - 10, HT - 60, 480, y + 10, HT - 40, "rope")
    for x in (-300, -60, 180):
        for y in (-340, 340): b.box(x - 40, y - 40, 0, x + 40, y + 40, HT, "trim")
    for x in (-300, -60, 180):
        for y in (-340, 340): b.torch(x, y - 40 if y > 0 else y + 40, 180, 0, -1 if y > 0 else 1, 330)
    b.light(380, 0, 300, 1500, "0.7 0.45 1")
    hall_fill(b, color="0.8 0.6 1")
    hall_nodes(b, xs=(-380, -160, 60), ys=(-200, 0, 200))
    b.node("dais", 330, 0, 56); b.edge("g21", "dais")
    b.slots += [[360, 0, 56, 180, "boss"], [100, -160, 24, 180, "guard"], [100, 160, 24, 180, "guard"]]
    return b

def cap(name):
    """closed iron-banded door filling a doorway; socket at the origin, the block's inside towards -x"""
    b = Block(name, 1, [[0, 0, 0]], minlight=40)
    b.box(-16, -DW, 0, -4, DW, DH, "wood")
    for z in (40, 120, 190): b.box(-18, -DW, z, -16, DW, z + 10, "metal")
    b.light(-90, 0, 150, 450); b.fill(-120, 0, 190, 160)
    return b

BLOCKS = [corridor("c_i", "EW"), corridor("c_l", "EN"), corridor("c_t", "EWN", torch_side="S"),
          corridor("c_x", "ENWS"), dead_end("c_end"), room_study("r_study"), room_cell("r_cell"),
          room_start("s_start"), hall_pillars("h_pillars"), hall_flooded("h_flood"), hall_lab("h_lab"),
          hall_goal("s_goal"), cap("cap")]

os.makedirs(f"{base}/maps/mz", exist_ok=True); os.makedirs(f"{base}/shaders", exist_ok=True)
td = f"{base}/textures/{SD}"; os.makedirs(td, exist_ok=True)
sys.path.insert(0, HERE)
import textures  # noqa: E402
textures.make_all(td)
cat = {b.name: b.write() for b in BLOCKS}

sh = ["// procedural Orochimaru maze blocks - generated by tools/maze/blocks.py\n"]
for name, c in cat.items():
    for m in c["shaders"]:
        sh.append(f"textures/{SD}/{name}_{m}\n{{\n\tqer_editorimage textures/{SD}/{TEX[m][0]}.jpg\n\tq3map_material {MAT[m]}\n"
                  f"\t{{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}}\n"
                  f"\t{{\n\t\tmap textures/{SD}/{TEX[m][0]}\n\t\tblendFunc GL_DST_COLOR GL_ZERO\n\t\trgbGen identity\n\t}}\n}}\n")
sh.append(f"""textures/{SD}/mz_liquid
{{
	qer_editorimage textures/{SD}/mz_liquid.jpg
	q3map_material glass
	surfaceparm nolightmap
	q3map_surfacelight 120
	q3map_lightImage textures/{SD}/mz_liquid.jpg
	{{
		map textures/{SD}/mz_liquid
		rgbGen identity
		tcMod scroll 0 0.08
	}}
	{{
		map textures/{SD}/mz_glass
		blendFunc GL_ONE GL_ONE
		rgbGen const ( 0.25 0.25 0.25 )
		tcGen environment
	}}
}}

textures/{SD}/mz_water
{{
	qer_editorimage textures/{SD}/mz_water.jpg
	q3map_material water
	surfaceparm water
	surfaceparm trans
	surfaceparm nonsolid
	surfaceparm nolightmap
	cull none
	{{
		map textures/{SD}/mz_water
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		alphaGen const 0.78
		rgbGen identity
		tcMod scroll 0.01 0.006
		tcMod turb 0 0.03 0 0.12
	}}
}}
""")
open(f"{base}/shaders/shinobi_maze.shader", "w").write("\n".join(sh))
json.dump(cat, open(f"{base}/mz_blocks.json", "w"), indent=1)
print(f"BLOCKS OK {len(cat)}: " + ", ".join(f"{n}({len(c['shaders'])} lit shaders)" for n, c in cat.items()))
