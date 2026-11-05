#!/bin/bash
# JK_Shinobi: just the game (no Steam link). Console log:
# ~/Library/Application Support/OpenJK/shinobi/qconsole.log
cd "$(dirname "$0")" || exit 1
./openjk.arm64.app/Contents/MacOS/openjk.arm64 +set fs_game shinobi +set logfile 2 +set r_fullscreen 1 +set r_mode -2 +exec shinobi_binds.cfg "$@" >/dev/null 2>&1 &
