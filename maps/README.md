# Maps

Model-based arenas: an environment model (FBX + textures) becomes a JKA map without hand brushwork.

```
tools/maps/get-q3map2.sh                       # once per machine; prints the Q3MAP2=... to export
tools/build-map.sh <name> <model.fbx> <texture dir>   # -> dist/shinobi/shinobi_map_<name>.pk3
```

Copy the pk3 into `GameData/shinobi/`, then `JK_Shinobi_MapTest.bat <name>` (devmap, cheats on).

## What the build does

| step | tool | |
|---|---|---|
| model | `prep_model.py` (Blender `bpy`) | applies transforms, moves the floor centre to the origin, scales to game units, one shader per texture (`textures/<dir>/tNN`), OBJ + JPG/TGA |
| prop clip | `propclip.py` | stepped clip boxes from a top-down raycast of dense props (statues, bushes) |
| map | `gen_map.py` | caulk hull, prop clip, `misc_model` (force meta), lights, spawns, shaders, `.arena` |
| levelshot | `levelshot.py` | loading-screen render (replace with an in-game shot) |
| compile | q3map2 `-game ja` | `-bsp -meta`, `-vis`, `-light -fast -bounce 2` |

Collision: shaders marked `"solid"` in `map.json` get `q3map_clipModel` (q3map2 builds brushes
from their triangles, 8 u thick, flat walls stay flat for wall runs); the rest are `nonsolid` and,
if listed under `prop_clip`, get the stepped boxes. `q3map_material` sets footstep / impact sounds.

## Checks

```
python3 -I tools/maps/heightmap.py build-maps/<name> 8
python3 tools/maps/collision_check.py build-maps/<name>/base/maps/<name>.bsp build-maps/<name>
python3 tools/maps/bspinfo.py build-maps/<name>/base/maps/<name>.bsp build-maps/<name>/base/maps/<name>.map
BSP_ARGS=-debugclip tools/build-map.sh ...      # draws the autoclip brushes in game
```

`collision_check` compares what you see from above with what you stand on; `bspinfo` lists
surfaces per shader (all should be `lightmap`) and the cluster of every light / spawn (-1 = in solid).

## Scale

Player: 64 u tall, steps 18 u, walkable slope up to ~45 deg, force jump 1/2/3 reaches 130 / 226 / 418 u.
`map.json` `model.scale` = game units per model unit.

## shinobi_chunin - Chunin Exam Hall

Source: `naruto-sala-examen-chunnin` (11.6k tris, 14 textures). Scale 160: hall 749 x 1205 x 376,
balconies at 192 (force jump 2 or the stairs at the door end), hand-seal statue at the far end.
Spawns: 4 FFA, duel / power duel. No bot routes yet.
