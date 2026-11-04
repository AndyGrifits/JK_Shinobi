"""Runtime-assembly spike for the procedural Orochimaru maze (misc_bsp instances).

    python3 tools/maze/spike.py <out base dir> <texture dir with oro_wall.jpg, oro_ground.jpg, oro_rock.jpg>

Writes (all into <out>/):
  maps/mz/spk_a.map   corridor block, orange light, its own shaders  textures/shinobi_maze/a_*
  maps/mz/spk_b.map   corner room (openings +X, +Y), blue light, a pillar and a medpac, shaders b_*
  maps/mz/spk_c.map   the corridor again with a GREEN light but A's shader names (control: the renderer
                      caches shaders by name + lightmap number, so C is expected to show A's orange light)
  maps/shinobi_maze_spike.map   a sealed caulk box with a ring of misc_bsp instances in all four yaws:
                      rooms at the corners (0/90/180/270), corridors between them (A at 0/90/180, C at 270)
  shaders/shinobi_maze_spike.shader, scripts/shinobi_maze_spike.arena, textures/shinobi_maze/*.jpg

Blocks are built around their cell centre (cell 512, floor z 0); doorways are 160 wide, 224 high, centred
on the cell edges.
"""
import os, shutil, sys

out, texdir = sys.argv[1], sys.argv[2]
SD = "shinobi_maze"
CELL = 512; H = CELL // 2; DW = 80; DH = 224; T = 16

def box(x0, y0, z0, x1, y1, z1, sh):
    s = f"{SD}/{sh} 0 0 0 0.5 0.5 0 0 0"
    return "\n".join(["{",
        f"( {x0} 0 0 ) ( {x0} 1 0 ) ( {x0} 0 1 ) {s}", f"( {x1} 0 0 ) ( {x1} 0 1 ) ( {x1} 1 0 ) {s}",
        f"( 0 {y0} 0 ) ( 0 {y0} 1 ) ( 1 {y0} 0 ) {s}", f"( 0 {y1} 0 ) ( 1 {y1} 0 ) ( 0 {y1} 1 ) {s}",
        f"( 0 0 {z0} ) ( 1 0 {z0} ) ( 0 1 {z0} ) {s}", f"( 0 0 {z1} ) ( 0 1 {z1} ) ( 1 0 {z1} ) {s}", "}"])

def ent(kv, brushes=()):
    return "{\n" + "".join(f'"{k}" "{v}"\n' for k, v in kv.items()) + "".join(b + "\n" for b in brushes) + "}\n"

def light(x, y, z, val, col):
    return ent({"classname": "light", "origin": f"{x} {y} {z}", "light": val, "_color": col})

def corridor(p, col):
    """along X through the whole cell: floor, ceiling, two side walls, open ends"""
    b = [box(-H, -DW - T, -T, H, DW + T, 0, p + "floor"),
         box(-H, -DW - T, DH, H, DW + T, DH + T, p + "ceil"),
         box(-H, -DW - T, 0, H, -DW, DH, p + "wall"),
         box(-H, DW, 0, H, DW + T, DH, p + "wall"),
         box(-40, -DW, 0, 40, -DW + 12, DH, p + "wall"),      # ribs: something to read the light on
         box(-40, DW - 12, 0, 40, DW, DH, p + "wall")]
    return ent({"classname": "worldspawn", "_minlight": "8"}, b) + \
        light(-140, 0, 170, 520, col) + light(140, 0, 170, 520, col)

def room(col):
    """corner room: interior -224..224, 320 high, doorways in the +X and +Y walls, a pillar, a medpac"""
    I = H - 32; RH = 320; b = []
    b += [box(-H, -H, -T, H, H, 0, "b_floor"), box(-H, -H, RH, H, H, RH + T, "b_ceil")]
    b += [box(-H, -H, 0, -I, H, RH, "b_wall"), box(-H, -H, 0, H, -I, RH, "b_wall")]          # -X, -Y: solid
    for ax in "xy":                                                                         # +X, +Y: doorway
        for lo, hi, z0, z1 in [(-H, -DW, 0, RH), (DW, H, 0, RH), (-DW, DW, DH, RH)]:
            b.append(box(I, lo, z0, H, hi, z1, "b_wall") if ax == "x" else box(lo, I, z0, hi, H, z1, "b_wall"))
    b.append(box(-142, -142, 0, -78, -78, RH, "b_pillar"))
    return ent({"classname": "worldspawn", "_minlight": "8"}, b) + light(40, 40, 280, 1500, col) + \
        ent({"classname": "item_medpac", "origin": "110 110 24"})

