<#
Runs FS1 demo missions under -autotest (docs/FS1_DEMO.md, section 5) and prints each one's result.

  tools\fs1demo_run.ps1 [-Missions btmdemo,demo01,...] [-Configs r,d] [-Time 60] [-GameDir <dir>] [-Pilot <name>] [-Out <dir>]

-GameDir is the FS1 demo folder: fsdemo10.exe (from the original demo download, never committed) with the exes
beside it; the game extracts the demo into fs1demo\ there on first start. It defaults to $env:FS1DEMO_PATH.
Build into it with $env:PATH_FS1 set to that folder (FS1 branches link into PATH_FS1).
-Pilot names an existing pilot in that folder (autotest picks it with -at_pilot); make one by starting the game
once by hand. Each mission's autotest.log is copied to <Out>\<mission>_<config>.log (default
%LOCALAPPDATA%\fs1demo_runs). The last line is PASS when every run exits 0 with no Warning, Error, Assert or Int3
(D-20261020-213146: the demo's own ships.tbl weapon names are logged, not warned, on its data); FAIL otherwise.
#>
param(
	[string[]]$Missions = @('btmdemo', 'demo01', 'demo02a', 'demo02b'),
	[string[]]$Configs = @('r', 'd'),
	[int]$Time = 60,
	[string]$GameDir = $env:FS1DEMO_PATH,
	[string]$Pilot = 'Tom',
	[string]$Out = (Join-Path $env:LOCALAPPDATA 'fs1demo_runs'),
	[switch]$Headless	# -headless: null renderer at GR_640, no sound device (#101)
)

if (-not $GameDir -or -not (Test-Path $GameDir)) { 'FAIL no FS1 demo folder (-GameDir or FS1DEMO_PATH)'; exit 1 }
# powershell -File passes "a,b" as one string
$Missions = @($Missions | ForEach-Object { $_ -split ',' })
$Configs = @($Configs | ForEach-Object { $_ -split ',' })
New-Item -ItemType Directory -Force $Out | Out-Null
Set-Location $GameDir

$failed = @()
foreach ($c in $Configs) {
	$exe = ".\FS2vs2022$c.exe"
	if (-not (Test-Path $exe)) { "FAIL no $exe in $GameDir"; exit 1 }
	foreach ($m in $Missions) {
		while (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
		Remove-Item autotest.log -ErrorAction SilentlyContinue
		$p = Start-Process $exe -ArgumentList "-window -autotest -at_mission $m -at_pilot $Pilot -at_time $Time$(if ($Headless) { ' -headless' })" -PassThru
		if (-not $p.WaitForExit(300000)) { Stop-Process $p -Force; "$m $c TIMEOUT"; $failed += "$m $c"; continue }
		$log = Join-Path $Out "${m}_$c.log"
		if (-not (Test-Path autotest.log)) { "$m $c exit=$($p.ExitCode) NO LOG"; $failed += "$m $c"; continue }
		Copy-Item autotest.log $log
		$counts = (Select-String $log -Pattern 'Asserts \d+, Int3 \d+, warnings \d+' | Select-Object -Last 1).Matches.Value
		$other = @(Select-String $log -Pattern '^\[[ \d.]+\] (Warning|Error|Assert|Int3)')
		$ok = ($p.ExitCode -eq 0) -and (-not $Headless -or (Select-String $log -Pattern 'No graphics device' -Quiet)) -and ($counts -notmatch 'Asserts [1-9]|Int3 [1-9]|warnings [1-9]') -and $other.Count -eq 0
		"$m $c exit=$($p.ExitCode) $counts$(if ($other) { ' | ' + $other[0].Line.Trim() })"
		if (-not $ok) { $failed += "$m $c" }
	}
}
if ($failed) { "FAIL $($failed -join ', ')"; exit 1 }
'PASS'
