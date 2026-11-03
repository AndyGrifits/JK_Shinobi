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

## Other model options (map.json "model" / top level)

- `.blend` sources work like FBX (armature modifiers are dropped); `"shader_names": "basename"` names
  shaders after the texture file (when the files share digits).
- `split_details`: loose parts of the listed shaders that are tiny (< `max_dim`) or thin narrow ribbons
  (wires, poles, railings) go to a `<shader>_d` copy without collision; flat horizontal pieces and
  wide sheets (steps, wall tops, single-plane floors) always keep it.
- `fullbright` shaders (sky dome, horizon cards): unlit, no collision, light passes through.
- `texture_recolor` {shader: [[dark], [light]]}, `texture_copy` {new shader: model shader}.
- `ground_shaders`: props sit on that terrain; `ring`: an adobe ring along a superellipse (props.py
  `plan_ring`), pushed outwards where it would hit the model; `boundary.superellipse` for the edge.
- `worldspawn._blocksize "0"` for big open maps (no pointless 1024-block vis split).

## Items

`map.json` "items": `{"class": ..., "at": [x, y, z]}` with stock classnames; the mod turns them into
ninja items at load (`codemp/game/bg_shinobi.c` shinobiItemMap): `weapon_thermal` Explosive Kunai,
`weapon_trip_mine` Explosive Tag, `weapon_rocket_launcher` Fuma Shuriken, `item_medpac` Healing Pill,
`ammo_force` Chakra Pill. An item drops straight down onto whatever is solid below `at` (put it a bit
above a rock to land on top); `python3 tools/maps/itemcheck.py maps/<name>/map.json <bsp>` replays that.

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
Spawns: 4 FFA, duel / power duel. Items: Fuma Shuriken, Explosive Tag, 2 x Explosive Kunai in the four
corners (combat items only). No bot routes yet.

## shinobi_chunin_final - Chunin Exam Finals

Source: `chunin-exams-final-stage` (98k tris; 79k after dropping the 2D grass cards). Scale 4: round
field, diameter ~2810, wall 494, 8 trees ~380 high (collision: a trunk column up to the fork with a small
landing on top; roots, branches and leaves are walk-through, so you can hide in the canopy), stands
and Hokage building behind the wall (visual only). Open sky, sun from the north-east lighting the stands.
Boundary: playerclip ring on the wall face up to 1600 + ceiling (players only, shots pass); one flat
floor brush. The wall is plain concrete (`texture_override`). 22 props: two rock pillars (410 / 309 high),
a rock slab (172), five boulder groups, three logs, three stumps, three training posts; the centre
(r 300) stays open. Spawns >= 224 u from anything solid. Items: Fuma Shuriken on the tall pillar,
Explosive Kunai on the short pillar and the east boulder, Explosive Tag on the rock slab, Healing Pill
by the north wall, Chakra Pill by the south-west wall.

## shinobi_sand_village - Village Hidden in the Sand

Source: `Crimson Storm - Sand Village 1` (.blend, 28.5k tris, metres). Scale 30. Kept: the whole village,
its terrain and the rip's anime sky dome; the horizon mountain cards recoloured from snow to desert
rock; flat sand added out to the horizon. Added: a ring of 33 Tatooine-like adobe towers, blocks and
town walls around it (the map's edge; roofs to fight on), sandstone pillars and slabs, ruins, crates
and boulders on the empty sand. 8 FFA spawns (open ground, >= 212 u from anything solid), duel /
power duel, 11 items (Fuma on the Kazekage tower and the NW pillar, tags on the slabs, kunai on the
pillar / boulder / ruin, pills by the ring). Boundary: playerclip superellipse outside the ring up to 2700.
