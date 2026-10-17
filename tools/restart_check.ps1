<#
The restart check (decision D-055): two recordings replayed one after the other in one process, with the host's
-replay_next. The game shuts down and starts again between them, as a host that restarts it would. Each trace must
equal its recording's reference trace, so the second mission plays as if it were the first of its process.

Usage (PowerShell, from anywhere):
  tools\restart_check.ps1 [-First sm1-01] [-Second sm3-06] [-Configs r,d] [-Out <dir>] [-GameDir <dir>]

-Missions is accepted and ignored, so suite_check.ps1 can pass its set to every check.
Results go to <Out>\summary.txt; the last line is DONE pass=<n> fail=<n>, one pass per build.
#>
param(
	[string]$First = 'sm1-01',
	[string]$Second = 'sm3-06',
	[string[]]$Configs = @('r', 'd'),
	[string]$Out,
	[string]$GameDir = $env:PATH_FS2,
	[string[]]$Missions
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Out) { $Out = Join-Path $repo ("replays\restart_" + (Get-Date -Format 'yyyyMMdd_HHmm')) }
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
if (-not (Test-Path $td)) { throw "No $td. Build it: MSBuild tools\TraceDiff\TraceDiff.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:SolutionDir=$repo\" }
New-Item -ItemType Directory -Force $Out | Out-Null
$summary = Join-Path $Out 'summary.txt'
$Configs = $Configs | ForEach-Object { $_ -split ',' }

$recA = Join-Path $repo "recordings\autotest\$First.fs2rec"
$recB = Join-Path $repo "recordings\autotest\$Second.fs2rec"

function Wait-NoGame {
	while (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
}

Set-Location $GameDir
$pass = 0; $fail = 0
"Restart check: $First then $Second in one process" | Set-Content $summary
foreach ($e in $Configs) {
	$ta = Join-Path $Out "${First}_first_$e.trace"
	$tb = Join-Path $Out "${Second}_second_$e.trace"
	Remove-Item $ta, $tb, "$recA.replay.log", "$recB.replay.log" -ErrorAction SilentlyContinue
	Wait-NoGame
	$flags = "-window -autotest -replay `"$recA`" -trace `"$ta`" -replay_next `"$recB`" -trace_next `"$tb`""
	$p = Start-Process ".\FS2vs2022$e.exe" -ArgumentList $flags -PassThru
	$line = "$e"
	if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $line += ' TIMEOUT' }
	$line += " exit=$($p.ExitCode)"
	foreach ($x in @(@($recA, $ta, 'first'), @($recB, $tb, 'second'))) {
		$diff = 'NO TRACE'
		if (Test-Path $x[1]) { $diff = (& $td ([IO.Path]::ChangeExtension($x[0], '.trace')) $x[1] | Select-Object -Last 1) }
		$line += " | $($x[2]): $diff"
		if (Test-Path "$($x[0]).replay.log") { Copy-Item "$($x[0]).replay.log" (Join-Path $Out ("$($x[2])_$e.replay.log")) }
	}
	if (Test-Path '.\autotest.log') { Copy-Item '.\autotest.log' (Join-Path $Out "restart_$e.autotest.log") }
	if (($p.ExitCode -eq 0) -and ($line -notmatch 'NO TRACE|DIVERGED|TIMEOUT') -and (([regex]::Matches($line, 'MATCH')).Count -eq 2)) {
		$pass++
	} else {
		$fail++
		$line = "FAIL $line"
	}
	$line | Add-Content $summary
	$line
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
