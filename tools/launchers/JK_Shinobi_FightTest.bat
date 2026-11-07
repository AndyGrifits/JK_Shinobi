@echo off
rem JK_Shinobi: the fight rules tests - guard / parry edge cases (launcher on a guard, push on a parry, riposte),
rem guard costs, flinch, air combo, blade combo, plain cuts (~2 minutes), then quits.
rem Results: Documents\My Games\OpenJK\shinobi\shinobi_autotest.txt
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +exec shinobi_fighttest.cfg %*
