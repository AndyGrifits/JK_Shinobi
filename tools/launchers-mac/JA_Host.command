#!/bin/bash
# Plain Jedi Academy (no mod): host a game for Steam friends (they run JA_SteamJoin / JA_Join).
cd "$(dirname "$0")" || exit 1
./JK_Shinobi_Steam host base
read -r -p "Нажми Enter, чтобы закрыть."
