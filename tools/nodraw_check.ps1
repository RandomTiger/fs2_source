<#
Checks that the game logic does not depend on drawing (PrePort step 3, docs\LOGIC_DRAW_SPLIT.md).

For each autotest mission it records a fresh run with the current build, with the AI flying the player ship
(-playernpc) as tools\record_autotest.ps1 does, then replays that recording twice:
once normally and once with -nodraw, and compares each replay's trace with the recorded one.

  draw     must always match. If it does not, the build itself is not deterministic: stop and fix that first.
  nodraw   matches only once nothing the game logic needs happens while drawing. That is the step 3 target.

It records fresh rather than using recordings\autotest because step 3 changes the effect random numbers, so the
checked-in recordings no longer replay with this branch. They are re-recorded once, when the branch is finished.

Usage (PowerShell, from anywhere):
  tools\nodraw_check.ps1 [-Out <dir>] [-Missions sm1-01,sm2-04,...] [-Config r|d] [-GameDir <dir>]

-Missions defaults to every mission in recordings\autotest. Results go to <Out>\summary.txt, one line per mission;
<Out> defaults to replays\nodraw_<date-time>. Build the config first (it runs FS2vs2022r.exe or FS2vs2022d.exe from
the game directory), and don't build or run the game from another session while this runs.
About 2.5 minutes a mission, so about 1.5 hours for all of them.
#>
param(
	[string]$Out,
	[string[]]$Missions,
	[string]$Config = 'r',
	[string]$GameDir = $env:PATH_FS2
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Out) { $Out = Join-Path $repo ("replays\nodraw_" + (Get-Date -Format 'yyyyMMdd_HHmm')) }
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
if (-not (Test-Path $td)) { throw "No $td. Build it: MSBuild tools\TraceDiff\TraceDiff.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:SolutionDir=$repo\" }
New-Item -ItemType Directory -Force $Out | Out-Null
$summary = Join-Path $Out 'summary.txt'

# powershell -File passes "a,b" as one string
$Missions = $Missions | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
if (-not $Missions) { $Missions = Get-ChildItem (Join-Path $repo 'recordings\autotest') -Filter *.fs2rec | ForEach-Object { $_.BaseName } }
if (-not $Missions) { throw 'No missions' }

# One copy of the game at a time: a second one closes straight away and writes no trace
function Wait-NoGame {
	while (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
}

function Run-Game([string]$flags) {
	Wait-NoGame
	$p = Start-Process ".\FS2vs2022$Config.exe" -ArgumentList $flags -PassThru
	if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; return 'TIMEOUT' }
	return $p.ExitCode
}

Set-Location $GameDir
$pass = 0; $fail = 0
foreach ($m in $Missions) {
	$rec = Join-Path $Out "$m.fs2rec"
	$trace = Join-Path $Out "$m.trace"
	# the recorder never overwrites
	Remove-Item "$rec*", $trace -ErrorAction SilentlyContinue
	# record the way tools\record_autotest.ps1 does: the AI flies the player, and the mission's notes may set the time
	$t = 120
	$file = Join-Path $repo "data\missions\$m.fs2"
	if (Test-Path $file) {
		$found = [regex]::Match([regex]::Match((Get-Content -Raw $file), '(?s)\$Notes:(.*?)\$End Notes:').Groups[1].Value, '-at_time (\d+)')
		if ($found.Success) { $t = [int]$found.Groups[1].Value }
	}
	$code = Run-Game "-window -autotest -at_mission $m -at_time $t -playernpc -record `"$rec`" -trace `"$trace`""
	$line = "$m | record exit=$code"
	$ok = Test-Path $trace
	foreach ($mode in 'draw', 'nodraw') {
		$rtrace = Join-Path $Out "${m}_$mode.trace"
		$log = Join-Path $Out "${m}_$mode.replay.log"
		Remove-Item $rtrace, "$rec.replay.log" -ErrorAction SilentlyContinue
		$flags = "-window -autotest -replay `"$rec`" -trace `"$rtrace`""
		if ($mode -eq 'nodraw') { $flags += ' -nodraw' }
		$code = Run-Game $flags
		$res = 'NO LOG'
		if (Test-Path "$rec.replay.log") {
			Copy-Item "$rec.replay.log" $log
			$res = (Select-String $log -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line
		}
		$diff = 'NO TRACE'
		if (Test-Path $rtrace) {
			# a divergence ends with advice, so take the verdict line
			$tdout = & $td $trace $rtrace
			$diff = ($tdout | Select-String '^(MATCH|DIVERGED)' | Select-Object -First 1).Line
			if (-not $diff) { $diff = "NO VERDICT: $($tdout | Select-Object -Last 1)" }
		}
		if (($res -notmatch 'REPLAY OK') -or ($diff -notmatch '^MATCH')) { $ok = $false }
		$line += " | $mode exit=$code $res / $diff"
	}
	if ($ok) { $pass++ } else { $fail++; $line = "FAIL $line" }
	$line | Add-Content $summary
	$line
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
