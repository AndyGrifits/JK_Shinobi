@echo off
rem JK_Shinobi combat 2 - the experiment: Shift block, LMB light chain, RMB heavy + finishers,
rem Space in the air = Shunshin (shinobi_binds_c2.cfg). The Chunin arena with two punching bags
rem (shinobi_combat2.cfg). JK_Shinobi.bat puts the usual controls back.
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +exec shinobi_binds_c2.cfg +exec shinobi_combat2.cfg %*
