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
