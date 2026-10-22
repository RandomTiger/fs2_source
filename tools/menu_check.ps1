<#
Runs the autotest menu tour (-autotest -at_menus, docs/AUTOTEST.md): every single-player screen opens and closes, with
no Assert, Int3, Warning or crash.

Usage (PowerShell, from anywhere):
  tools\menu_check.ps1 [-Out <dir>] [-Configs r,d] [-GameDir <dir>]

The game directory defaults to $env:PATH_FS2, where the build puts FS2vs2022r.exe / FS2vs2022d.exe.
Results go to <Out>\summary.txt, one line per build; the last line is DONE pass=N fail=N.
Build first, and don't build or run the game from another session while this runs.
#>
param(
	[string]$Out,
	[string[]]$Configs = @('r', 'd'),
	[string]$GameDir = $env:PATH_FS2
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Out) { $Out = Join-Path $repo ("replays\menus_" + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$summary = Join-Path $Out 'summary.txt'
# powershell -File passes "a,b" as one string
$Configs = $Configs | ForEach-Object { $_ -split ',' }

Set-Location $GameDir
$pass = 0; $fail = 0
foreach ($e in $Configs) {
	while (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
	$log = Join-Path $GameDir 'autotest.log'
	Remove-Item $log -ErrorAction SilentlyContinue
	$p = Start-Process ".\FS2vs2022$e.exe" -ArgumentList '-window -autotest -at_menus' -PassThru
	if (-not $p.WaitForExit(600000)) { Stop-Process $p -Force }
	$seen = 'NO LOG'
	if (Test-Path $log) {
		Copy-Item $log (Join-Path $Out "menus_$e.autotest.log")
		$m = Select-String $log -Pattern 'Menu tour: \d+ of \d+ screens seen' | Select-Object -Last 1
		$seen = if ($m) { $m.Matches[0].Value } else { 'tour did not finish' }
	}
	$line = "$e exit=$($p.ExitCode) $seen"
	if ($p.ExitCode -ne 0) { $fail++; $line = "FAIL $line" } else { $pass++ }
	$line | Add-Content $summary
	$line
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
