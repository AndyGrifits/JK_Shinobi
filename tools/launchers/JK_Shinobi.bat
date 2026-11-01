@echo off
rem JK_Shinobi launcher - place in Jedi Academy\GameData
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +exec shinobi_binds.cfg %*
