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

- `.obj` sources (with their .mtl) and `.blend` sources work like FBX (armature modifiers are dropped); `"shader_names": "basename"` names
  shaders after the texture file (when the files share digits).
- `split_details`: loose parts of the listed shaders that are tiny (< `max_dim`) or thin narrow ribbons
  (wires, poles, railings) go to a `<shader>_d` copy without collision; flat horizontal pieces and
  wide sheets (steps, wall tops, single-plane floors) always keep it.
- `fullbright` shaders (sky dome, horizon cards): unlit, no collision, light passes through.
- `texture_recolor` {shader: [[dark], [light]]}, `texture_copy` {new shader: model shader}.
- `ground_shaders`: props sit on that terrain; `ring`: an adobe ring along a superellipse (props.py
  `plan_ring`), pushed outwards where it would hit the model; `boundary.superellipse` for the edge.
- `worldspawn._blocksize "0"` for big open maps (no pointless 1024-block vis split); `distanceCull` (MP renderer
  default 6000) must reach the far side of a sky dome, or the game culls it and leaves a round hole in the sky.
- `model.stretch`: raise a storey - vertices of the listed shaders within r of a point move up by dz above z1,
  proportionally between z0 and z1. `split_details.regions`: parts wholly inside a box lose collision above z.
- `plaza`: slices the coarse terrain into a grid around a rect, levels it (blending back over `margin`) and
  can cut a `hole` (for a sunken `pool` prop). `water`: water brushes; `extra_shaders`: hand-written shaders
  (`raw` stages, `{tex}` = a generated texture) for brushes; a model shader with `raw` replaces the generated one.
- `slabs`: smooth (flat or sloped) clip walkways from / to [x, y, top z], e.g. over a bumpy deck.
- `_d` detail copies and `"twosided"` shaders are drawn from both sides (single-sided railings of a rip).
- `model.uv_alpha_split`: atlas faces that sample cut-out texels get their own alpha-tested shader, so the rest
  of an atlas stays an opaque JPG (`texture_opaque`, `texture_size` for a bigger / smaller copy).
- `model.uv_terrain`: ground textured with a blurry overview image is repainted: a colour map over a region
  built from the overview colours (classes -> textures_gen kinds, soft blend) plus a tiled grain stage.
- `far`: faces outside the play area (superellipse + margin) go to `<shader>_far`: no collision, vertex lit.
- `model.delete_faces`: open up a band of a wall (a `gallery` prop gives it floor, ceiling and posts);
  `model.drop_beyond`: faces farther than r from the origin go.
- `water_fill` (`tools/maps/waterfill.py`): water brushes in every open channel below a level.
- `stairs` (`tools/maps/stairs.py`): one smooth clip ramp per staircase (see the sand village).
- `import_textures`: images from another asset set (crop / resize / `key_sky` makes a panorama's sky
  transparent); `sky.clouds`: a cloud stage on the sky shader; `sky.hills`: a panorama painted into the skybox.
- props: `ledges` (platforms hugging a round wall, inner edge found by raycasts), `pool`, `furniture`
  (desk / shelf at a given floor z), `dust_ring` (translucent scrolling dust curtains along a superellipse),
  `crates` sit on the lowest ground under each crate; Konoha: `waterway` (stream + pond, cuts the ground and
  adds its water), `arch_bridge` (walk-through deck on smooth clip slabs), `stones`, `pavilion`, `hall`
  (enterable, doors per side), `stall` (awning, sign, noren, stools), `gallery`, `fence`; Orochimaru:
  `tunnel` (closed corridor along a polyline `[x, y, floor z, height]`, open at both ends, model shaders).
- `model.openings`: boxes that cut doorways into the model's walls (faces split on the box planes, the
  inside goes) - where a new `tunnel` meets an existing corridor.
- `model.squash`: lower a mountain without squashing its carvings - where the top surface is above a height,
  heights between `zb` and `zh` shrink by `k` and everything above `zh` comes down as it is (`keep` circles stay,
  `rigid` [x, y, r, z0, z1] parts move as one piece). `arch_bridge` options: `rise` < 0 sags (rope bridge),
  `shaders`, `rails` [left, right], `stilts` (posts down to the ground).
