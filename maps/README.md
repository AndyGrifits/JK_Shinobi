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
| props | `props.py` (from prep) | generated cover: boulders, rock pillars / slab, logs, stumps, posts (`map.json` "props"), autoclipped |
| textures | `textures_gen.py` | tileable painted concrete / rock / bark / wood; `texture_override` swaps a model texture |
| sky | `gen_sky.py` | open-air maps: seamless gradient skybox; hull top/sides become sky, `q3map_sun` + `q3map_skyLight` |
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

`python3 tools/maps/lightstats.py <bsp>` - lightmap brightness per shader (0..255, 255 = full texture).
`python3 -I tools/maps/preview_fbx.py <fbx> <tex dir> <out>` - flat renders of a raw model before prep.
`python3 -I tools/maps/bsp_render.py <bsp> <base> <out dir> '<cams json>'` - render the COMPILED map
(texture x lightmap) without the game. `python3 tools/maps/coverage.py <bsp> 20 8 circle 0 0 <r>` - holes in a floor.

Model gotchas: ground faces / vertex normals pointing down make q3map2 give a meta surface a downward
plane - the game culls it (a hole) and it stays unlit: list such shaders in `model.face_up`.

`collision_check` compares what you see from above with what you stand on; `bspinfo` lists
surfaces per shader (all should be `lightmap`) and the cluster of every light / spawn (-1 = in solid).

## Scale

Player: 64 u tall, steps 18 u, walkable slope up to ~45 deg, force jump 1/2/3 reaches 130 / 226 / 418 u.
`map.json` `model.scale` = game units per model unit.

## shinobi_chunin - Chunin Exam Hall

Source: `naruto-sala-examen-chunnin` (11.6k tris, 14 textures). Scale 320: hall 1498 x 2410 x 752,
balconies at 384 (force jump 3 or the stairs at the door end). The hand-seal statue (hands + sleeves)
keeps its scale-160 size (`model.rescale` in map.json), against the far wall.
Spawns: 4 FFA, duel / power duel. No bot routes yet.

## shinobi_chunin_final - Chunin Exam Finals

Source: `chunin-exams-final-stage` (98k tris; 79k after dropping the 2D grass cards). Scale 4: round
field, diameter ~2810, wall 494, 7 trees ~380 high (trunk = column, canopy = slab to stand on), stands
and Hokage building behind the wall (visual only). Open sky, sun from the north-east lighting the stands.
Boundary: playerclip ring on the wall face up to 1600 + ceiling (players only, shots pass); one flat
floor brush. The wall is plain concrete (`texture_override`). 22 props: two rock pillars (410 / 309 high),
a rock slab (172), five boulder groups, three logs, three stumps, three training posts; the centre
(r 300) stays open. Spawns >= 224 u from anything solid.
