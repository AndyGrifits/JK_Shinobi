#!/usr/bin/env bash
# JK_Shinobi: builds JK_Shinobi_Steam.exe (the Steam link, see jks_steamlink.cpp) with MinGW.
# The Steamworks SDK headers and steam_api64.dll come from Steamworks.NET (pinned commit) -
# fetched once into tools/steamlink/sdk/ (not in git).
#   tools/steamlink/build.sh   -> dist/steamlink/{JK_Shinobi_Steam.exe, steam_api64.dll}
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SDK="$HERE/sdk"
REV=ea51b029a5dbcb47a99d3c1fac930b811672ed32
if [ ! -f "$SDK/CodeGen/steam/steam_api_flat.h" ]; then
  rm -rf "$SDK"
  git clone -q https://github.com/rlabrecque/Steamworks.NET.git "$SDK"
  git -C "$SDK" checkout -q "$REV"
fi
INC="$SDK/CodeGen"     # (the headers include each other as steam/...)
DLL="$SDK/com.rlabrecque.steamworks.net/Plugins/steam_api64.dll"
OUT="$ROOT/dist/steamlink"
mkdir -p "$OUT"
x86_64-w64-mingw32-g++ -O2 -std=c++17 -Wall -Wno-unknown-pragmas -I"$INC" -I"$INC/steam" \
  "$HERE/jks_steamlink.cpp" "$DLL" -o "$OUT/JK_Shinobi_Steam.exe" \
  -static -static-libgcc -static-libstdc++ -lws2_32
cp "$DLL" "$OUT/"
echo "$OUT/JK_Shinobi_Steam.exe"
