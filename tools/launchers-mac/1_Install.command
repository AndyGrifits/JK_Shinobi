#!/bin/bash
# JK_Shinobi for Mac (Apple Silicon): run once after unpacking.
# The game and the mod are not signed by Apple, so macOS blocks them as "downloaded from the
# internet": this removes that flag from the whole folder and signs the apps ad hoc on this Mac
# (what the JKHub OpenJK guide has you do by hand in Terminal).
cd "$(dirname "$0")" || exit 1
echo "JK_Shinobi: установка в $(pwd)"
echo
ok=1
if [ ! -f base/assets0.pk3 ] || [ ! -f base/assets1.pk3 ]; then
  echo "!! Нет файлов игры: base/assets0.pk3 и base/assets1.pk3 должны лежать в папке base рядом с этим файлом."
  ok=0
fi
xattr -dr com.apple.quarantine . 2>/dev/null
chmod +x ./*.command JK_Shinobi_Steam openjk.arm64.app/Contents/MacOS/openjk.arm64 2>/dev/null
# re-sign: inner libraries first, then the app (the bundled SDL2's own signature is broken)
find openjk.arm64.app -name "*.dylib" -exec codesign --force --sign - {} \; 2>/dev/null
codesign --force --sign - openjk.arm64.app/Contents/MacOS/openjk.arm64 2>/dev/null
if [ ! -f openjk.arm64.app/Contents/Frameworks/libSDL3.dylib ]; then
  echo "!! Нет openjk.arm64.app/Contents/Frameworks/libSDL3.dylib - без неё игра не запустится."
  ok=0
fi
codesign --force --sign - openjk.arm64.app 2>/dev/null
codesign --force --sign - shinobi/*.dylib JK_Shinobi_Steam 2>/dev/null
if ! codesign --verify openjk.arm64.app/Contents/Frameworks/libSDL2-2.0.0.dylib 2>/dev/null; then
  echo "!! Не получилось подписать игру (codesign)."
  ok=0
fi
if [ ! -d /Applications/Steam.app ] && [ ! -d "$HOME/Applications/Steam.app" ]; then
  echo "!! Steam не найден - для игры по сети его нужно установить: https://store.steampowered.com/about/"
fi
echo
if [ $ok = 1 ]; then
  echo "Готово. Дальше:"
  echo "  Join_Friend.command  - подключиться к другу через Steam (он хостит)"
  echo "  Host_Game.command    - захостить самому через Steam"
  echo "  JA_Join.command / JA_Host.command - то же, но обычный Jedi Academy без мода"
  echo "  Solo.command         - просто запустить игру (проверка)"
else
  echo "Установка не закончена - см. сообщения выше."
fi
echo
read -r -p "Нажми Enter, чтобы закрыть."
