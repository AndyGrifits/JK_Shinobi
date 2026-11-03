# JK_Shinobi

Naruto-style jutsu mod for Star Wars Jedi Knight: Jedi Academy multiplayer, built on [OpenJK](https://github.com/JACoders/OpenJK) (GPLv2).

## Character screen (Appearance)

**Appearance** on the build screen opens our own character screen instead of the stock
profile menu: a live 3D preview (drag to turn it), the character (a tile per player
model with a `shinobi.char` - Asuma, Kakashi and whatever is added; icon_<look>.tga on
the tile), its look (`model_<look>.skin`: default, props...), a "Take all off" button
for the accessories, and up to 4 accessories, each with its own fit: size (40-250%), forward / left / up
(+-15 units) and tilt forward / turn / tilt sideways. Everything goes out at once through
the userinfo (`model`, `sex`, `char_color_*`, `shinobi_acc`), so all players see it.

`shinobi_acc` / the configstring `acc`: `name[,scale,fwd,left,up,pitch,yaw,roll];...`
(scale in %, offsets in tenths of a unit, angles in degrees; a bare name = as modelled).
The server re-writes it from what parses (at most 4, 200 characters).

Accessories are `models/accessories/head/<name>.md3` (+ texture). Model them in Blender
in the bind pose of the stock `_humanoid` skeleton, sitting on the head where they belong,
facing the character's front (-Y in the glm). Weights don't matter: the game keeps them
rigid on the `cranium` bone (bolt now x inverse bind pose from `_humanoid.gla`, plus a fixed
axis/scale fix between the gla's and the glm's frames - see `Shinobi_AccPlace`). Export a
.glm and convert: `python3 tools/glm2acc.py model.glm texture.png <name>`.

## Fighting for chakra

Chakra comes back by itself at the normal pace only up to 30% of the pool, four times slower
above that; the rest is earned in the fight (x1.1 per Chakra regen point, x2 with the Curse
Seal awake):

| | chakra |
|---|---|
| fist / blade damage you deal | 25% of it |
| jutsu damage you deal | 12% of it |
| damage you take | 10% of it (a way back into the fight) |
| your hit lands on a guard | +3 |
| a combo finisher (or the air slam) lands | +10 |
| you break a guard | +15 |
| your perfect parry | +10 |

The bonus ones show over your head (`+10 chakra`). Any direct hit (not burning, drowning,
ash or a fall) breaks the seals of a jutsu being charged ("Interrupted"; a formed Rasengan /
Chidori stays).

## Quick cast: Shift + the jutsu key

Shift + F (any jutsu that charges; not the held ones, not Shunshin): no charging - its first
level comes out at once (the Great Fireball: level 2), for the price of the top level. With
back held too it is that jutsu's alternative form (for now: Storm Wind pulls). Shift is the
"more chakra" modifier all round.

## Kawarimi and Shunshin charges

Both work on charges (the pips over the jutsu list): Kawarimi 3, each back 15 s after use
on its own; Shunshin 3, each back in 6 s (and cheaper: 10 / 15 / 20). Kawarimi puts you
80 u back, already hidden, works lying on the floor too, and breaks you out of a combo
(flinch, air juggle), a Water Prison, a Shadow Bind or Trap.

## Knocked down

On the floor 0.75 s at least (stock 1.1), then a direction key rolls you up that way, jump
flips you up (stock moves); doing nothing you stand up 0.7 s later (stock 4 s). Bots mostly
roll up to a side. Kawarimi works from the floor.

## Bots defend

Bots wear our characters (a random `shinobi.char` model nobody else wears yet, instead of
their bots.txt one). `shinobi_botfists` % of their lives (default 50) they fight bare-handed
with our combo: fists in hand, the attack held while someone is in reach, a finisher picked
per combo (kick / launcher / sweep / spin), the air combo after their own launcher, and a
random fist style.

Bots raise the guard against a blow coming at them (`shinobi_botguard`, default 55 %), held
a moment (a press right before the hit is a perfect parry), drop it when it is nearly gone,
Kawarimi out of a combo after the second blow half the time (and now and then off the floor
with someone over them), and hit whoever is reeling in front of them.

## Taijutsu combo (fists)

Three styles, switched with **L** (like the blade stances; shown over the jutsu list):

| style | chain | per hit | finisher | guard eaten |
|---|---|---|---|---|
| Fast | jab, cross, jab | 11 / 15 / 21 | x1 | x1 |
| Medium | jab, roundhouse kick, side kick | 14 / 20 / 26 | x1.15 | x1.2 |
| Strong | shoulder ram, double kick | 24 / 32 | x1.3 | x1.8 |

Slower styles hold the victim longer per blow (the flinch follows the attacker's pace). The
style stays over respawns.

Hold attack (or keep pressing it): jab - cross - jab, each next one within ~0.45 s of the
last move's end, then a finisher picked by the direction held at that moment:

| held | finisher | what it does |
|---|---|---|
| nothing | front kick | 35 damage, pushed away and knocked down |
| forward | rising kick | 32 damage, thrown up into the air |
| back | sweep: a forward roll into his legs | 28 damage, knocked down on the spot, to each one it rolls into |
| left / right | spin kick | 25 damage all around you, nothing more (no push, no knockdown): a second chance to land a hit |

Each hit of the chain hits harder: punches 11 / 15 / 21, then the finisher - the four together
a bit less than four blade swings, so a chain is worth stepping away from or guarding against.
Punches reach ~60 u from you (stock fists: ~35, you had to be in his face). Each chain punch makes the victim flinch until the attacker's next hit can land (his weapon
time + 0.25 s, 0.4-0.9 s): rooted, no jumping, attacks, throws or jutsu (a blade swing in
progress bounces, a charge is dropped); only raising the guard (which breaks the chain) or
Kawarimi gets him out. The finisher has no flinch.
The kicks keep the stock kick knockdown.

**Air combo**: jump within ~0.45 s after the launcher (forward + attack finisher) and you
leap after him; he is helpless until he lands (Kawarimi still works). Up there two punches
keep him up (you hang in the air with him), the next attack is the diving kick that slams
him down: 18 damage (x the style's finisher), knocked down, no falling damage on top. A blocked or parried
hit breaks the chain; block held from before doesn't come up until a kick, a grab or a blade's special attack is over. Attack with someone lying right in front of you (up to ~96 u): a
two-handed blow down at him (the saber stab-down animation, 21 damage). Attack in the air
(at least ~1 m up; also off a wall run or a flip - pressed early, it waits until it can):
a kick forward and down at 45 degrees, steered with the view (no falling damage), 10 damage to everyone within 90 u of where you
land. Block + attack: the stock grab. Taijutsu stat, Gates, Edges and the Gentle Fist
apply as to any fist hit. The state is in `ps.stats[13..15]`, predicted.

`shinobi_hitnumbers` (server, on by default) shows floating combat text over everyone hit:
the damage (`-37`; red over yourself) and what the hit did - Flinch, Stagger, Knockdown, Launch,
Stunned, Shadow Trap / Bind, Water Prison, Block (and how much the guard took), Parry, Guard
break. `shinobi_hitnumbers 0` turns it off.

`shinobi_dps 1` (server) logs every attacker's damage per second in 5 s windows, split into
fists / blade / jutsu, to the console and qconsole.log (`[dps] ...`).

## Map water (lakes, rivers)

- **Walking on water**: while you have chakra you stand on map water like on ground
  (2 chakra a second while on it). Crouch to dive in and swim (stock swimming, no casting
  under water, drowning as stock); jump at the surface to come back up onto it. A stun, an
  electric shock, a knockdown, a Water Prison or an empty pool and you go under.
- **Suiton** cast standing on water costs 30% less (on an ice floe - free standing - it doesn't).
- **Lightning** reaching the water (Chidori, Lightning Dash, Lightning Clone, anything that
  electrifies puddles) electrifies a patch of it, radius 250, for 3 s: damage and a stun to
  everyone on it or swimming under it (they sink, too). More lightning there keeps it going.
- **Fire** on water only steams (a steam cloud on the surface); no burning ground on it.
- **Wind** (Wind Wall, Storm Wind) over water freezes a floe, radius 200, for 15 s: stand on it
  without chakra, slippery; fire melts it.
- **Earth**: the Earthquake doesn't reach anyone standing on water (and can't be cast from it);
  no puddles, mud or dust are left on water.
- Swimming wets you (fire weaker, lightning stronger); standing on the surface doesn't.

The map's water must be ordinary water brushes with a flat top; any depth.

## Wind and water: ice (not canon, but fun)

- The wind freezes puddles: a Wind Wall freezes those on it and a little before / behind
  it, Storm Wind those in its cone. Ice (24 s) is slippery (no friction, little control);
  fire on it melts it back to water.
- Your own water bullets flying out through your own Wind Wall freeze into icicles:
  x1.3 speed, x1.6 damage, no soaking, they shatter instead of leaving a puddle.
- Your own fireball flying out through your own Wind Wall grows twice as big (hitbox and
  look; the blast stays the same).
- Enemy projectiles are still thrown back at whoever threw them.

## Player models from old Naruto games

A character ripped with its own game's rig (for now the Clash of Ninja one: `hip`,
`L_harm`, `R_lleg`, `L_finNa01`...) goes in as a whole player model:

```
python3 tools/blend2glm.py Asuma.blend --gla _humanoid.gla --tags jedi_hm.glm \
    --textures <folder with the png> [--drop <duplicate mesh objects>]
python3 tools/glmpreview.py models/players/asuma/model.glm --gla _humanoid.gla \
    --cfg animation.cfg --anims base,BOTH_STAND1,BOTH_RUN1:0.3 --tags
```

The source can also be the rip's own `.smd` (skeleton + weights, no Blender needed for
that part). `--parts` lists the model's connected pieces; `--drop-parts` leaves some out
(Kakashi's book) and `--prop-parts` makes them held props (his kunai). Outline shells (the
inside-out copies the Wii game draws black around legs, arms, pouches, kunai) are found
and left out by themselves (`--keep-shells` keeps them). The rips by the same hand as
Sasuke's (Neji, Lee, Shikamaru, all of Revolution 2) need `--flip-v`. `--drop-held` finds
what is held in the hands and leaves it out: pieces on one hand (or a weapon bone under it)
that stick out of the hand more than 2 units, and everything on a weapon bone under the hand
(grips, claws); flat glove plates stay. Check with `--parts` (marked `<- held`).
Vertex budget: OpenJK transforms a whole model (20 bytes a vertex) into a 256 KB heap for
each Ghoul2 trace and saber mark, the saber included, and drops the game with "Ran out of
transform space for Ghoul2 Models" when it doesn't fit (Kagura, 13.8k vertices, as a shadow
clone). blend2glm slims models over 10.5k vertices by collapsing edges (`tools/glm_slim.py`,
pymeshlab; `--no-slim` keeps them as they are); glm_slim.py also works on a finished glm.
`glmpreview.py --icon icon_default.tga` renders the character screen's head shot. Heights
used (Part I databooks / MyAnimeList, cm): Sasuke 153, Neji 160, Lee 160, Shikamaru 152,
Kabuto 176, Yugao 169, Kisame 195, Anko 167, Baki 188, Gaara 146, Naruto 145, Yamato 178;
the fillers (Kagura, Komachi, Towa, Rogue Ninja) are left at full size. Yamato's rip points
at the wood-clone textures (mky_*); it is built with his own (ymt_*) copied under those
names. Anko's dango skewer pieces inside the hand outline go by --drop-parts. Kisame's Samehada on his back goes
by --drop-parts too (its bone b_186ken). `--rigid-parts PART[:bone]` puts a piece on one
bone so it doesn't bend: Gaara's gourd on `thoracic`.

