@echo off
rem JK_Shinobi over Steam - host. Steam must be running (logged in). Opens the game full screen on
rem the Create Game screen; once your map is up, Steam friends who start JK_Shinobi_SteamJoin.bat
rem are let in - through Steam (no VPN, no router setup; your Steam status shows "Spacewar").
rem Keep this window open while you play: it carries the friends' traffic and closes with the game.
cd /d "%~dp0"
JK_Shinobi_Steam.exe host
pause
