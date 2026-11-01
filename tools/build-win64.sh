#!/usr/bin/env bash
# Build the JK_Shinobi game modules (jampgame, cgame, ui) for Windows x86_64
# from Linux using MinGW-w64, and pack them into dist/shinobi/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-win64"
DIST="$ROOT/dist/shinobi"

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/Toolchains/mingw64-cross.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPDed=OFF -DBuildMPRend2=OFF \
  -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF \
  -DBuildMPGame=ON -DBuildMPCGame=ON -DBuildMPUI=ON >/dev/null
ninja -C "$BUILD"

mkdir -p "$DIST"
for m in jampgame cgame ui; do
  cp "$BUILD/${m}x86_64.dll" "$DIST/"
done

# Per-level effect copies are generated from tools/effect_templates.
python3 "$ROOT/tools/gen_effect_levels.py" >/dev/null

# Assets (effects, shaders, menus) live in assets/ and are zipped into a pk3.
if [ -d "$ROOT/assets" ] && [ -n "$(ls -A "$ROOT/assets")" ]; then
  rm -f "$DIST/zz_shinobi.pk3"
  (cd "$ROOT/assets" && zip -qr "$DIST/zz_shinobi.pk3" .)
fi
cp "$ROOT"/tools/launchers/*.bat "$ROOT/dist/"
ls -l "$DIST"