- `clip_ring`: an irregular playerclip fence (radius per point, bottom per segment) around a crater whose
  rim is torn open; bottoms sit just under the rim so tunnels and roofs below stay free.

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
`python3 -I tools/maps/preview_fbx.py <fbx|obj|blend> <tex dir> <out>` - flat renders of a raw model before prep;
`python3 -I tools/maps/preview_prep.py <build> <out> '<cams>'` - the same for the prepared model (props, retexturing).
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
its terrain and the rip's anime sky dome (`distanceCull 16000`: it is off-centre and ~9000 away); the
horizon mountain cards recoloured from snow to desert rock; flat sand added out to the horizon. Added: a
ring of 33 Tatooine-like adobe towers, blocks and town walls around it (the map's edge; flat roofs inside a
parapet to fight on), sandstone pillars and slabs, ruins, crates and boulders on the empty sand.
Kazekage tower: the open top storey raised from 94 to 200 u (floor 1343, roof 1738), lit, with a desk,
two scroll shelves and crates (a Chakra Pill on the desk); 13 adobe ledges spiral up the dome (~115 u
apart) to the storey and on to the roof. North: a levelled plaza with a sunken pool (1032 x 572 water,
96 deep, steps out at both ends, coping and a low fence with a gap on every side) - the water-walking
arena. The bridge between the two houses has a flat clip floor (452) and a ramp up to the west roof;
its deck, rails and bumps are walk-through. Dust storm: two translucent curtains scrolling along the edge
(one stage each). 8 FFA spawns, duel / power duel, 12 items (Fuma on the Kazekage roof and the NW pillar,
tags on the SE slab and the pool's NW corner, kunai on the pillar / boulder / ruin, pills by the ring and
on the desk). Boundary: playerclip superellipse outside the ring up to 2700.

## shinobi_konoha - Village Hidden in the Leaves

Sources: `naruto-hiddenly-village` (FBX: the whole village, 56k tris on one 2828 px atlas) at scale 6000,
origin in the canal quarter south of the pagoda; `konoha-hidden-leaf-village` (town.obj) gives the cloud
layer, the distant hills in the skybox and the shop signs. Play area: superellipse 3150 x 2950 around
(150, 250); the rest of the village and the Hokage rock with the five faces are backdrop (vertex lit,
no collision). The ground's overview texture is repainted (lawns / paths / canal bed).
Zones: the canal (water at -300, walkway at -241 on the north side, sloped south bank) with the stone road
bridge, a red arched footbridge and seven stepping stones; the pagoda with an open, lit top storey (desk,
shelves) reached by nine wooden ledges around it; a garden pond with an island pavilion, stepping stones
and a stream with a small arched bridge running into a culvert; a market lane (three stalls) facing
Ichiraku (noren, stools) and a meat stall; the south field with an enterable dojo (three doors, tatami,
lit) and training ground 3 (three posts, the memorial stone). Board fences close the streets at the edge,
water gates the canal. 9 FFA spawns, duel / power duel, 12 items.

## shinobi_orochimaru - Orochimaru's Hideout

Source: `naruto_slugfest_orochimaru_hideout` (Slugfest rip, OBJ, 16 meshes, 28.5k tris) at scale 320, origin at
the crater's floor centre. The main corridor feeds three side corridors: to the big hall (and the snake hall
behind it), to the open crater, and to a corridor at the top. Two new tunnels close the dead ends into loops:
A runs from the big hall's north wall to the crater's west dead end, B crosses that dead end, runs north under
the plateau and east into the end of the top corridor (doorways cut with `model.openings`). The crater is the
open arena: a crown of 40 boulders (crater rock texture) covers the torn rim and a `clip_ring` keeps players
inside it. Torch-coloured lights every ~420 u over the covered floor, the crater sunlit; 12 FFA spawns (duel /
power duel), 12 items, all on floor reachable from the crater.

## shinobi_hokage - Hokage Residence

Same FBX as shinobi_konoha, a different district: origin on the plaza of the Hokage residence (konoha game
coords (4800, 6400, 370)), play area superellipse 5000 x 5000 (n 2.6): the walled oval plaza with the round
residence and its two side towers, the streets and houses to the south-west, and the Hokage rock. The rock was
~5000 u tall with the faces in its upper half: `model.squash` shrinks the cliff below the faces (x0.3) and brings
the faces and the rock top down unchanged (chins ~1000-1800, top ~3300-3600); the five small cliff houses
move as rigid pieces. `maps/shinobi_hokage/plan_routes.py` (run on a prepared build, writes the `_route`
props) lays wooden routes just in front of the rock and the faces: a gallery under the chins (z 700, stilts)
with ramps from the plaza at both ends, a six-flight fire escape between the 2nd and 3rd face up to the top
(z 3340) with a bridge onto the plateau and a rope bridge up to the crown of the 3rd face, a three-flight
escape between the 4th and 5th face, a rope bridge between the two escapes across the faces at 1580, and a
rope bridge from the residence roof to the first escape. Rooms (residence, houses) are lit by `rooms`.
17 FFA spawns (3 on the rock), 14 items (4 on the rock routes), the green-blue wind haze at the edge.

