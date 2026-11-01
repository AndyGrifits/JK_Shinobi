# JK_Shinobi

Naruto-style jutsu mod for Star Wars Jedi Knight: Jedi Academy multiplayer, built on [OpenJK](https://github.com/JACoders/OpenJK) (GPLv2).

## Controls (prototype)

Pick a jutsu with **Q / E** (the list is at the bottom of the screen), **hold F** to
charge it: three bars (blue, yellow, red) fill up; releasing casts the jutsu at the
level of the last full bar (a tap = level 1). Higher levels cost more chakra and are
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
| **2** | fists / saber |
| W + A/D toward a wall + Space | wall run, also from mid-jump; keep holding W to keep running (6 chakra per stride) |

Shift no longer walks: it is the chakra modifier (more Shift combos later).

| Jutsu | What it does | Chakra Lv1/2/3 |
|---|---|---|
| Rasengan | ball in hand; attack = dash strike, knockdown. Size, damage (40/60/90) and knockback grow with level. Also hits earth walls | 25/40/60 |
| Chidori | lightning in hand; attack = long dash (380/495/620 units) leaving a lightning streak, 50/75/105 damage, electrocutes | 25/40/60 |
| Katon | great fireball blasted with both hands: bigger/slower/stronger per level (25+40 / 45+65 / 70+100 dmg, blast radius 130/180/250), loud explosion and camera shake | 15/30/50 |
| Doton | earth wall: 3/5/7 stone pillars, 35/57/85 shared HP (a Lv1 Rasengan takes 70% of a Lv2 wall); at half HP the 1st, 3rd, 5th... pillars crumble | 15/30/50 |
| Kage Bunshin | 1/2/3 clones in a big smoke cloud, the original lands in a random spot; each clone drains 3 chakra/s | 25/45/70 |
| Kawarimi | an AI double with your looks takes your place and goes for the nearest enemy; you turn invisible to everyone else and can only creep. Any action (attack, jump, use, jutsu) or taking damage reveals you, and the double turns into a log. Hitting the double turns it into a log too. 12 s max, 10 s cooldown | 35 |
| Suiton | water bullet: 15/25/40 damage and a push; leaves a puddle (radius 70/100/140); puts out fire, cancels fireballs (the stronger one goes on, weakened) | 15/30/50 |
| Futon | Daitoppa: wind gust in a cone (320/480/640 units), throws people back; fans burning ground (bigger, longer), levels up fireballs in flight, blows steam away | 15/25/40 |
| Water Prison | hold F: the enemy within reach in front floats in a ball of water for as long as you hold the key and have chakra: no moving, no attacking, slowly drowns (8/s). You must hold still; releasing, attacking, jumping or getting hit lets go. Only Kawarimi gets the victim out | 20 + 8/s |

### Elements

| Meets | Result |
|---|---|
| water + fire (ground or fireball) | the fire goes out in a cloud of steam (blocks sight) |
| wind + fire | burning ground grows and lasts longer; fireballs level up and speed up |
| wind + steam | steam is blown away |
| Chidori + puddle | the puddle is electrified for 3 s: stuns and hurts whoever stands in it |
| earth wall + puddle | mud: slows everyone in it |
| fireball into lake/river on the map | fizzles out |

Katon Lv2/3 leaves burning ground (radius 90/130). Statuses: **burning** (3 damage every
0.5 s, 3 s after leaving the fire; water, steam and map water put it out), **wet** (6 s:
can't catch fire, fire does half damage, lightning 1.5x), **in mud** (half speed).
Earth walls take half damage from fire and a third from water and wind.

Quick keys still work: hold **T** Chidori, **V** Doton, **G** Kage Bunshin.
Everyone has Force Jump 2 (wall runs) and fists. Extended melee is on (`g_debugMelee 1`): with fists, right click kicks (direction keys pick the kick, also in the air), both buttons grab; jump at a wall and hold Space to cling to it. No names are shown over enemies,
so clones and doubles can't be told apart from the real ninja by name.
Console: `rasengan 3`, `katon 2`, `shunshin 2 back`, `kawarimi`... cast instantly (used by the self-test).

## Install / run (Windows)

1. Install Jedi Academy (Steam) and [OpenJK](https://github.com/JACoders/OpenJK/releases) into `GameData`.
2. Copy `dist/shinobi/` to `GameData/shinobi/` and `dist/*.bat` to `GameData/`.
3. `JK_Shinobi_Test.bat` starts a local FFA with bots; `JK_Shinobi.bat` starts the game with the mod
   (borderless window at desktop resolution; the console goes to `qconsole.log`).
4. `JK_Shinobi_SelfTest.bat` casts every jutsu by script, saves screenshots to
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
- `codemp/cgame/cg_shinobi.c` - client effects, jutsu selection and HUD
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
