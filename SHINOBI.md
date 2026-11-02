# JK_Shinobi

Naruto-style jutsu mod for Star Wars Jedi Knight: Jedi Academy multiplayer, built on [OpenJK](https://github.com/JACoders/OpenJK) (GPLv2).

## Character screen (Appearance)

**Appearance** on the build screen opens our own character screen instead of the stock
profile menu: a live 3D preview (drag to turn it), the base (Human Male `jedi_hm` /
Human Female `jedi_hf`) with all its head / torso / legs skins, the cloth colour (RGB),
and up to 4 accessories, each with its own fit: size (40-250%), forward / left / up
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

## Dust and mud

Doton leaves loose earth (a sandy patch, 20 s): the Earthquake over its whole area,
Burrow along its trail, broken earth walls / Rock Pillar blocks where they stood.
Water on dust or dust on water makes mud (slows), whichever comes first.

Puddles are a low dome of rippling water, the Water Wave a rolling breaker of the same
water (the game's Bespin water texture when installed, otherwise our own); the wave leaves
pools as wide as itself. The game lists the stock water shaders once
into `shinobi_water_shaders.txt` (in the shinobi folder); `shinobi_puddleShader <name>`
puts one of them on the puddles instead (takes effect on the next map load).

## No guns

Players only ever hold fists, the saber and the three tool weapons (explosive kunai,
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
- **Special**: one ability. Ready:
  Curse Seal (awakens at 0 chakra and stays awake until it has eaten your health down
  to 10, ~3 health/s; meanwhile chakra returns 5x faster, dark chakra around you and a darkened screen; at 10 health or less it only stirs (HUD says so);
  `/curseseal` or `C` toggles it).
  - **Sharingan** (`C` on/off, 5 chakra + 3/s, no natural regen while open): health bars over enemies in sight
    (2000 u), the name of the jutsu they are charging, Kawarimi-hidden enemies within
    1500 u show as ghosts. Clones look like perfect copies (owner's health). Red eyes,
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
| Fuma Shuriken x2 (`fuma`) | rocket launcher, repeater, flechette, concussion, disruptor, bowcaster | a big spinning star flying straight: 45 damage and a knock; sticks in walls |

The throwables are the stock explosive weapons underneath (thermal detonator = explosive
kunai, trip mine = tag, det pack = shuriken): they show in the stock weapon bar with their
own icons and names, are picked with the weapon keys (`3` cycles them) or the mouse wheel,
and used with attack - or with `Z`, which throws a plain kunai when fists or the blade are
in hand. `Shift + Z` is always the marked kunai. They are held in an empty hand.

Up to 6 of each throwable; respawn 20 s (pills) / 30 s (tools). Wind Wall throws
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

Hold the right mouse button (`+button13`; Ctrl works too) for a guard stance. It replaces
the stock alt-attack on that button: no saber throw (the force setup has none anyway) and no
attack+alt-attack special moves (katas). Guard stance: slow steps, no jumping, no attacks, no
jutsu (raising it lets go of fire breath / shadow bind). Hits from the front (~150°)
go into the guard - the shield bar (100) - instead of health: fists, kicks and blades
fully, jutsu half (the other half hurts). An empty guard breaks: 1 s stagger. A full
(level 3) Rasengan or Chidori breaks any guard at once; lower levels are just blocked
(half damage, no knockdown). Perfect parry: a hit within 0.2 s of pressing block does
nothing, and an attacker within reach staggers for 0.8 s (spamming block gives no
window). The guard refills ~12/s from 1.5 s after the last blocked hit. With the Byakugan open
(Gentle Fist) fists burn guard twice as fast. Burning, drowning, ash and zones ignore the guard. The
shield no longer soaks damage on its own, and the automatic saber block is gone.

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
| **Shift + Z** | throw a marked kunai (Hiraishin seal, 10 chakra, max 2): stays 30 s; an enemy it hits carries the mark 12 s |
| **2** | fists / saber |
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
| Storm Wind | Daitoppa: wind in a cone (320/480/640 units) throws people back; fans burning ground, levels up fireballs, blows steam away | 15/25/40 |
| Wind Blade | a wide arc of wind (~110 degrees) sweeping out to 450/600/800 units at chest height: 20/30/45 damage; hard to sidestep, jump over it; walls stop it | 15/25/40 |
| Water Prison | hold F: the enemy within reach in front floats in a ball of water for as long as you hold the key and have chakra: no moving, no attacking, slowly drowns (8/s). You must hold still; releasing, attacking, jumping or getting hit lets go. Only Kawarimi gets the victim out. When it bursts the victim gasps for air for 0.5 s (can't move or attack): a free hit | 20 + 8/s |
| Water Clone | a clone of you made of water (20 s, one hit bursts it into a puddle). It fights like a shadow clone and, on its own, uses the jutsu that comes **after Water Clone in your build list** (Water Prison: it holds the prison for you while you hit the victim; also bullets, wave, winds, Katon, ash, fire breath, shadow bind). Everything it casts costs your chakra. One at a time | 25 |
| Hidden Mist | `kirigakure`: a bank of fog around you, radius 260 / 340 / 420 for 10 / 13 / 16 s. Inside, a fog sheet covers the view; only the mist's maker sees people within 450 u as dark silhouettes (not through walls); puts out burning people. Storm Wind blows it away; the Byakugan sees through it (the Sharingan does not) | 20 / 30 / 40 |
| Earthquake | `jishin`: long charge (bars at 0.7 / 1.7 / 3 s), radius 220 / 340 / 480. A faint circle shows the area, cracks race out to it (0.5-0.75 s), then everyone standing on the ground inside takes 15 / 30 / 45 and is knocked down (level 1: staggered); anyone in the air is spared. Ignores the guard. Leaves the whole area under dust (20 s); puddles turn to mud, the camera shakes. 5 s cooldown | 20 / 35 / 55 |
| Wind Wall | `kazekabe`: a wall of wind 64 u ahead, 160-240 wide, for 2 / 3 / 4 s. Fireballs, water bullets and kunai flying into its face go back at whoever threw them (they become yours; a fireball grows a level, a marked kunai loses its mark). 6 s cooldown | 15 / 25 / 35 |
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
| Healing | `shosen`, Yang: 20 / 35 / 50 health back. 10 s cooldown | 25 / 40 / 55 |
| Shadow Bind | Yin/Yang. Hold F: your shadow crawls 600 units straight ahead along the floor (not over gaps or through walls). An enemy standing on it is caught and copies your moves, jumps, turns and attacks while you hold (walk him off a ledge or into fire). Jump over the shadow to dodge; getting hit, running out of chakra or Kawarimi breaks it | 15 + 10/s |
| Hiraishin | Yin/Yang. Teleport to your newest marked kunai; if it is on an enemy, you land behind him facing him. 8 s cooldown | 25 |
| Lightning Trap | Raiton. Every marked kunai of yours discharges: 25 damage and a 0.8 s stun within 110 units (electrifies puddles, sets off ash) | 20 |

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
