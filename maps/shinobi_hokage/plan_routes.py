"""Plan the wooden routes on the Hokage rock for shinobi_hokage (writes the "_route" props into map.json).

    python3 -I maps/shinobi_hokage/plan_routes.py maps/shinobi_hokage/map.json build-maps/shinobi_hokage

Run on a prepared build (prep_model.py) - prop faces (p_*) are ignored. Works in a frame along the cliff:
s along the cliff (south-east = +), depth d from a plane in front of it towards the rock, z up. Every piece is
an arch_bridge (flat walkway, sloped ramp, sagging rope bridge or a landing deck) placed just in front of the
rock / the faces in its height range, so nothing cuts into the carving:
  gallery   flat walkway under the chins (z 700), rail on the open side, stilts down to the ground
  ramps     from the plaza up to both ends of the gallery
  E1        six-flight fire escape between the 2nd and 3rd face, gallery -> top of the rock (z 3340)
  E2        three flights between the 4th and 5th face, gallery -> 1580
  rope      rope bridge E1 <-> E2 at 1580 across the 3rd and 4th face; rope bridge from the residence roof
  top       bridge from E1's top landing onto the plateau behind the faces, rope bridge up to the 3rd head
"""
import json, math, sys
import bpy, bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree

cfg_path, build = sys.argv[-2:]
cfg = json.load(open(cfg_path))
bpy.ops.wm.open_mainfile(filepath=build + "/model.blend")
o = [o for o in bpy.context.scene.objects if o.type == 'MESH'][0]
names = [s.material.name.split('/')[-1] for s in o.material_slots]
solid = {k for k, v in cfg["shaders"].items() if v.get("solid") or "q3map_clipModel" in v.get("raw", [])}
bm = bmesh.new(); bm.from_mesh(o.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if names[f.material_index] not in solid or names[f.material_index].startswith("p_")],
                 context='FACES')
bvh = BVHTree.FromBMesh(bm)

T = Vector((0.667, -0.745, 0)).normalized(); N = Vector((0.745, 0.667, 0)).normalized()
O = Vector((2150, 1400, 0)) - N * 3500
def W(s, d, z): return O + T * s + N * d + Vector((0, 0, z))
def top(p):
    h = bvh.ray_cast(Vector((p.x, p.y, 9000)), Vector((0, 0, -1)), 20000)
    return h[0].z if h[0] is not None else -1e9
_cl = {}
def cliff(s):
    """depth where the rock (or a face) first rises above 1800 - the residence and the houses stay lower"""
    k = int(round(s / 50)) * 50
    if k not in _cl:
        d = 0
        def rock(p_):                    # the residence's roof spires reach 1865: not rock
            return top(p_) >= 1800 and (Vector((p_.x, p_.y, 0)) - Vector((200, -150, 0))).length > 1300 \
                and (Vector((p_.x, p_.y, 0)) - Vector((1300, -500, 0))).length > 600
        while d < 8000 and not rock(W(k, d, 0)): d += 50
        _cl[k] = d
    return _cl[k]
def face(s0, s1, z0, z1, step=40):
    """nearest rock / face depth over a window of the cliff (rays start 1000 in front of the cliff line,
    behind the residence)"""
    best = 9000.0; s = s0
    while s <= s1:
        d0 = cliff(s) - 1000; z = z0
        while z <= z1:
            h = bvh.ray_cast(W(s, d0, z), N, 9000)
            if h[0] is not None: best = min(best, d0 + (h[0] - W(s, d0, z)).dot(N))
            z += step
        s += step
    return best
def r1(v): return [round(v.x, 1), round(v.y, 1), round(v.z, 1)]

WOOD = ["p_deck", "p_wood", "p_wood"]
props = []
def bridge(a, b, width, rise=0, rails=(True, True), stilts=0, seg=None, note=""):
    L = (Vector((b[0], b[1], 0)) - Vector((a[0], a[1], 0))).length
    props.append({"type": "arch_bridge", "a": r1(a), "b": r1(b), "width": width, "rise": rise,
                  "segments": seg or max(2, int(L / 90)), "rails": list(rails), "stilts": stilts,
                  "shaders": WOOD, "_route": note})
def landing(s, z, d_in, d_out, length=170, note="landing"):
    """deck across the lanes at s: from d_in (rock side) to d_out (open side)"""
    bridge(W(s, d_in, z), W(s, d_out, z), length, rails=(False, False), stilts=0, seg=2, note=note)

ZG = 700; WG = 160                       # gallery
gal = []
for s in range(-3200, 3801, 300):
    gal.append((s, face(s - 170, s + 170, ZG - 20, ZG + 150) - 50 - WG / 2))
gal = [(s, min(d for _, d in gal[max(0, i - 1):i + 2])) for i, (s, _) in enumerate(gal)]   # no zigzag in and out
for (sa, da), (sb, db) in zip(gal[:-1], gal[1:]):
    bridge(W(sa, da, ZG), W(sb, db, ZG), WG, rails=(True, False), stilts=2, note="gallery")
def gal_d(s): return min(d for s_, d in gal if abs(s_ - s) <= 300)