os.makedirs(f"{out}/maps/mz", exist_ok=True)
for d in ("shaders", "scripts", f"textures/{SD}"):
    os.makedirs(f"{out}/{d}", exist_ok=True)
open(f"{out}/maps/mz/spk_a.map", "w").write(corridor("a_", "1 0.55 0.2"))
open(f"{out}/maps/mz/spk_b.map", "w").write(room("0.4 0.6 1"))
open(f"{out}/maps/mz/spk_c.map", "w").write(corridor("a_", "0.3 1 0.35"))

# main map: sealed caulk box around the ring; lights only feed the light grid (players / NPCs)
X0, Y0, Z0, X1, Y1, Z1 = -H - 128, -H - 128, -128, 2 * CELL + H + 128, 2 * CELL + H + 128, 512
t = 16
hull = [box(X0 - t, Y0 - t, Z0 - t, X1 + t, Y1 + t, Z0, "caulk"), box(X0 - t, Y0 - t, Z1, X1 + t, Y1 + t, Z1 + t, "caulk"),
        box(X0 - t, Y0 - t, Z0, X0, Y1 + t, Z1, "caulk"), box(X1, Y0 - t, Z0, X1 + t, Y1 + t, Z1, "caulk"),
        box(X0, Y0 - t, Z0, X1, Y0, Z1, "caulk"), box(X0, Y1, Z0, X1, Y1 + t, Z1, "caulk")]
inst = [("spk_b", 0, 0, 0), ("spk_b", 2, 0, 90), ("spk_b", 2, 2, 180), ("spk_b", 0, 2, 270),
        ("spk_a", 1, 0, 0), ("spk_a", 2, 1, 90), ("spk_a", 1, 2, 180), ("spk_c", 0, 1, 270)]
m = ent({"classname": "worldspawn", "message": "Maze spike", "_minlight": "60", "gridsize": "128 128 128"}, hull)
for blk, cx, cy, yaw in inst:
    m += ent({"classname": "misc_bsp", "bspmodel": f"mz/{blk}", "origin": f"{cx * CELL} {cy * CELL} 0", "angle": yaw})
    m += light(cx * CELL, cy * CELL, 200, 400, "1 1 1")
m += ent({"classname": "info_player_deathmatch", "origin": "-150 0 40", "angle": "0"})
open(f"{out}/maps/shinobi_maze_spike.map", "w").write(m)

# shaders: every block gets its own names (b_*); C deliberately reuses A's
src = {"wall": "oro_wall", "floor": "oro_ground", "ceil": "oro_rock", "pillar": "oro_wall"}
for t_ in set(src.values()):
    shutil.copy(f"{texdir}/{t_}.jpg", f"{out}/textures/{SD}/{t_}.jpg")
sh = [f"// maze spike - generated by tools/maze/spike.py\n",
      f"textures/{SD}/caulk\n{{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nomarks\n}}\n"]
for p, parts in (("a_", ("wall", "floor", "ceil")), ("b_", ("wall", "floor", "ceil", "pillar"))):
    for k in parts:
        tex = f"textures/{SD}/{src[k]}"
        sh.append(f"textures/{SD}/{p}{k}\n{{\n\tqer_editorimage {tex}.jpg\n\tq3map_material rock\n"
                  f"\t{{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}}\n"
                  f"\t{{\n\t\tmap {tex}\n\t\tblendFunc GL_DST_COLOR GL_ZERO\n\t\trgbGen identity\n\t}}\n}}\n")
open(f"{out}/shaders/shinobi_maze_spike.shader", "w").write("\n".join(sh))
open(f"{out}/scripts/shinobi_maze_spike.arena", "w").write(
    '{\nmap "shinobi_maze_spike"\nlongname "Maze spike"\ntype "ffa"\n}\n')
print(f"SPIKE OK: 3 blocks, {len(inst)} instances")
