# Procedural maze (misc_bsp runtime assembly)

Spike: `tools/maze/build-spike.sh <oro texture dir>` -> `shinobi_maze_spike` (+ `JK_Shinobi_MazeSpike.bat`,
`shinobi_mazespike.cfg`: screenshots + collision check). Verified in game (2026-11-04):

- `misc_bsp` blocks load, render and collide in all four yaws; entities inside a block (items) are moved and
  rotated with it.
- Each block needs **its own shader names**: the renderer caches shaders by name + lightmap number and every
  sub-BSP numbers its lightmaps from 0, so two blocks sharing a shader name share one lightmap (the control
  block C, green light with A's names, renders with A's orange light).
- The engine's brush traces read a hit side's surface flags from the **main map's** shader table
  (`cm_trace.cpp: cmg.shaders[side->shaderNum]`) even for a sub-BSP brush: the main map must carry at least as
  many plain (rock) shaders as the biggest block uses, at the lowest numbers, or players slide on garbage
  flags. q3map2 numbers worldspawn brush shaders from the last brush backwards: the pads go last in the file.
- Characters are lit by the main map's light grid only (flat light inside blocks).

## Full maze (shinobi_oro_maze)

`Q3MAP2=... tools/maze/build.sh [layouts=300]` -> `dist/shinobi/shinobi_map_shinobi_oro_maze.pk3`:
- `textures.py` tileable lair textures; `blocks.py` the 13 blocks (corridors I/L/T/X/end, study, cell, start,
  halls pillars / flooded / lab, Sasuke's chamber, cap = closed door) + catalogue `mz_blocks.json` (doorways,
  nav nodes / edges, NPC slots, spawns, items); `compile-blocks.sh` one BSP per block (padded: the vanilla
  renderer reads one lightmap past a 1-lightmap BSP and crashed); `navcheck.py` checks every node / edge of
  the blocks and of whole layouts against the brushes (it reproduced the game's blocked-edge log exactly);
  `layout.py` growing-tree layouts on a 9x9 grid with loops, caps and the goal deepest; `main_map.py` the
  caulk box with the shader pads.
- Game side (main branch): `codemp/game/g_shinobi_maze.c` assembles a layout at load and runs the Sound ninja
  director / AI; cvars `shinobi_maze_layout` (-1 random), `shinobi_maze_npcs`, `shinobi_maze_debug`; cheat
  `maze_goto [role|n]`. NPCs `oto_ninja` / `oto_jonin` in `assets/ext_data/NPCs/shinobi.npc`.
- `JK_Shinobi_MazeTest.bat` + `shinobi_mazetest.cfg`: tour, spotted by a patrol, search, the keeper.
