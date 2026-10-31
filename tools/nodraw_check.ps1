<#
Checks that the game logic does not depend on drawing (PrePort step 3, docs\LOGIC_DRAW_SPLIT.md).

By default it replays each checked-in recording in recordings\autotest with -nodraw and compares the trace with
the recorded one. The normal replay of the same recordings is tools\replay_check.ps1's job, so it is not repeated.

With -Fresh, for a branch whose checked-in recordings no longer replay, it records a fresh run of each mission with
the current build, with the AI flying the player ship (-playernpc) as tools\record_autotest.ps1 does, then replays
that recording twice, normally and with -nodraw, and compares each replay's trace with the recorded one.

  draw     (-Fresh only) must always match. If it does not, the build itself is not deterministic: fix that first.
  nodraw   matches only when nothing the game logic needs happens while drawing.

Usage (PowerShell, from anywhere):
  tools\nodraw_check.ps1 [-Out <dir>] [-Missions sm1-01,sm2-04,...] [-Config r|d] [-GameDir <dir>] [-Fresh] [-Headless]

-Headless replays with -headless instead of -nodraw: a message-only window that is never shown, no graphics device
(-nographics, the null renderer code\Graphics\GrDummy.cpp, which also skips drawing) and no sound device
(-nosounddevice: sounds load their length, nothing plays), so the game logic runs headless (PrePort gate 1). Keys,
mouse and joystick come from the recording. Each replay's autotest.log must say there was no graphics device.

-Missions defaults to every mission in recordings\autotest. Results go to <Out>\summary.txt, one line per mission;
<Out> defaults to replays\nodraw_<date-time>. Build the config first (it runs FS2vs2022r.exe or FS2vs2022d.exe from
the game directory), and don't build or run the game from another session while this runs.
About 45 seconds a mission, or about 2.5 minutes with -Fresh.
#>
param(
	[string]$Out,
	[string[]]$Missions,
	[string]$Config = 'r',
	[string]$GameDir = $env:PATH_FS2,
	[switch]$Fresh,
	[switch]$Headless
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Out) { $Out = Join-Path $repo ("replays\nodraw_" + (Get-Date -Format 'yyyyMMdd_HHmm')) }
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
if (-not (Test-Path $td)) { throw "No $td. Build it: MSBuild tools\TraceDiff\TraceDiff.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:SolutionDir=$repo\" }
New-Item -ItemType Directory -Force $Out | Out-Null
# the test missions it records, as replay_check.ps1 and record_autotest.ps1 do
Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $GameDir 'data\missions')
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

# the replay that must not depend on drawing: -nodraw, or with -Headless no graphics or sound device at all
$quiet = if ($Headless) { 'headless' } else { 'nodraw' }

Set-Location $GameDir
$pass = 0; $fail = 0
foreach ($m in $Missions) {
	if ($Fresh) {
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
		$recok = ($code -eq 0)
		$modes = @('draw', $quiet)
	} else {
		$rec = Join-Path $repo "recordings\autotest\$m.fs2rec"
		$trace = Join-Path $repo "recordings\autotest\$m.trace"
		$line = $m
		$modes = @($quiet)
		$recok = $true
	}
	$ok = (Test-Path $trace) -and $recok
	foreach ($mode in $modes) {
		$rtrace = Join-Path $Out "${m}_$mode.trace"
		$log = Join-Path $Out "${m}_$mode.replay.log"
		Remove-Item $rtrace, "$rec.replay.log" -ErrorAction SilentlyContinue
		$flags = "-window -autotest -replay `"$rec`" -trace `"$rtrace`""
		if ($mode -eq 'nodraw') { $flags += ' -nodraw' }
		if ($mode -eq 'headless') { $flags += ' -headless' }
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
		if (($mode -eq 'headless') -and -not (Select-String (Join-Path $GameDir 'autotest.log') -Pattern 'No graphics device' -Quiet)) { $res = "NOT HEADLESS ($res)" }
		$counts = Select-String (Join-Path $GameDir 'autotest.log') -Pattern 'Asserts \d+, Int3 \d+, warnings \d+' -ErrorAction SilentlyContinue | Select-Object -Last 1
		if (($res -notmatch '^REPLAY OK') -or ($diff -notmatch '^MATCH')) { $ok = $false }
		$line += " | $mode exit=$code $res / $diff"
		# an autotest run exits 0 only with no Assert, Int3 or Warning (D-029, D-077)
		if ($counts) { $line += " ($($counts.Matches[0].Value))" }
		if ($code -ne 0) { $ok = $false }
		# a Debug build must prove its array guards fire (D-20261018-030012)
		if (($Config -match '^d') -and -not (Select-String (Join-Path $GameDir 'autotest.log') -Pattern 'guards: assert' -Quiet)) { $ok = $false; $line += ' GUARDS NOT ASSERTING' }
	}
	if ($ok) { $pass++ } else { $fail++; $line = "FAIL $line" }
	$line | Add-Content $summary
	$line
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
