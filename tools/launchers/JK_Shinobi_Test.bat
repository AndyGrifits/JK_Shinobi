@echo off
rem JK_Shinobi: start a local free-for-all with bots
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +exec shinobi_binds.cfg +exec shinobi_test.cfg %*
