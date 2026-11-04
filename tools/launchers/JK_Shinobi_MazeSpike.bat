@echo off
rem JK_Shinobi: maze spike auto-test (misc_bsp blocks). Takes screenshots and quits (about 30 s).
rem Results: Documents\My Games\OpenJK\shinobi\screenshots\mz*.jpg and shinobi_mazespike.txt
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +exec shinobi_mazespike.cfg %*
