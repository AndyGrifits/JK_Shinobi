@echo off
rem JK_Shinobi: automatic self-test. Casts every jutsu, takes screenshots, shoots the
rem Kaiten poses, runs the automatic tests (bots TestA / TestB) and quits (about 3 minutes).
rem Results: Documents\My Games\OpenJK\shinobi\qconsole.log, shinobi_condump.txt,
rem shinobi_autotest.txt and screenshots\st*.jpg, ka_*.jpg
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +exec shinobi_selftest.cfg %*
