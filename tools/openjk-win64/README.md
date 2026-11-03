# OpenJK for the JK_Shinobi package (Windows x86_64)

The engine the mod runs on, put next to the game so a friend's package works as it is:
OpenJK-MP `latest-2-g260c59c2` (win_msvc-x86_64, Sep 29 2026), from
https://github.com/JACoders/OpenJK releases, GPLv2. Only the multiplayer files:

- `openjk.x86_64.exe`, `openjkded.x86_64.exe` (client, dedicated server)
- `rd-vanilla_x86_64.dll`, `rd-rend2_x86_64.dll` (renderers), `SDL2.dll`
- `base/cgamex86_64.dll`, `base/jampgamex86_64.dll`, `base/uix86_64.dll` (stock modules
  for plain JA; the mod brings its own in `shinobi/`)

To update: replace these with the files of a newer OpenJK release.
