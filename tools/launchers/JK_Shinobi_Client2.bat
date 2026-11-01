@echo off
rem JK_Shinobi: two-window test, part 2 - a second player "Dummy" in a 1280x720 window
rem (drag it next to the first one) that joins the game hosted by JK_Shinobi_Host.bat.
rem It keeps its own settings and log in Documents\My Games\OpenJK_Dummy, so it
rem never touches your binds or your build. Switch windows with Alt+Tab.
cd /d "%~dp0"
start "" openjk.x86_64.exe +set fs_homepath "%USERPROFILE%\Documents\My Games\OpenJK_Dummy" +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set r_noborder 0 +set name Dummy +set shinobi_build "suiton,wave,suiro,futon,windblade/2,2,2,2,2/none" +exec shinobi_binds.cfg +set cg_thirdPerson 1 +connect 127.0.0.1 %*
