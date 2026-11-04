@echo off
rem JK_Shinobi: open a map in a 1280x720 window with cheats on (noclip etc), no bots.
rem   JK_Shinobi_MapTest.bat              -> shinobi_konoha
rem   JK_Shinobi_MapTest.bat <mapname>
cd /d "%~dp0"
set MAP=%1
if "%MAP%"=="" set MAP=shinobi_konoha
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set r_noborder 0 +exec shinobi_binds.cfg +set g_gametype 0 +set g_forcePowerDisable 0 +set g_maxForceRank 7 +set bot_minplayers 0 +set sv_maxclients 8 +devmap %MAP%
