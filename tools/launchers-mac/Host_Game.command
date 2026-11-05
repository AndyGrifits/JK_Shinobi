#!/bin/bash
# JK_Shinobi: host a game for Steam friends (they run Join). Steam must be running.
cd "$(dirname "$0")" || exit 1
./JK_Shinobi_Steam host
read -r -p "Нажми Enter, чтобы закрыть."
