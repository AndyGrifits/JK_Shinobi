@echo off
rem JK_Shinobi over Steam - join. Steam must be running (logged in) and the host must be your Steam
rem friend: this finds him (once his map is up), starts the game and connects it through Steam.
rem Keep this window open while you play: it carries the traffic and closes with the game.
cd /d "%~dp0"
JK_Shinobi_Steam.exe join
pause
