#!/bin/bash
# Plain Jedi Academy (no mod): join a Steam friend's game (he runs JA_SteamHost / JA_Host).
cd "$(dirname "$0")" || exit 1
./JK_Shinobi_Steam join base
read -r -p "Нажми Enter, чтобы закрыть."
