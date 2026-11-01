# JK_Shinobi

Naruto-style jutsu mod for Star Wars Jedi Knight: Jedi Academy multiplayer, built on [OpenJK](https://github.com/JACoders/OpenJK) (GPLv2).

## Controls (prototype)

Jutsu keys are **held**: three bars (blue, yellow, red) fill up; releasing casts the
jutsu at the level of the last full bar (a tap = level 1). Higher levels cost more
chakra and are bigger; if you can't afford a level the bar turns red and the cast
drops to the best level you can pay for. Costs and numbers: `codemp/game/bg_shinobi.h`.

| Key | Action | Chakra Lv1/2/3 |
|---|---|---|
| hold **E** (stand still) | Gather chakra: meditate, +40 chakra/s | |
| hold **R** | Rasengan in hand; attack = dash strike, knockdown. Size, damage (40/60/90) and knockback grow with level | 25/40/60 |
| hold **T** | Chidori in hand; attack = long dash (380/495/620 units) leaving a lightning streak, 50/75/105 damage, electrocutes | 25/40/60 |
| hold **F** | Great fireball: dense ball, bigger/slower/stronger per level (25+40 / 45+65 / 70+100 dmg, blast radius 130/180/250) | 15/30/50 |
| hold **V** | Earth wall: 3/5/7 stone pillars, 250/450/700 shared HP; at half HP the 1st, 3rd, 5th... pillars crumble | 15/30/50 |
| hold **G** | Kage Bunshin: 1/2/3 clones in a big smoke cloud, the original lands in a random spot; each clone drains 3 chakra/s | 25/45/70 |
| **Q** | Shunshin (body flicker): teleport up to 320 units where you look, 0.6 s cooldown | 15 |

Everyone has Force Jump 2, so wall runs work for all (W + strafe toward a wall + jump).
Console: `rasengan 3`, `katon 2`... cast instantly at a level (used by the self-test).

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
python3 tools/gen_models.py     # regenerate earth wall pillar MD3s (optional)
# tools/build-win64.sh also runs tools/gen_effect_levels.py (per-level effect copies)
tools/build-win64.sh            # -> dist/shinobi/{*.dll, zz_shinobi.pk3}
```

## Where things live

- `codemp/game/bg_shinobi.h` - shared flags and tuning constants
- `codemp/game/g_shinobi.c` - server core: media, chakra charging, Rasengan/Chidori, Shunshin
- `codemp/game/g_shinobi_clones.c` - Kage Bunshin (shadow clone NPCs and their supervisor)
- `codemp/game/g_shinobi_elements.c` - Katon fireball and Doton earth wall entities
- `codemp/game/bg_pmove.c` (`PM_ShinobiPreMove`) - predicted movement/animation
- `codemp/cgame/cg_shinobi.c` - client effects and HUD text
- `assets/ext_data/NPCs/shinobi.npc` - base NPC definition for clones (model/saber/team overridden in code)
- `assets/` - effects, shaders, textures, configs (packed into `zz_shinobi.pk3`)

## Known limitations

- Clones are NPCs: bots ignore them, and each NPC slot keeps its client/NPC memory for the
  rest of the map (stock OpenJK behaviour), so casting clones hundreds of times on one map
  could exhaust the game's memory pool.
- Clone saber colours fall back to the saber's default colour on other clients.
