#!/usr/bin/env bash
# Build q3map2 (netradiant-custom, Garux) for Linux into tools/maps/q3map2/ - only the compiler, no editor.
# Needs: git, make, g++-14 (the current source does not build with g++ 13), libxml2-dev, libjpeg-dev,
# libpng-dev, zlib1g-dev, libglib2.0-dev.   Then: export Q3MAP2=tools/maps/q3map2/q3map2.x86_64
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${SRC:-/tmp/netradiant-custom}"
[ -d "$SRC" ] || git clone --depth 1 https://github.com/Garux/netradiant-custom.git "$SRC"
make -C "$SRC" -j"$(nproc)" CC=gcc-14 CXX=g++-14 DEPENDENCIES_CHECK=off DOWNLOAD_GAMEPACKS=no \
     ASSIMP_INTERNAL=yes BUILD=release install/q3map2.x86_64
mkdir -p "$HERE/q3map2"
cp "$SRC/install/q3map2.x86_64" "$SRC/install/libassimp_.so" "$HERE/q3map2/"
echo "export Q3MAP2=$HERE/q3map2/q3map2.x86_64"
