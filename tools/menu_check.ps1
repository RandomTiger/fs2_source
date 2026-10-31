<#
Runs the autotest menu tour (-autotest -at_menus, docs/AUTOTEST.md): every single-player screen, with each tab, page
and help overlay, opens and closes with no Assert, Int3, Warning or crash.

Usage (PowerShell, from anywhere):
  tools\menu_check.ps1 [-Out <dir>] [-Configs r,d] [-GameDir <dir>] [-Shots <dir>] [-Mission <file.fs2>]

The game directory defaults to $env:PATH_FS2, where the build puts FS2vs2022r.exe / FS2vs2022d.exe.
Prints one line per screen per build ("<config> Screen '<category>/<screen>': PASS|FAIL <why>|SKIP <why>"), then one
line per build. Everything printed also goes to <Out>\summary.txt; the last line is DONE pass=N fail=N (builds).
-Shots saves each screen as <Shots>\<config>\<category>\<screen>.bmp. Keep it outside the repo: the screenshots show
game data, which must never be committed.
Build first, and don't build or run the game from another session while this runs.
#>
param(
	[string]$Out,
	[string[]]$Configs = @('r', 'd'),
	[string]$GameDir = $env:PATH_FS2,
	[string]$Shots,
	[string]$Mission
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
	$gameArgs = '-window -autotest -at_menus'
	if ($Mission) { $gameArgs += " -at_mission $Mission" }
	if ($Shots) {
		$dir = Join-Path $Shots $e
		New-Item -ItemType Directory -Force $dir | Out-Null
		$gameArgs += " -at_shots `"$dir`""
	}
	$p = Start-Process ".\FS2vs2022$e.exe" -ArgumentList $gameArgs -PassThru
	if (-not $p.WaitForExit(900000)) { Stop-Process $p -Force }
	$tour = 'NO LOG'
	if (Test-Path $log) {
		Copy-Item $log (Join-Path $Out "menus_$e.autotest.log")
		foreach ($m in (Select-String $log -Pattern "Screen '[^']+': (PASS|FAIL|SKIP).*")) {
			$line = "$e $($m.Matches[0].Value)"
			$line | Add-Content $summary
			$line
		}
		$m = Select-String $log -Pattern 'Menu tour: \d+ screens, .*' | Select-Object -Last 1
		$tour = if ($m) { $m.Matches[0].Value } else { 'tour did not finish' }
	}
	$line = "$e exit=$($p.ExitCode) $tour"
	if ($p.ExitCode -ne 0) { $fail++; $line = "FAIL $line" } else { $pass++ }
	$line | Add-Content $summary
	$line
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
