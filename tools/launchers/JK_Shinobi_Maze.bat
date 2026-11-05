@echo off
rem JK_Shinobi: play the procedural Orochimaru maze (shinobi_oro_maze) - a random layout every load, cheats on.
rem   JK_Shinobi_Maze.bat            random layout, 12 Sound ninjas
rem   JK_Shinobi_Maze.bat 17 20      layout 17, 20 ninjas
rem The maze log ([maze] lines: who spotted / lost you) goes to Documents\My Games\OpenJK\shinobi\qconsole.log
cd /d "%~dp0"
set LAYOUT=%1
if "%LAYOUT%"=="" set LAYOUT=-1
set NPCS=%2
if "%NPCS%"=="" set NPCS=12
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set r_noborder 0 +exec shinobi_binds.cfg +set g_gametype 0 +set bot_minplayers 0 +set sv_maxclients 8 +set shinobi_maze_layout %LAYOUT% +set shinobi_maze_npcs %NPCS% +set shinobi_maze_debug 1 +devmap shinobi_oro_maze
