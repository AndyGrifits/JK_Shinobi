@echo off
rem JK_Shinobi launcher - place in Jedi Academy\GameData
rem logfile 2: console is written to Documents\My Games\OpenJK\shinobi\qconsole.log
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +exec shinobi_binds.cfg %*
