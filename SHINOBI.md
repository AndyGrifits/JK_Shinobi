# JK_Shinobi

Naruto-style jutsu mod for Star Wars Jedi Knight: Jedi Academy multiplayer, built on [OpenJK](https://github.com/JACoders/OpenJK) (GPLv2).

## Controls (prototype)

| Key | Action |
|---|---|
| hold **E** (stand still) | Gather chakra: meditate, +40 chakra/s |
| **R** | Rasengan: costs 40 chakra, forms in 0.7 s, held for 6 s |
| **Attack** while holding a Rasengan | Dash strike: 60 damage, big knockback and knockdown |

Chakra is the force pool (the force bar on the HUD).

## Install / run (Windows)

1. Install Jedi Academy (Steam) and [OpenJK](https://github.com/JACoders/OpenJK/releases) into `GameData`.
2. Copy `dist/shinobi/` to `GameData/shinobi/` and `dist/*.bat` to `GameData/`.
3. `JK_Shinobi_Test.bat` starts a local FFA with bots; `JK_Shinobi.bat` starts the game with the mod.

## Build

Cross-compiles the game modules on Linux with MinGW-w64:

```
sudo apt install mingw-w64 cmake ninja-build zip
python3 tools/gen_textures.py   # regenerate effect textures (optional)
tools/build-win64.sh            # -> dist/shinobi/{*.dll, zz_shinobi.pk3}
```

## Where things live

- `codemp/game/bg_shinobi.h` - shared flags and tuning constants
- `codemp/game/g_shinobi.c` - server logic (chakra charging, Rasengan)
- `codemp/game/bg_pmove.c` (`PM_ShinobiPreMove`) - predicted movement/animation
- `codemp/cgame/cg_shinobi.c` - client effects and HUD text
- `assets/` - effects, shaders, textures, configs (packed into `zz_shinobi.pk3`)
