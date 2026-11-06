@echo off
rem JK_Shinobi: only the bot tests - tactics (Kawarimi out of a Water Prison, its own prison held,
rem seals broken, fireballs dodged, a Kaiten kept off) and the old defence / fists (~1.5 minutes),
rem then quits. Results: Documents\My Games\OpenJK\shinobi\shinobi_autotest.txt
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +exec shinobi_bottest.cfg %*
