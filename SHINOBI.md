# JK_Shinobi

Naruto-style jutsu mod for Star Wars Jedi Knight: Jedi Academy multiplayer, built on [OpenJK](https://github.com/JACoders/OpenJK) (GPLv2).

## Controls (prototype)

| Key | Action |
|---|---|
| hold **E** (stand still) | Gather chakra: meditate, +40 chakra/s |
| **R** | Rasengan: costs 40 chakra, forms in 0.7 s, held for 6 s |
| **Attack** while holding a Rasengan | Dash strike: 60 damage, big knockback and knockdown |
| **T** | Chidori: 35 chakra, forms in 0.5 s; attack = faster dash, 75 damage, electrocutes (short stun) instead of knocking down |
| **F** | Katon: great fireball, 30 chakra, 0.9 s cooldown; explodes on impact (35 direct + 55 splash, radius 170), never hurts the caster |
| **V** | Doton: earth wall, 30 chakra, 3 s cooldown; up to 5 stone pillars rise 96 units ahead, 150 HP each, sink after 12 s. Pillars never spawn on top of a player |
| **Q** | Shunshin (body flicker): teleport up to 320 units where you look, 15 chakra, 0.6 s cooldown |
| **G** | Kage Bunshin: two shadow clones (50 chakra, 25 s). They copy your model and saber, follow you, attack the nearest enemy with the Jedi AI, vanish after one hit; their kills count as yours |

Chakra is the force pool (the force bar on the HUD).

## Install / run (Windows)

1. Install Jedi Academy (Steam) and [OpenJK](https://github.com/JACoders/OpenJK/releases) into `GameData`.
2. Copy `dist/shinobi/` to `GameData/shinobi/` and `dist/*.bat` to `GameData/`.
3. `JK_Shinobi_Test.bat` starts a local FFA with bots; `JK_Shinobi.bat` starts the game with the mod.

## Build

Cross-compiles the game modules on Linux with MinGW-w64:

```
sudo apt install mingw-w64 cmake ninja-build zip
python3 tools/gen_textures.py   # regenerate effect and rock textures (optional)
python3 tools/gen_models.py     # regenerate earth wall pillar MD3s (optional)
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
