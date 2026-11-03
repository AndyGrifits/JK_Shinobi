# OpenJK for the JK_Shinobi package (Windows x86_64)

The engine the mod runs on, put next to the game so a friend's package works as it is:
OpenJK-MP `latest-2-g260c59c2` (win_msvc-x86_64, Sep 29 2026), from
https://github.com/JACoders/OpenJK releases, GPLv2. Only the multiplayer files:

- `openjk.x86_64.exe`, `openjkded.x86_64.exe` (client, dedicated server)
- `rd-vanilla_x86_64.dll`, `rd-rend2_x86_64.dll` (renderers), `SDL2.dll`
- `base/cgamex86_64.dll`, `base/jampgamex86_64.dll`, `base/uix86_64.dll` (stock modules
  for plain JA; the mod brings its own in `shinobi/`)

The package puts our own engine build over the release's exes and vanilla renderer
(`tools/build-win64-engine.sh`, from this repo's OpenJK code with a 1 MB Ghoul2 transform
heap instead of 256 KB: the stock one drops the game on traces against heavy player models,
e.g. Kagura's shadow clone). rend2, SDL2 and the base modules stay the release's.

To update: replace these with the files of a newer OpenJK release.