def ramp(s_top, toward, note):
    """from the gallery end at s_top down to the plaza along the open side (25 deg)"""
    d = min(gal_d(s_top), gal_d(s_top + toward * 600)) - WG - 20
    gz = 0
    for _ in range(4):
        L = (ZG - gz) / math.tan(math.radians(25)); s_low = s_top + toward * L
        gz = max(top(W(s_low, d, 0)), -400)
    landing(s_top - toward * 60, ZG, gal_d(s_top) + WG / 2, d - WG / 2, 200, note + " landing")
    bridge(W(s_low, d, gz + 2), W(s_top, d, ZG), WG, rails=(True, True), stilts=3, note=note)
ramp(-3200, 1, "west ramp")
ramp(3800, -1, "east ramp")

def escape(sA, sB, z0, rise, legs, note, start_west=True):
    """fire escape: flights alternating between an outer and an inner lane, landings at the turns"""
    """each pair of flights (up and back) hugs the rock / faces of its own height range, so the lower pairs stay
    close to the cliff and only the upper ones step out in front of the faces"""
    zt = z0 + rise * legs; WE = 140
    ends = []; lanes = []
    for k in range(legs):
        if k % 2 == 0:
            rail = face(sA - 120, sB + 120, z0 + rise * k - 20, z0 + rise * min(legs, k + 2) + 180)
            d_in = rail - 50 - WE / 2; d_out = d_in - WE - 12
            if lanes: d_in, d_out = min(d_in, lanes[-1][0]), min(d_out, lanes[-1][1])   # never step back in
            lanes.append((d_in, d_out))
        a_, b_ = (sA, sB) if (k % 2 == 0) == start_west else (sB, sA)
        d = d_out if k % 2 == 0 else d_in
        za, zb = z0 + rise * k, z0 + rise * (k + 1)
        bridge(W(a_, d, za), W(b_, d, zb), WE, rails=(True, True), note=f"{note} flight {k + 1}")
        out = 1 if b_ > a_ else -1
        d_next_out = d_out
        if k % 2 == 1 and k + 1 < legs:                 # the next pair may stand further out
            rail2 = face(sA - 120, sB + 120, zb - 20, z0 + rise * min(legs, k + 3) + 180)
            d_next_out = min(d_out, rail2 - 50 - WE / 2 - WE - 12)
        landing(b_ + out * 85, zb, d_in + WE / 2, d_next_out - WE / 2, 170, f"{note} landing {k + 1}")
        ends.append((b_ + out * 85, zb))
    first = sA if start_west else sB
    d_in0, d_out0 = lanes[0]
    landing(first - (1 if start_west else -1) * 85, z0, max(d_in0 + WE / 2, gal_d(first) + WG / 2), d_out0 - WE / 2, 170, f"{note} foot")
    return lanes[-1][0], lanes[-1][1], ends

e1_in, e1_out, e1 = escape(-450, 450, ZG, 440, 6, "E1", start_west=True)      # gap between the 2nd and 3rd face
e2_in, e2_out, e2 = escape(2100, 3000, ZG, 293, 3, "E2", start_west=False)     # gap between the 4th and 5th face

# rope bridge E1 landing 2 (s ~535, z 1580) <-> E2 top landing (s ~2015, z 1579)
(sa, za), (sb, zb) = e1[1], e2[2]
dr = face(sa, sb, za - 20, za + 160) - 50 - 60
bridge(W(sa + 85, min(dr, e1_out), za), W(sb - 85, min(dr, e2_out), zb), 120, rise=-70, note="rope bridge E1-E2")

# top: E1 top landing -> onto the plateau, and a rope bridge up to the 3rd (tallest) head
st, zt = e1[-1]
d = e1_in
while d < e1_in + 3000 and top(W(st, d, 0)) < zt - 30:
    d += 40
bridge(W(st, e1_in + 70, zt), W(st, d + 160, max(zt, top(W(st, d + 160, 0)) + 2)), 150, note="top bridge to the plateau")
h3 = Vector((3500, 1700, 0)); best = None                 # the 3rd head's crown: highest point near it
for dx in range(-500, 501, 50):
    for dy in range(-500, 501, 50):
        z = top(h3 + Vector((dx, dy, 0)))
        if best is None or z > best[0]: best = (z, h3 + Vector((dx, dy, 0)))
props.append({"_": f"3rd head crown at {r1(best[1])[:2]} z {round(best[0])}"})
cz = best[0] + 2
bridge(W(st, e1_out - 60, zt), Vector((best[1].x, best[1].y, cz)), 110, rise=-60, note="rope bridge to the 3rd head")

# residence roof -> E1 landing 1 (s ~535, z 1140)
sl, zl = e1[0]
res = Vector((200, -150, 0)); lp = W(sl, e1_out - 40, zl)
u = (Vector((lp.x, lp.y, 0)) - res).normalized(); a = res + u * 420
bridge(Vector((a.x, a.y, top(a) + 2)), lp, 120, rise=-80, note="rope bridge from the residence roof")

cfg["props"] = [p for p in cfg.get("props", []) if "_route" not in p and not str(p.get("_", "")).startswith("3rd head")] + props
json.dump(cfg, open(cfg_path, "w"), indent=1)
print(f"ROUTES {sum(1 for p in props if 'type' in p)} pieces; gallery {len(gal)} points, E1 top z {zt}, 3rd head z {round(best[0])}")
