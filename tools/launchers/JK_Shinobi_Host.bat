@echo off
rem JK_Shinobi: host a game for friends. Opens full screen (a borderless window at your
rem desktop resolution) straight on the stock Create Game screen: pick the game type and the
rem map, set the bots if you want them, Start - you play on the server you host.
rem First it asks the router (UPnP) to open UDP 29070 to this PC and says whether this
rem connection can host over the internet (a DS-Lite / carrier-NAT one can't: then a friend
rem hosts); the port is closed again when the game ends. Allow OpenJK in the Windows firewall.
cd /d "%~dp0"
echo.
echo  JK_Shinobi - hosting.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0JK_Shinobi_Upnp.ps1"
echo.
echo  On this network (or a shared VPN) friends can use one of these addresses:
powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } | ForEach-Object { '   ' + $_.IPAddress + '   (' + $_.InterfaceAlias + ')' }"
echo.
echo  (this window closes the router's port when the game ends - leave it open)
start "" /wait openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +set g_forcePowerDisable 0 +set g_maxForceRank 7 +set bot_minplayers 0 +set sv_maxclients 8 +exec shinobi_binds.cfg +wait 30 +ui_openmenu createserver %*
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0JK_Shinobi_Upnp.ps1" -Remove