```
python3 tools/blend2glm.py Kakashi.smd --gla _humanoid.gla --tags jedi_hm.glm \
    --textures Kakashi --parts
```

(`pip install bpy` for Blender's Python.) Ghoul2 skins a .glm against the bind pose
stored in `_humanoid.gla`, so a skeleton of the model's own never reaches the game: the
mesh is bent into the stock pose instead (limbs turned and stretched joint to joint; the
torso mapped by height so the hip and shoulder joints land on the stock ones; neck and
head scaled with the shoulders), its own game's weights are renamed onto the stock bones (spine spread
over pelvis / lumbar / thoracic by height, fingers onto d1 / d2 / d4), and the tag
surfaces (`*r_hand`, `*l_leg_foot`, ...) are copied from `jedi_hm`. The torso, hips and
upper thighs take `jedi_hm`'s own weights instead (the surfaces its skin shows, nearest
point in the bind pose): those were painted for the lumbar / thoracic twist of the stock
animations and the aim turning the torso, a split by height twisted it like a candy
wrapper (`--no-stock-weights` for the old way). `glmpreview.py --twist 70` turns the torso
the way the game does, to look at it. Source bones the table
doesn't know (hair, shoulder pads) go with their nearest known parent. A mesh weighted only
to one finger is a held prop: rigid on the hand, off in `model_default.skin`, on in
`model_props.skin`. In the game: `/model asuma` or `/model asuma/props`. It also writes
`shinobi.char` (`--title "Asuma Sarutobi"`, `--female`): every player model folder with
that file is on the character screen, its `model_*.skin` files are its looks. Pack the
folder into a pk3 in shinobi/ and it shows up - nothing else to register.

