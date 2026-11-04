"""Main map of the procedural Orochimaru maze: a sealed caulk box the blocks are assembled in at load.

    python3 tools/maze/main_map.py <base dir> <map name> <grid cells> <max shaders of a block>

Writes maps/<name>.map, scripts/<name>.arena and appends the pad / caulk shaders to shaders/shinobi_maze.shader.
- Pads: the engine's brush traces read a hit side's surface flags from the MAIN map's shader table even for a
  sub-BSP brush (cm_trace.cpp: cmg.shaders[side->shaderNum]), so the first shader numbers of the main map must
  all be plain rock; q3map2 numbers worldspawn brush shaders from the last brush backwards, so they go last.
- Lights only feed the light grid (players and NPCs): the blocks carry their own baked light.
- The worldspawn key "shinobi_maze" tells the game to assemble a layout (codemp/game/g_shinobi_maze.c).
"""
import sys

base, name, G, nsh = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
SD = "shinobi_maze"; CELL = 512
NPAD = max(32, nsh + 8)

def box(x0, y0, z0, x1, y1, z1, sh):
    s = f"{SD}/{sh} 0 0 0 0.5 0.5 0 0 0"
    return "\n".join(["{",
        f"( {x0} 0 0 ) ( {x0} 1 0 ) ( {x0} 0 1 ) {s}", f"( {x1} 0 0 ) ( {x1} 0 1 ) ( {x1} 1 0 ) {s}",
        f"( 0 {y0} 0 ) ( 0 {y0} 1 ) ( 1 {y0} 0 ) {s}", f"( 0 {y1} 0 ) ( 1 {y1} 0 ) ( 0 {y1} 1 ) {s}",
        f"( 0 0 {z0} ) ( 1 0 {z0} ) ( 0 1 {z0} ) {s}", f"( 0 0 {z1} ) ( 0 1 {z1} ) ( 1 0 {z1} ) {s}", "}"])

X0 = Y0 = -CELL // 2 - 256; X1 = Y1 = (G - 1) * CELL + CELL // 2 + 256; Z0, Z1 = -160, 640; t = 16
hull = [box(X0 - t, Y0 - t, Z0 - t, X1 + t, Y1 + t, Z0, "caulk"), box(X0 - t, Y0 - t, Z1, X1 + t, Y1 + t, Z1 + t, "caulk"),
        box(X0 - t, Y0 - t, Z0, X0, Y1 + t, Z1, "caulk"), box(X1, Y0 - t, Z0, X1 + t, Y1 + t, Z1, "caulk"),
        box(X0, Y0 - t, Z0, X1, Y0, Z1, "caulk"), box(X0, Y1, Z0, X1, Y1 + t, Z1, "caulk")]
pads = [box(X0 + 16 * i, Y0 + 8, Z0 - 12, X0 + 16 * i + 8, Y0 + 16, Z0 - 4, f"pad_{i:02d}") for i in range(NPAD)]
ents = ["{\n" + f'"classname" "worldspawn"\n"message" "Orochimaru\'s Lair (maze)"\n"shinobi_maze" "1"\n'
        f'"_minlight" "45"\n"gridsize" "128 128 128"\n"distanceCull" "8000"\n' + "\n".join(hull + pads) + "\n}\n"]
for i in range(G):
    for j in range(G):
        if (i + j) % 2 == 0:
            ents.append("{\n" + f'"classname" "light"\n"origin" "{i * CELL} {j * CELL} 300"\n"light" "700"\n'
                        f'"_color" "1 0.8 0.65"\n"spawnflags" "1"\n' + "}\n")
# no spawn points here: the layout's start room brings them (g_shinobi_maze.c)
open(f"{base}/maps/{name}.map", "w").write("".join(ents))
open(f"{base}/scripts/{name}.arena", "w").write(
    f'{{\nmap "{name}"\nlongname "Orochimaru\'s Lair (maze)"\ntype "ffa team"\n}}\n')
sh = [f"textures/{SD}/caulk\n{{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nomarks\n}}\n"]
sh += [f"textures/{SD}/pad_{i:02d}\n{{\n\tq3map_material rock\n}}\n" for i in range(NPAD)]
open(f"{base}/shaders/shinobi_maze.shader", "a").write("\n" + "\n".join(sh))
print(f"MAIN OK {name}: box {X0},{Y0} .. {X1},{Y1}, {NPAD} pad shaders")
