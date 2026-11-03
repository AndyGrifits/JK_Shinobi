#!/usr/bin/env bash
# Build the OpenJK multiplayer engine (client, dedicated server, vanilla renderer) for
# Windows x86_64 from Linux with MinGW-w64, into dist/engine/. JK_Shinobi needs its own
# engine for one change: a 1 MB Ghoul2 transform heap instead of 256 KB
# (G2_VERT_SPACE_*_SIZE in codemp/server/sv_init.cpp and codemp/client/cl_main.cpp); with
# the stock one a trace on a heavy player model (Kagura's shadow clone) drops the game with
# "Ran out of transform space for Ghoul2 Models".
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-win64-engine"
DIST="$ROOT/dist/engine"

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/Toolchains/mingw64-cross.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBuildMPEngine=ON -DBuildMPRdVanilla=ON -DBuildMPDed=ON -DBuildMPRend2=OFF \
  -DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF \
  -DBuildMPGame=OFF -DBuildMPCGame=OFF -DBuildMPUI=OFF >/dev/null
ninja -C "$BUILD"
mkdir -p "$DIST"
cp "$BUILD"/openjk.x86_64.exe "$BUILD"/openjkded.x86_64.exe "$BUILD"/rd-vanilla_x86_64.dll "$DIST/"
ls -l "$DIST"
