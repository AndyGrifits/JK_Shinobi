@echo off
rem JK_Shinobi: host a game for friends. Opens full screen (a borderless window at your
rem desktop resolution) straight on the stock Create Game screen: pick the game type and the
rem map, set the bots if you want them, Start - you play on the server you host.
rem Friends join with JK_Shinobi_Join.bat and your address (below; UDP port 29070 - allow
rem OpenJK in the Windows firewall when it asks).
cd /d "%~dp0"
echo.
echo  JK_Shinobi - hosting. Your addresses (a friend types one into JK_Shinobi_Join.bat):
powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } | ForEach-Object { '   ' + $_.IPAddress + '   (' + $_.InterfaceAlias + ')' }"
echo.
echo  Over the internet: the address of a shared VPN (Radmin VPN, ZeroTier, Tailscale...) or your
echo  public address with UDP 29070 forwarded on the router.
echo.
start "" openjk.x86_64.exe +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 +set g_forcePowerDisable 0 +set g_maxForceRank 7 +set bot_minplayers 0 +set sv_maxclients 8 +exec shinobi_binds.cfg +wait 30 +ui_openmenu createserver %*
pause
