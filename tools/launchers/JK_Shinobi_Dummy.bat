@echo off
rem JK_Shinobi: punching bags - the small Chunin arena, two bots that just stand in front of you (one holds
rem its guard up) and come back after each death (shinobi_dummy.cfg). Needs shinobi_map_shinobi_chunin.pk3.
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +exec shinobi_dummy.cfg %*
