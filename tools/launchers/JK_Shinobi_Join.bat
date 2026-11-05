@echo off
rem JK_Shinobi: join a friend's game. Full screen (a borderless window at your desktop
rem resolution). Type the host's address (shown in his JK_Shinobi_Host.bat window), or leave
rem it empty for the server list (games on your network / your VPN's).
rem   JK_Shinobi_Join.bat 26.12.34.56   - joins that address straight away
cd /d "%~dp0"
set "HOST=%~1"
if "%HOST%"=="" set /p HOST=Host address (empty = server list): 
if "%HOST%"=="" (
  start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +wait 30 +ui_openmenu joinserver
) else (
  start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +exec shinobi_binds.cfg +connect %HOST%
)
