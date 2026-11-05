@echo off
rem JK_Shinobi: copies the stock skeleton (_humanoid.gla) and its animation.cfg out of
rem base\assets*.pk3 into shinobi\src\ - the animation generator needs them.
cd /d "%~dp0"
if not exist "shinobi\src" mkdir "shinobi\src"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
 "Add-Type -AssemblyName System.IO.Compression.FileSystem;" ^
 "$want = @('models/players/_humanoid/_humanoid.gla','models/players/_humanoid/animation.cfg');" ^
 "$found = @{};" ^
 "foreach ($pk in (Get-ChildItem 'base\assets*.pk3' | Sort-Object Name -Descending)) {" ^
 "  $z = [System.IO.Compression.ZipFile]::OpenRead($pk.FullName);" ^
 "  foreach ($e in $z.Entries) {" ^
 "    $n = $e.FullName.Replace('\','/').ToLower();" ^
 "    if ($want -contains $n -and -not $found.ContainsKey($n)) {" ^
 "      $dst = Join-Path 'shinobi\src' $e.Name;" ^
 "      [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dst, $true);" ^
 "      $found[$n] = $pk.Name; Write-Host ('  ' + $e.FullName + '  <-  ' + $pk.Name) } };" ^
 "  $z.Dispose() };" ^
 "if ($found.Count -lt 2) { Write-Host 'NOT FOUND:' ($want | Where-Object { -not $found.ContainsKey($_) }) } else { Write-Host 'Done: shinobi\src' }"
pause
