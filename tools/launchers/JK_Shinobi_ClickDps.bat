@echo off
rem JK_Shinobi: only the click_dps measurement (every weapon, fists first, fastest single clicks on a still
rem target, ~2 minutes), then quits. Results: Documents\My Games\OpenJK\shinobi\shinobi_autotest.txt
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +exec shinobi_clickdps.cfg %*
