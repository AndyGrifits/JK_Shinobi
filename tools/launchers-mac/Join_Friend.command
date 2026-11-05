#!/bin/bash
# JK_Shinobi: join a Steam friend's game (he runs Host). Steam must be running.
cd "$(dirname "$0")" || exit 1
./JK_Shinobi_Steam join
read -r -p "Нажми Enter, чтобы закрыть."
