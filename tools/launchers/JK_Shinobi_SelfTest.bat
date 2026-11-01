@echo off
rem JK_Shinobi: automatic self-test. Casts every jutsu, takes screenshots and quits
rem (about a minute). Results: Documents\My Games\OpenJK\shinobi\qconsole.log,
rem shinobi_condump.txt and screenshots\st*.jpg
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +exec shinobi_binds.cfg +exec shinobi_selftest.cfg %*
