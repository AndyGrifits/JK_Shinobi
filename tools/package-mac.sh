#!/usr/bin/env bash
# JK_Shinobi for a friend on a Mac (Apple Silicon): dist/JK_Shinobi-mac-kit.tar holds everything
# but the game's own files - the official OpenJK app, the mod's arm64 modules, pk3 and maps, the
# Steam link and the .command launchers - with their Unix permissions. On the Windows PC that
# has the game, JK_Shinobi_MakeMacKit.bat (next to the tar in GameData) adds base/assets0-3.pk3
# and writes JK_Shinobi-mac.zip to send (Windows' own tar keeps the permissions from the tar).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/tools/build-win64.sh" >/dev/null           # the pk3
"$ROOT/tools/build-macos-arm64.sh" >/dev/null
"$ROOT/tools/steamlink/build.sh" >/dev/null

STAGE="$ROOT/dist/package-mac"
G="$STAGE/JK_Shinobi"
OUT="$ROOT/dist/JK_Shinobi-mac-kit.tar"
rm -rf "$STAGE" "$OUT"
mkdir -p "$G/shinobi"
tar xzf "$ROOT/tools/openjk-macos-arm64/OpenJK-macos-arm64.tar.gz" -C "$G" openjk.arm64.app
# the bundled libSDL2 is sdl2-compat: it loads SDL3 at run time, which the official build leaves
# out ("Failed loading SDL3 library" on a Mac without Homebrew's SDL3) - it looks next to itself
cp "$ROOT/tools/openjk-macos-arm64/libSDL3.dylib" "$G/openjk.arm64.app/Contents/Frameworks/"
cp "$ROOT"/dist/shinobi-mac/*.dylib "$ROOT/dist/shinobi/zz_shinobi.pk3" "$G/shinobi/"
if ls "$ROOT"/dist/maps/shinobi_map_*.pk3 >/dev/null 2>&1; then
  cp "$ROOT"/dist/maps/shinobi_map_*.pk3 "$G/shinobi/"
else
  echo "warning: no maps in dist/maps/ (shinobi_map_*.pk3)" >&2
fi
cp "$ROOT"/dist/steamlink-mac/JK_Shinobi_Steam "$ROOT"/dist/steamlink-mac/libsteam_api.dylib "$G/"
cp "$ROOT"/tools/launchers-mac/*.command "$ROOT"/tools/launchers-mac/README.txt "$G/"
chmod 755 "$G"/*.command "$G/JK_Shinobi_Steam"
# no base/ entry: the .bat adds JK_Shinobi/base/assets*.pk3
(cd "$STAGE" && tar --owner=0 --group=0 --numeric-owner -cf "$OUT" JK_Shinobi)
echo "$OUT: $(du -h "$OUT" | cut -f1)"
