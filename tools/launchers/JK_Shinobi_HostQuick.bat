@echo off
rem JK_Shinobi: two-window test, part 1 - hosts mp/ffa3 at once (no menu) in a 1280x720 window
rem (drag it to one side). Then start JK_Shinobi_Client2.bat for the second player.
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set r_noborder 0 +exec shinobi_binds.cfg +exec shinobi_host.cfg %*
