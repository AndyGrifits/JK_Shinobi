#!/usr/bin/env bash
# JK_Shinobi: the whole thing for a friend, in one zip: unpack it into
# "Jedi Academy\GameData" (the Steam / CD game is needed for its assets) and run
# JK_Shinobi.bat. Holds OpenJK (tools/openjk-win64, with the engine built from this repo over it:
# tools/build-win64-engine.sh), the mod's modules and one pk3 with
# everything else (effects, menus, configs, player models, the ninja skeleton).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/tools/build-win64.sh" >/dev/null
"$ROOT/tools/build-win64-engine.sh" >/dev/null

PKG="$ROOT/dist/package"
OUT="$ROOT/dist/JK_Shinobi-win64.zip"
rm -rf "$PKG" "$OUT"
mkdir -p "$PKG/GameData/shinobi" "$PKG/GameData/base"
cp "$ROOT"/tools/openjk-win64/*.exe "$ROOT"/tools/openjk-win64/*.dll "$PKG/GameData/"
cp "$ROOT"/dist/engine/* "$PKG/GameData/"         # our engine build over the release's
cp "$ROOT"/tools/openjk-win64/base/*.dll "$PKG/GameData/base/"
cp "$ROOT"/dist/shinobi/* "$PKG/GameData/shinobi/"
cp "$ROOT"/tools/launchers/*.bat "$PKG/GameData/"
cat > "$PKG/GameData/JK_Shinobi_README.txt" <<'TXT'
JK_Shinobi - Naruto mod for Jedi Academy (multiplayer)

1. You need Star Wars Jedi Knight: Jedi Academy (Steam or CD).
2. Unpack this archive into the game's GameData folder, e.g.
   C:\Program Files (x86)\Steam\steamapps\common\Jedi Academy\GameData
   (it adds OpenJK, the base modules and the shinobi folder; nothing of the game is replaced
   except the stock modules in base, by OpenJK's).
3. Start JK_Shinobi.bat. To play together: one starts JK_Shinobi_Host.bat, the others
   connect to him (console: /connect <his address>).

Characters: Appearance on the build screen (12 of them, two pages), or /model <name>:
asuma, kakashi, sasuke, neji, lee, shikamaru, kabuto, yugao, kagura, komachi, towa, rogue_ninja.
Log: Documents\My Games\OpenJK\shinobi\qconsole.log
TXT
(cd "$PKG" && zip -qr -X "$OUT" GameData)
echo "$OUT: $(du -h "$OUT" | cut -f1)"
unzip -l "$OUT" | tail -1
