@echo off
rem JK_Shinobi: only the Reverse Lotus log and the Yin-Yang taijutsu / Inner Gates tests (~1 minute),
rem then quits. Results: Documents\My Games\OpenJK\shinobi\shinobi_autotest.txt
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +exec shinobi_lotustest.cfg %*