## Ninja skeleton (shorter upper arms)

The old Naruto rigs have shorter upper arms than `_humanoid` (Asuma 85 %, Kakashi 78 %,
Sasuke 89 %; forearms, collar bones and legs match within a few %), and a model's bones
are as long as its skeleton's, so they used to be stretched. `tools/make_skeleton.py
_humanoid.gla _humanoid_ninja.gla --upper-arm 0.85` writes a copy of the stock skeleton
with the elbows moved in, every animation carried over (each bone keeps its rotation in
every frame); `blend2glm.py --gla _humanoid_ninja.gla --stock-gla _humanoid.gla` builds
models on it (the tags move with the bones). It lives in
`assets/models/players/_humanoid/_humanoid_ninja.gla` (the game only takes skeletons from
that folder; its animation.cfg is shared), so it goes into `zz_shinobi.pk3` with the rest.
The server still traces sabers on the stock skeleton, so a blade reaches ~1.5 units
further than the shorter arm draws it.

## Package for friends

`tools/package-win64.sh` builds everything and writes `dist/JK_Shinobi-win64.zip`: unpack
it into `Jedi Academy\GameData` and run `JK_Shinobi.bat`. It holds OpenJK
(`tools/openjk-win64`, see its README), the mod's modules and `shinobi/zz_shinobi.pk3`
with all the assets: effects, menus, configs, the player models (Asuma, Kakashi, Sasuke,
all the converted characters, in `assets/models/players/`) and the ninja skeleton. New converted models go into
`assets/models/players/<name>/` (`blend2glm.py --out assets`).

## Player size

`models/players/<model>/scale.cfg` with `scale 85` draws a character at 85 % of the stock
size, and the hitbox (height and width), the eyes, the ghoul2 traces, the bolts and his
clones shrink with him; the feet stay on the floor (50-150 %, read once per model name).
`blend2glm.py --height 153` writes it from the databook height in cm, against Kakashi's
181 cm = the stock size (Sasuke Part I: 153 -> 85 %). Rips come in different axes and
units (Sasuke's .smd is -y up, ~30x smaller, v flipped: `--flip-v`); the converter turns
them upright and to one size from the rig itself, and smooths rips that have one normal
per face.

## Item models

The kunai, explosive kunai, explosive tag, fuma shuriken and the healing / chakra pills
are the artist's models (OBJ from Blender, converted with
`tools/glm2acc.py <model.obj> - <name> --out assets/models/shinobi --game-dir models/shinobi`
plus `--fit` / `--center` / `--turn` / `--zscale`; each .obj material keeps its own
texture). Sizes: kunai 28, explosive kunai 40, shuriken 48 (x2 thicker), tag 12, pills 10.
OBJ faces are turned to the game's winding (clockwise from outside), or the back side would show.

## Dust and mud

Doton leaves loose earth (a sandy patch, 40 s): the Earthquake over its whole area,
Burrow along its trail, broken earth walls / Rock Pillar blocks where they stood.
Water on dust or dust on water makes mud (slows), whichever comes first.

Puddles are a low dome of rippling water, the Water Wave a rolling breaker of the same
water (the game's Bespin water texture when installed, otherwise our own); the wave leaves
pools as wide as itself. The game lists the stock water shaders once
into `shinobi_water_shaders.txt` (in the shinobi folder); `shinobi_puddleShader <name>`
puts one of them on the puddles instead (takes effect on the next map load).

## Weapons (Weapon on the build screen)

No lightsabers: the blades are sabers the game draws without a light blade, glow or
clash flare, with a plain swing trail, the stock sword / tusken staff sounds and a cut
mark instead of the burn; they can't be switched off. The stock saber combat stays
(stances, blocks, kata, parries). The Weapon screen picks how you fight - **Fists only**
(the weapon key does nothing), **One blade**, **Two blades** (a weapon per hand) or a
**Staff** - and the weapon(s); the preview holds them in the stance that goes with them. Stored in the
`shinobi_weapon` userinfo (none / one / two / staff) and the stock `saber1` / `saber2`;
the server takes only `shinobi_*` weapons, fixes what doesn't fit the style and applies
it on the next spawn. Bots get one style each by slot (katana, two knives, staff,
kubikiribocho, fists); shadow clones carry what their maker carries.

| weapon | from | reach (blade length) |
|---|---|---|
| ANBU Katana (`shinobi_katana`) | Itachi's ANBU blade | 40 |
| Trench Knife (`shinobi_knife`) | Asuma's knuckle knives | 40 (as far as the fists) |
| Adamantine Staff (`shinobi_staff`) | Hiruzen's staff, both ends (64 long) | 40 each way from the middle |
| Kurosawa (Tsubaki) (`shinobi_kurosawa`) | Famed Blade Kurosawa (39 long) | 41 (as far as the fists) |
| Kubikiribocho (`shinobi_kubikiribocho`) | Zabuza's Executioner's Blade | 52 |
| Samehada (`shinobi_samehada`) | Kisame's Shark Skin (bandaged) | 49 |

A weapon over 1000 vertices is slimmed by weapon2glm (glm_slim.py; `max_verts`). A
`<name>_l` copy (the trench knife has one) is what the left hand holds.

Only the reach differs between weapons; speed, damage and knockback are the stock ones of
the style (one blade / two / staff). The reach follows the visible blade: the katana keeps
the stock saber's 40, a longer or shorter blade reaches as much farther or shorter as its
tip is from the katana's (about 14 past the tip), but never less than the fists
(`SHINOBI_PUNCH_RANGE`: 40 from a blade starting at the hand, the knife's); the staff
reaches as far as the stock saber staff, 40 each way. weapons.json can also set damage /
speed / knockback / move scales per weapon (none do).

New weapons: an entry in `tools/weapons.json` (source mesh .obj / .dae, axis map so the
blade runs along +Z, the grip point, scale, blade tags and stats), then
`python3 tools/weapon2glm.py tools/weapons.json --src <models> --out assets`. It writes
`models/weapons2/shinobi_<name>/model.glm` (on the engine's one-bone "*default"
skeleton, like the stock hilts: grip at the origin, blade along +Z, `*blade<n>` tags
shaped like saber_1's) and `ext_data/sabers/shinobi_<name>.sab`; it shows up on the
Weapon screen by itself. Check the fit with
`glmpreview.py <player glm> ... --weapons <right glm>[,<left glm>] --tags` (red = the
damage line). Keep a weapon well under 1000 vertices: it shares the 256 KB Ghoul2
transform space with the player model.

## No guns

Players only ever hold fists, their weapon (the saber slot) and the three tool weapons (explosive kunai,
tags, fuma shuriken): spawn loadouts, `give` and anything else are stripped every frame.
Maps spawn only the items that turn into ninja tools (plus CTF flags / cubes and the
Jedi Master saber); armor shards, DEMP2, shield/ammo dispensers, power converters and
emplaced guns are not spawned.

## Ninja build (B)

The build screen replaces the stock profile / force-rank menus: it comes up when you
enter a server (you wait as a spectator until **Join battle**), on **B**, and wherever the
game used to show the force screen. School tabs on top, the techniques of the tab with a
description panel on the left, your 5 slots (order = Q/E cycle, ^ moves up, x drops),
stats and special on the right. **Apply** saves for the next respawn, **Appearance** opens
the stock name/model/saber menu (its force half hidden), **Spectate**, **Close**.
The stock force-rank build is gone: every player gets force jump 3 and saber 3/3,
nothing else (bots still get their pseudo powers for the AI).

- **Techniques**: up to 5, from at most 2 schools (Katon, Suiton, Futon, Doton, Raiton,
  Yin/Yang). Only these are in the Q/E list and can be cast. Shunshin, Kawarimi,
  chakra charging and fists are for everyone.
- **Stats**: 10 points, at most 5 in one: chakra pool (+20 per point), health (+15),
  taijutsu (+10% fist/kick/blade damage), casting speed (bars 8% faster), chakra regen
  (12% faster).
- **Training or a gift**: the special is paid for with stat points - Inner Gates 3,
  Sharingan 4, Curse Seal 4, Byakugan 5. No special = all 10 points, and chakra pool,
  health and taijutsu go up to 7 (casting speed and regen stay at 5: they scale steeply).
  Picking a dearer special takes the extra points back off the end of the list.
- **Special**: one ability. Ready:
  Curse Seal (awakens at 0 chakra and stays awake until it has eaten your health down
  to 10, ~3 health/s; meanwhile chakra returns 5x faster, dark chakra around you and a darkened screen; at 10 health or less it only stirs (HUD says so);
  `/curseseal` or `C` toggles it).
  - **Sharingan** (`C` on/off, 5 chakra + 3/s, no natural regen while open): health bars over enemies in plain sight (a clear line from your eyes - no walls; PVS no longer counts)
    (2000 u), the name of the jutsu they are charging, Kawarimi-hidden enemies within
    1500 u show as ghosts. Clones are marked "clone" instead of a health bar. Red eyes,
    visible to everyone.
  - **Byakugan** (`C` on/off, 5 chakra + 8/s, no natural regen while open; also the Gentle Fist: bare-handed hits burn the target's chakra and burn guards twice as fast): chakra bars and glowing shells of every
    ninja within 1400 u, through walls; clones are hollow grey shells marked "clone".
    Switching it on is heard map-wide. Pale eyes.
  - **Inner Gates** (`C`): 10 s at 1.35x speed and +50% taijutsu, then 15 s exhausted
    (0.75x speed, -30% taijutsu, no chakra regen). Once a minute. Green aura.

Saving stores the build in the `shinobi_build` cvar; it takes effect when you respawn.
With cheats on (devmap) every jutsu can be cast from the console anyway (self-test).

## Items on the maps

The stock pickups are turned into ninja items at load (same item slots, new models,
icons and names; no guns are handed out any more):

| Item | Replaces | Effect |
|---|---|---|
| Healing Pill | medkits, bacta, health dispenser | +30 health at once (big bacta +50); not picked up at full health |
| Chakra Pill | force items, all ammo packs, jetpack, cloak, binoculars, ysalamiri | +30..60 chakra at once; not picked up at full chakra |
| Explosive Kunai x2 (`exkunai`) | thermals, pistols, blaster, stun baton | thrown like a kunai; sticks where it lands (or in whom it hits) and goes off 1 s later: 45 fire damage in 140 u |
| Explosive Tag x2 (`extag`) | trip mines, det packs, seeker, sentry, shield, e-web | stuck on the wall / floor ahead (or under you); armed after 1 s; an enemy within 90 u sets it off 0.4 s later: 55 fire damage in 170 u. Blasts and shots set tags off (chains). Two at a time, 90 s |
| Fuma Shuriken (`fuma`) | rocket launcher, repeater, flechette, concussion, disruptor, bowcaster | one at a time, thrown like a saber: flies out 850 u spinning, turns and homes back to your hand, 45 damage to everyone it cuts (once out, once back). Catch it to throw again; a wall on the way keeps it - walk up to it (anyone without one) to pull it out (30 s) |

The throwables are the stock explosive weapons underneath (thermal detonator = explosive
kunai, trip mine = tag, det pack = shuriken): they show in the stock weapon bar with their
own icons and names, are picked with the weapon keys (`3` cycles them) or the mouse wheel,
and used with attack - or with `Z`, which throws a plain kunai when fists or the blade are
in hand. `Shift + Z` is always the marked kunai. They are held in an empty hand.

Up to 6 of each throwable (one Fuma Shuriken); respawn 20 s (pills) / 30 s (tools). Wind Wall throws
explosive kunai and shuriken back too. Shields (armor) still refill the block guard.

## Clones

Any clone (shadow, water, earth, lightning, the Kawarimi double) hits in melee with 30%
of its owner's strength (the owner's taijutsu stat included).

## Bots

Bots get a random build every life (two random schools, up to five of their jutsu,
random stats and perk; the build goes to games.log), or the one in the
`shinobi_botbuild` cvar, e.g. `set shinobi_botbuild "suiro,kagemane/2,2,2,2,2/none"`.
They also get a spread of force powers they never really use: whenever the stock bot
AI decides to use one, the closest jutsu of its build is cast instead - push: wind /
wave / wind wall / earthquake; pull and grip: water prison / shadow bind / pillar;
lightning: fireball / fire breath / bullets / Chidori / Rasengan / pillar; drain: fire
breath / ash / mist; rage: edges / stone skin / clones (or the Inner Gates); absorb and
protect: stone skin / wind wall / Kawarimi / earth wall / mist; mind trick: Kawarimi /
clones / mist / burrow; speed: Shunshin (backward when hurt) / Hiraishin / burrow;
sight: Sharingan / Byakugan. Charged jutsu are charged like a player's (the Sharingan
reads them) and let go at the bar of the level they want; held jutsu are let go after
1.2-2.8 s (Lightning Dash 0.5-1.1 s). Healing is off.
`shinobi_debug 1` logs each translation.

## Block (right mouse button)

Hold the right mouse button (`+button13`; Ctrl works too) to guard. It replaces the stock
alt-attack on that button: no saber throw (the force setup has none anyway) - attacking with
a blade while holding block does the blade's special attack (the stock kata: standing still,
on the ground, costs chakra); with fists it is the stock grab. Guarding: slow steps, no
jumping, no attacks, no jutsu (raising it lets go of fire breath / shadow bind).

- **With a blade** the guard switches on the stock saber block (the automatic one of the
  base game, otherwise off for players and bots; NPCs keep it): you stand in the ready
  stance, your blade parries toward each swing from the front (a wide arc), the attacker
  bounces off, a strong enough swing crushes the parry. Each blocked swing costs the guard
  what it would have done to you; the swing that empties it gets through and breaks the
  guard. Special attacks (kata, lunges, jump attacks) are unblockable, as stock. The saber
  switches on by itself when you raise the guard.
- **With fists** (and against fists, kicks, thrown things and jutsu for everyone): our
  guard pose; hits from the front (~150 degrees) go into the guard - fists, kicks and blades
  fully, jutsu half (the other half hurts).

The guard is the shield bar (200); an empty guard breaks: 1 s stagger. A full (level 3)
Rasengan or Chidori breaks any guard at once; lower levels are just blocked (half damage, no
knockdown). Perfect parry: a hit within 0.2 s of pressing block does nothing (costs nothing),
and an attacker within reach staggers for 0.8 s (spamming block gives no window). The guard
refills slowly, ~12/s from 1.5 s after the last blocked hit (empty to full ~16 s). With the
Byakugan open (Gentle Fist) fists burn guard twice as fast. Burning, drowning, ash and zones
ignore the guard. The shield no longer soaks damage on its own.

While someone guards, a faint bubble stands around him: blue while the guard is above 2/3,
yellow above 1/3, red below; each blocked hit makes it flare (the stock shield flash, in that
colour).

## Anime outline (test)

`cg_shinobiOutline 1` / `2` / `3` (thin / medium / thick, `0` off - the default): a black
outline around characters, their accessories and the tool in hand. The model is drawn once
more, pushed out along its normals, only its back faces, in black (the "inverted hull").
Not on the map itself; see-through bodies (Kawarimi, fading) get none.

## Sharingan view

While your Sharingan is open: a dark red rim around the view and the eye turning in the top left
corner; everyone within its range (1400 u) and in sight has a thin red rim of chakra
(friends in team games excepted; not through walls - that is the Byakugan); every enemy
player in plain sight shows a thin red line from his eyes along his look (up to 1600 u), with
a ring where it points - you see what he is aiming at.

## Controls (prototype)

Pick a jutsu with **Q / E** (the list is at the bottom of the screen), **hold F** to
charge it: three bars (blue, yellow, red) fill up; releasing casts the jutsu at the
level of the last full bar. While the key is held your character forms hand seals; letting
go before the first bar is full cancels the jutsu (no chakra spent). Higher levels cost more chakra and are
bigger; if you can't afford a level the bar turns red and the cast drops to the best
level you can pay for. Costs and numbers: `codemp/game/bg_shinobi.h`.

| Key | Action |
|---|---|
| **Q / E** | previous / next jutsu |
| hold **F** | charge and cast the selected jutsu |
| hold **R** (stand still) | gather chakra: meditate, +40 chakra/s |
| **Shift + Space** | Shunshin where you look (up and down too); hold Space longer = farther (160 / 360 / 640 units, about 4 / 10 / 18 m; 20/30/40 chakra, 5 s cooldown) |
| **Shift + S + Space** | Shunshin the opposite way |
| **X** | Kawarimi (substitution; not in the Q/E list) |
| **Z** | throw a kunai (10 damage, sticks in walls) |
| **Shift + Z** | throw a marked kunai (Hiraishin seal, 10 chakra, max 3 - a fourth replaces the oldest): stays 30 s; an enemy it hits carries the mark 12 s |
| **2** | fists / your weapon (nothing with Fists only) |
| **B** | ninja build screen |
| W + A/D toward a wall + Space | wall run, also from mid-jump; keep holding W to keep running (6 chakra per stride) |

Shift no longer walks: it is the chakra modifier (more Shift combos later).

| Jutsu | What it does | Chakra Lv1/2/3 |
|---|---|---|
| Rasengan | ball in hand; attack = dash strike, knockdown. Size, damage (40/60/90) and knockback grow with level. Also hits earth walls | 25/40/60 |
| Chidori | lightning in hand; attack = long dash (380/495/620 units) leaving a lightning streak, 50/75/105 damage, electrocutes | 25/40/60 |
| Katon | great fireball, cast at 2 or 3 bars only (letting go earlier cancels): slow and dodgeable, 60+80 / 95+120 damage, blast radius 190/260, leaves the ground burning; flying through ash blows it up | -/30/50 |
| Fire Breath | hold F: a cone of fire (260 units) that follows your aim at a limited turn rate (55°/s, bots and clones too); 20 damage/s, sets people and the ground alight; you walk slowly | 10 + 12/s |
| Ash Cloud | a cloud of hot ash 200 units ahead (radius 110/150/190, 9 s): blinds (the screen goes dark inside), chokes; any fire (fireball, fire breath, burning ground, Chidori) blows it up for 35/50/70 and leaves burning ground; wind blows it away | 20/30/40 |
| Doton | earth wall (F again next to your wall: push it - a wave of earth that shoves people 240/320/400 units, 15/25/35 damage, crushes anyone pinned against a wall for 30/45/60 and a knockdown, then crumbles; 15 chakra): 3/5/7 stone pillars, 35/57/85 shared HP (a Lv1 Rasengan takes 70% of a Lv2 wall); at half HP the 1st, 3rd, 5th... pillars crumble | 15/30/50 |
| Kage Bunshin | 1/2/3 clones in a big smoke cloud, the original lands in a random spot; each clone drains 3 chakra/s | 25/45/70 |
| Kawarimi | an AI double with your looks takes your place and goes for the nearest enemy; you turn invisible to everyone else and can only creep. Any action (attack, jump, use, jutsu) or taking damage reveals you, and the double turns into a log. Hitting the double turns it into a log too. 12 s max, 10 s cooldown | 35 |
| Water Bullets | 1/2/3 fast bullets in a burst (charge level = number of bullets), 12 damage each, small push; each leaves a small puddle, puts out fire, wears fireballs down | 10/18/26 |
| Water Wave | a low wave rolling along the floor (width 140/200/260, 400/550/700 units): 10/18/28 damage, throws people back, soaks, puts out fire and fireballs, leaves puddles. Too high to step over, clear it with a force jump | 20/35/50 |
| Storm Wind | Daitoppa: wind in a cone (320/480/640 units) throws people back; fans burning ground, levels up fireballs, blows steam away. Let go of the key while holding back (S): the gust pulls everyone in the cone to you instead (a short hop that lands them ~64 u in front of you) | 15/25/40 |
| Wind Blade | a wide arc of wind (~110 degrees) sweeping out to 450/600/800 units at chest height: 20/30/45 damage; hard to sidestep, jump over it; walls stop it | 15/25/40 |
| Water Prison | hold F: the enemy within reach in front floats in a ball of water for as long as you hold the key and have chakra: no moving, no attacking, slowly drowns (8/s). You must hold still; releasing, attacking, jumping or getting hit lets go. Only Kawarimi gets the victim out. When it bursts the victim gasps for air for 0.5 s (can't move or attack): a free hit | 20 + 8/s |
| Water Clone | a clone of you made of water (20 s, one hit bursts it into a puddle). It fights like a shadow clone and, on its own, uses the jutsu that comes **after Water Clone in your build list** (Water Prison: it holds the prison for you while you hit the victim; also bullets, wave, winds, Katon, ash, fire breath, shadow bind). Everything it casts costs your chakra. One at a time | 25 |
| Hidden Mist | `kirigakure`: a bank of fog around you, radius 260 / 340 / 420 for 10 / 13 / 16 s. Inside, a fog sheet covers the view; only the mist's maker sees people within 450 u as dark silhouettes (not through walls); puts out burning people. Storm Wind blows it away; the Byakugan sees through it (the Sharingan does not) | 20 / 30 / 40 |
| Earthquake | `jishin`: long charge (bars at 0.7 / 1.7 / 3 s) standing still, radius 220 / 340 / 480. A faint circle shows the area, cracks race out to it (0.5-0.75 s), then everyone standing on the ground inside takes 15 / 30 / 45 and is knocked down (level 1: staggered); anyone in the air is spared. Ignores the guard. Leaves the whole area under dust (40 s); puddles turn to mud, the camera shakes. 5 s cooldown | 20 / 35 / 55 |
| Wind Wall | `kazekabe`: a wall of wind 64 u ahead, 160-240 wide, for 4 / 6 / 8 s. Fireballs, water bullets and kunai flying into its face go back at whoever threw them (they become yours; a fireball grows a level, a marked kunai loses its mark). 6 s cooldown | 15 / 25 / 35 |
| Sky Step | `tenpo`, Futon passive (takes a slot, never cast, skipped by Q/E): jumps and wall runs cost half the chakra, no falling damage | - |
| Wind Edge | `kazeyaiba`, Futon, instant: 15 s of wind on fists and blade - each swing throws an air slash 36-140 u ahead (14 saber / 7 fists), and hits cut through the guard (a perfect parry still stops them) | 20 |
| Lightning Edge | `raiyaiba`, Raiton, instant: 15 s - fist and blade hits +20%, x3 against earth walls (and future stone) | 20 |
| Burrow | `moguragakure`, Doton, hold F: under the ground (5 chakra/s, up to 6 s): invisible (but to the Byakugan), untouchable but for an Earthquake (which throws you out); a dust trail follows you and leaves patches of dust on the floor every 64 u. Let go / attack / run dry: burst out, launching (20 dmg, knockdown) whoever is within 96 u. 8 s cooldown | 15 |
| Rock Pillar | `ganchu`, Doton: a rumble where you look (450-650 u), 0.35 s later a wide stone block (108 x 108; 72 / 112 / 160 high by the charge: jump on it) erupts there, launching whoever is on the spot (20 / 30 / 40, knockdown). Aimed at a wall: a ledge (108 x 108 x 32) sticks out of it instead, shoving people off the wall. Blocks are axis-aligned, stay 90 s, take 50 / 100 / 170 damage (Lightning x3), are not pushable; at most 3 per caster (a 4th takes the oldest down). Broken, they leave dust. 4 s cooldown | 20 / 30 / 40 |
| Stone Skin | `tsuchiyoroi`, Doton, instant: 8 s of -40% damage, no knockdowns or knockback, 0.85x speed; the body is drawn as stone. Lightning Edge hits ignore it. 20 s cooldown | 25 |
| Earth Clone | `tsuchibunshin`, Doton, instant: a slow (0.7x), tough (150) clone of stone (drawn as stone) for 25 s; crumbles into mud. One at a time | 30 |
| Lightning Clone | `raibunshin`, Raiton, instant: a crackling clone (12 s, runs 1.6x) that, like a Kawarimi double, has no leader and heads for the nearest enemy anywhere; within 80 u of it - or when struck down - it goes off: 15 damage and a 1.3 s real paralysis (rooted, no attacks) to everyone within 160 u, puddles electrified. One at a time | 30 |
| Lightning Dash | `raido`, Raiton, hold F: you become a bolt of lightning (body hidden) racing ahead at 3x speed, steered with the mouse, no jumping or attacks; 40 chakra/s, up to 4 s. Whoever you pass within ~48 u takes 10 (no stun; once per 0.8 s); puddles you cross are electrified. 3 s cooldown after | 15 + 40/s |
| Shadow Hide | `kagegakure`, Yin, hold F: melt into your shadow (4 chakra/s, up to 6 s, 0.7x speed): others see only a dark patch (the Byakugan sees you); fists, blades and missiles pass over it, fire and lightning drive you out. Let go or attack to rise. 8 s cooldown | 15 |
| Flash | `senko`, Yang, instant: everyone within 550 u who can see you and looks your way is blinded for 2.5 s (bots and NPCs reel); Kawarimi and Shadow Hide around you are flushed out. 10 s cooldown | 25 |
| Healing | `shosen`, Yang, hold F: chakra turns into health one for one, 20 a second, while you walk slowly; attacking, blocking, full health or an empty pool stops it | 5 + 20/s |
| Shadow Bind | Yin/Yang. Hold F: your shadow crawls 600 units straight ahead along the floor (900 u/s) (not over gaps or through walls). An enemy standing on it is caught and moves the way you move over the ground (you back off - he comes after you), turns and attacks with you while you hold (walk him off a ledge or into fire). Jump over the shadow to dodge; getting hit, running out of chakra or Kawarimi breaks it | 15 + 10/s |
| Hiraishin | Yin/Yang. Teleport to your newest marked kunai; if it is on an enemy, you land behind him facing him. 8 s cooldown | 25 |
| Lightning Trap | Raiton. Needs two or three marked kunai in place (one: it fizzles): a lightning net between them for 8 s - a line or a triangle, each side lit while its ends are within 500 u with nothing solid between. Whoever touches a line - you too (not your clones or team): 20 damage and a 1 s stun (rooted, no attacks; then the usual CC immunity), the same man once per 1.5 s. A kunai in an enemy is a node that moves with him (he is shocked once as the net comes on, never by his own lines). The kunai are used up when the net ends | 20 |
| Shadow Trap | `kagewana`, Yin/Yang. From each of your two newest marked kunai a shadow reaches along the floor (1500 u/s) to the nearest enemy within 420 u (or the one the kunai sits in) and holds him in place: no moving, jumping, attacks, guard or jutsu but Kawarimi. Up to 15 s, ~7 chakra/s per caught enemy; any damage to him (yours too) breaks it, and so does running dry. The kunai is used up | 20 |
| Kaiten | `kaiten`, Yin/Yang, hold F: 0.5 s winding up on the spot (a hit breaks it), then you spin inside a dome of chakra for as long as the key is held and the chakra lasts. No damage gets through (falls, lava, drowning aside), no knockdown, stagger, flinch, bind or prison takes; kunai, shuriken, fire- and waterballs glance off the dome (and become yours); whoever comes within ~96 u is thrown away and knocked down every 0.1 s. Rooted, no attacks, no guard. Bots use it to defend | 10 + 30/s |

### Elements

| Meets | Result |
|---|---|
| water + fire (ground or fireball) | the fire goes out in a cloud of steam (blocks sight) |
| wind + fire | burning ground grows and lasts longer; fireballs level up and speed up |
| wind + steam | steam is blown away |
| Chidori + puddle | the puddle is electrified for 3 s: stuns and hurts whoever stands in it |
| earth wall + puddle | mud: slows everyone in it |
| fireball into lake/river on the map | fizzles out |

Katon leaves burning ground (radius 90/130).

Crowd control can't be chained: after a stun, knockdown, Water Prison or Shadow Bind
ends, the victim is immune to the next one for 2.5 s (damage still lands). Statuses: **burning** (3 damage every
0.5 s, 3 s after leaving the fire; water, steam and map water put it out), **wet** (6 s:
can't catch fire, fire does half damage, lightning 1.5x), **in mud** (half speed).
Earth walls take half damage from fire and a third from water and wind. Puddles slow a little,
mud a lot. A pushed wall smothers burning ground it rolls over.

Quick keys still work: hold **T** Chidori, **V** Doton, **G** Kage Bunshin.
Everyone has Force Jump 2 (wall runs) and fists. Extended melee is on (`g_debugMelee 1`): with fists, right click kicks (direction keys pick the kick, also in the air), both buttons grab; jump at a wall and hold Space to cling to it. No names are shown over enemies,
so clones and doubles can't be told apart from the real ninja by name.
Console: `rasengan 3`, `katon 2`, `shunshin 2 back`, `kawarimi`... cast instantly (used by the self-test).

## Automatic tests

`exec shinobi_autotest` (main menu console): a map with cheats, you as a spectator, two bots
(TestA, TestB) whose AI the tests switch off and drive themselves. Every test prints
`[autotest] PASS|FAIL <name>: <what it measured>` (console, qconsole.log,
shinobi_autotest.txt) and a summary. `shinobi_autotest list` - the tests,
`shinobi_autotest <name> ...` - only those, `shinobi_autotest stop`. Kaiten poses:
`exec shinobi_animtest` spins with each candidate and takes screenshots (ka_*.jpg); the
poses are the cvars `shinobi_kaitenAnim1` (wind-up) / `shinobi_kaitenAnim2` (spin).

## Install / run (Windows)

1. Install Jedi Academy (Steam) and [OpenJK](https://github.com/JACoders/OpenJK/releases) into `GameData`.
2. Copy `dist/shinobi/` to `GameData/shinobi/` and `dist/*.bat` to `GameData/`.
3. `JK_Shinobi_Test.bat` starts a local FFA with bots; `JK_Shinobi.bat` starts the game with the mod
   (borderless window at desktop resolution; the console goes to `qconsole.log`).
4. Two players on one PC: `JK_Shinobi_Host.bat` hosts a map (no bots, no cheats) in a
   1280x720 window, then `JK_Shinobi_Client2.bat` joins it as "Dummy" in a second window with
   its own settings folder (`Documents/My Games/OpenJK_Dummy`). Drag the windows apart and
   switch with Alt+Tab - only the active window gets keyboard and mouse.
5. `JK_Shinobi_SelfTest.bat` casts every jutsu by script, saves screenshots to
   `Documents/My Games/OpenJK/shinobi/screenshots/st*.jpg` and quits.

## Build

Cross-compiles the game modules on Linux with MinGW-w64:

```
sudo apt install mingw-w64 cmake ninja-build zip
python3 tools/gen_textures.py   # regenerate effect and rock textures (optional)
python3 tools/gen_models.py     # regenerate earth wall pillar and log MD3s (optional)
# tools/build-win64.sh also runs tools/gen_effect_levels.py (per-level effect copies)
tools/build-win64.sh            # -> dist/shinobi/{*.dll, zz_shinobi.pk3}
```

## Where things live

- `codemp/game/bg_shinobi.h` - shared flags and tuning constants
- `codemp/game/g_shinobi.c` - server core: media, chakra charging, Rasengan/Chidori, Shunshin, Kawarimi
- `codemp/game/g_shinobi_clones.c` - Kage Bunshin and the Kawarimi double (NPCs, their supervisor, the log)
- `codemp/game/g_shinobi_elements.c` - Katon fireball and Doton earth wall entities
- `codemp/game/bg_pmove.c` (`PM_ShinobiPreMove`, `PM_ShinobiWallRun`) - predicted movement/animation, Shift modifier, wall runs
- `codemp/game/bg_shinobi.c`, `g_shinobi_build.c` - ninja build: parsing/validation, applying stats and perks
- `codemp/cgame/cg_shinobi.c` - client effects, jutsu selection, HUD
- `codemp/ui/ui_shinobi.c` - the build screen (UI module, replaces the profile / force menus)
- `assets/ext_data/NPCs/shinobi.npc` - base NPC definition for clones (model/saber/team overridden in code)
- `assets/` - effects, shaders, textures, configs (packed into `zz_shinobi.pk3`)

## Known limitations

- Clones are NPCs: bots ignore them, and each NPC slot keeps its client/NPC memory for the
  rest of the map (stock OpenJK behaviour), so casting clones hundreds of times on one map
  could exhaust the game's memory pool.
- Clone saber colours fall back to the saber's default colour on other clients.
- Shift is read as the engine's walk button (`cl_run 1`, set by `shinobi_binds.cfg`); with
  `cl_run 0` the modifier would be inverted.
- A hidden (Kawarimi) player is not sent to other clients at all, so they can walk through
  him; he can't be hit by aimed attacks either, only by blasts (which reveal him).
