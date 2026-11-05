@echo off
rem JK_Shinobi: makes JK_Shinobi-mac.zip for a friend on a Mac - JK_Shinobi-mac-kit.tar
rem (the Mac game + mod, made by tools/package-mac.sh) plus this game's base\assets0-3.pk3.
rem Windows' own tar copies the Mac files with their permissions (the .command files and the
rem app must stay executable) - a zip made by Explorer would lose them.
cd /d "%~dp0"
if not exist JK_Shinobi-mac-kit.tar (echo JK_Shinobi-mac-kit.tar is missing next to this file. & pause & exit /b 1)
if not exist base\assets0.pk3 (echo base\assets0.pk3 not found - put this file into Jedi Academy\GameData. & pause & exit /b 1)
set "T=%TEMP%\jks_mackit"
if exist "%T%" rmdir /s /q "%T%"
mkdir "%T%\JK_Shinobi\base"
for %%f in (assets0 assets1 assets2 assets3) do (
  mklink /H "%T%\JK_Shinobi\base\%%f.pk3" "base\%%f.pk3" >nul 2>&1 || copy /y "base\%%f.pk3" "%T%\JK_Shinobi\base\" >nul
)
echo Packing JK_Shinobi-mac.zip (about 1.3 GB, a minute or two)...
if exist JK_Shinobi-mac.zip del JK_Shinobi-mac.zip
"%SystemRoot%\System32\tar.exe" --format zip --options zip:compression=store -cf JK_Shinobi-mac.zip @JK_Shinobi-mac-kit.tar -C "%T%" JK_Shinobi/base/assets0.pk3 JK_Shinobi/base/assets1.pk3 JK_Shinobi/base/assets2.pk3 JK_Shinobi/base/assets3.pk3
set RC=%ERRORLEVEL%
rmdir /s /q "%T%"
if not "%RC%"=="0" (echo Packing failed. & pause & exit /b 1)
echo.
echo Done: %CD%\JK_Shinobi-mac.zip
echo Send it to your friend (Google Drive, WeTransfer...). He unpacks it and runs 1_Install.command.
explorer /select,"%CD%\JK_Shinobi-mac.zip"
pause
