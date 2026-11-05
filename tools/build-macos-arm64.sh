#!/usr/bin/env bash
# Build the JK_Shinobi game modules (jampgame, cgame, ui) for macOS arm64 (Apple Silicon) from
# Linux, with zig as the cross compiler (pip install ziglang), into dist/shinobi-mac/.
# The engine is the official OpenJK app (tools/openjk-macos-arm64).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-macos-arm64"
DIST="$ROOT/dist/shinobi-mac"
python3 -m ziglang version >/dev/null 2>&1 || pip install --break-system-packages -q ziglang

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/Toolchains/macos-arm64-zig.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DMakeApplicationBundles=OFF \
  -DUseInternalZlib=ON -DUseInternalPNG=ON -DUseInternalJPEG=ON \
  -DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPDed=OFF -DBuildMPRend2=OFF \
  -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF \
  -DBuildMPGame=ON -DBuildMPCGame=ON -DBuildMPUI=ON >/dev/null
ninja -C "$BUILD" jampgamearm64 cgamearm64 uiarm64

mkdir -p "$DIST"
cp "$BUILD/codemp/game/jampgamearm64.dylib" "$BUILD/codemp/cgame/cgamearm64.dylib" "$BUILD/codemp/ui/uiarm64.dylib" "$DIST/"
ls -l "$DIST"
