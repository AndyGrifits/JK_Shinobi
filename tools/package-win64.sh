#!/usr/bin/env bash
# JK_Shinobi: the whole thing for a friend, in one zip: unpack it into
# "Jedi Academy\GameData" (the Steam / CD game is needed for its assets) and run
# JK_Shinobi.bat. Holds OpenJK (tools/openjk-win64), the mod's modules and one pk3 with
# everything else (effects, menus, configs, player models, the ninja skeleton).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/tools/build-win64.sh" >/dev/null

PKG="$ROOT/dist/package"
OUT="$ROOT/dist/JK_Shinobi-win64.zip"
rm -rf "$PKG" "$OUT"
mkdir -p "$PKG/GameData/shinobi" "$PKG/GameData/base"
cp "$ROOT"/tools/openjk-win64/*.exe "$ROOT"/tools/openjk-win64/*.dll "$PKG/GameData/"
cp "$ROOT"/tools/openjk-win64/base/*.dll "$PKG/GameData/base/"
cp "$ROOT"/dist/shinobi/* "$PKG/GameData/shinobi/"
# the maps (built elsewhere: shinobi_map_*.pk3) - put them into dist/maps/ before packing
if ls "$ROOT"/dist/maps/shinobi_map_*.pk3 >/dev/null 2>&1; then
  cp "$ROOT"/dist/maps/shinobi_map_*.pk3 "$PKG/GameData/shinobi/"
else
  echo "warning: no maps in dist/maps/ (shinobi_map_*.pk3) - the package has only the stock ones" >&2
fi
cp "$ROOT"/tools/launchers/*.bat "$ROOT"/tools/launchers/*.ps1 "$PKG/GameData/"
"$ROOT/tools/steamlink/build.sh" >/dev/null
cp "$ROOT"/dist/steamlink/JK_Shinobi_Steam.exe "$ROOT"/dist/steamlink/steam_api64.dll "$PKG/GameData/"
cat > "$PKG/GameData/JK_Shinobi_README.txt" <<'TXT'
JK_Shinobi - Naruto mod for Jedi Academy (multiplayer)

1. You need Star Wars Jedi Knight: Jedi Academy (Steam or CD).
2. Unpack this archive into the game's GameData folder, e.g.
   C:\Program Files (x86)\Steam\steamapps\common\Jedi Academy\GameData
   (it adds OpenJK, the base modules and the shinobi folder; nothing of the game is replaced
   except the stock modules in base, by OpenJK's).
3. Start JK_Shinobi.bat to play alone (with bots: JK_Shinobi_Test.bat).
4. Together over Steam (easiest - nothing to set up): both run Steam and are Steam friends.
   The host starts JK_Shinobi_SteamHost.bat (the game opens on Create Game: pick a map,
   Start); the other starts JK_Shinobi_SteamJoin.bat - it finds the host and connects.
   Keep the black windows open while playing. Your Steam status shows "Spacewar"
   (Valve's test app the link runs under).
5. Or directly: the host starts JK_Shinobi_Host.bat - full screen, the Create Game screen
   (choose the map, Start); its black window shows his addresses. The others start
   JK_Shinobi_Join.bat and type the host's address (or nothing: the server list).
   Over the internet both need the same VPN (Radmin VPN / ZeroTier / Tailscale - use the
   host's VPN address), or the host forwards UDP port 29070 on his router.
   Allow OpenJK through the Windows firewall when it asks.

Characters: Appearance on the build screen, or /model <name> (the folder names in
models/players of shinobi/zz_shinobi.pk3, e.g. /model kakashi).
Log: Documents\My Games\OpenJK\shinobi\qconsole.log
TXT
(cd "$PKG" && zip -qr -X "$OUT" GameData)
echo "$OUT: $(du -h "$OUT" | cut -f1)"
unzip -l "$OUT" | tail -1
