# JK_Shinobi - ideas for later

## Alternative cast: hold back while casting

Back held at the moment of the cast (charged jutsu: at release; held jutsu: at the press)
casts a different form of the technique.

| Jutsu | Now | Back + cast | Effort |
|---|---|---|---|
| Rasengan | ball in hand, attack = dash | leap up and strike down-forward at 45 degrees at once, hits on contact / landing, no extra attack | medium |
| Chidori | same, long dash | the same leap and 45-degree strike, with the lightning streak | medium |
| Katon | one great fireball | Hosenka: 3/4/5 small fireballs in a fan / arc, each leaves a small patch of fire | low |
| Water Wave | wave rolls ahead | ride forward on the crest of the wave | medium |
| Shadow Bind | shadow crawls ahead | a shadow circle grows around you while held (and paid for); catches everyone in it | medium |
| Rock Pillar | block where you look | the block erupts under you and throws you up (escape / high ground) | low |
| Wind Wall | wall in front | a dome of wind around you: reflects from every side, shorter and smaller | low |
| Storm Wind | cone, throws back | reverse gust: pulls enemies in the cone toward you | low |
| Wind Blade | 110-degree arc ahead | a 360-degree ring around you, shorter reach | low |
| Water Bullets | burst of bullets | Suijinheki: a water wall around you - puts out fire, stops missiles, leaves a big puddle | medium |
| Doton (earth wall) | wall ahead | a ring of pillars around you, full cover for a couple of seconds | medium |
| Earthquake | circle around you | a fissure straight ahead: narrow, twice as long | medium |
| Burrow | go under the ground | Headhunter: dive, come up under the nearest enemy ahead and bury him to the neck (rooted a couple of seconds, can only block) | medium |
| Shunshin | blink forward | blink backward with a puff of smoke | low |
| Lightning Clone | runs at the enemy | stays where it is as a mine, goes off when an enemy comes near | low |
| Ash Cloud | cloud 200 u ahead | a cloud around you: a smoke screen to get away | low |
| Hidden Mist | fixed bank of fog | smaller fog that moves with you | low |
| Healing | heals you | heals the ally in front of you (team modes) | low |

No alternative (for now): Fire Breath, Water Prison, Water Clone, Kage Bunshin, Kawarimi,
Hiraishin, Lightning Trap, Earth Clone, Stone Skin, Wind / Lightning Edge, Sky Step,
Lightning Dash, Shadow Hide, Flash, Shadow Trap.

Suggested first batch: Rasengan, Chidori, Katon, Water Wave, Shadow Bind, Rock Pillar,
Storm Wind (pull), Shunshin (back), Wind Wall (dome). Second: Burrow, Earthquake, water
wall, earth ring, Wind Blade ring.

## Henge (transformation) - instead of Shadow Hide

Map decoration is mostly compiled into the BSP, so "any object in the scene" won't work.
Plan: the plain cast takes the looks of another player (model, colours, accessories, name
over the head); cast holding back turns you into a prop of our own set (rock, log, crate,
barrel). Hitbox and damage stay yours; an attack, a jutsu or a hit ends it in a puff; the
Byakugan sees through. Chakra to start, up to 30 s; slow creeping as a prop.

## Weapons

Weapons submenu (fists / one blade / two blades / double-ended) with real swords as
.sab hilts (`noBlade`, `trailStyle 2`, ...) - waiting for the sword models.

## Animations

Mixamo animations retargeted onto the _humanoid skeleton (bpy works headless in the build
container); start with one move (the launcher) to prove the pipeline.
