@echo off
rem JK_Shinobi: procedural maze auto-test (shinobi_oro_maze, layout 0). Tours it, gets spotted, runs, quits (about 2 min).
rem Results: Documents\My Games\OpenJK\shinobi\screenshots\mt*.jpg and shinobi_mazetest.txt
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +exec shinobi_mazetest.cfg %*
