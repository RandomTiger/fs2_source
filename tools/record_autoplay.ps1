<#
Records missions under autoplay (the autotest that tries to win, code\Replay\Autoplay.h) into
recordings\autoplay, then replays each in Release and Debug and compares the traces.

  tools\record_autoplay.ps1 -Missions AutotestAI,sm1-01 [-Skill 2] [-Time 900] [-GameDir <dir>]

Always passes -at_skill (default 2), never the pilot's level (D-20261107-124843); a run at another
level is a separate, labelled run: its files get a _skillN suffix.
-Time is the mission-time limit (AUTOPLAY_DEFAULT_TIME, 900, unless given).
The summary lists each mission's "Autoplay result" line (WON, FAILED, PARTIAL, STALLED, TIMEOUT).
The results are a tracked baseline, not a gate; the replays must still sync (D-20261107-124845).
An existing recording of the same mission is replaced; check the replays before committing.
Build both configs first, and don't run the game from another session while this runs.
#>
param(
	[Parameter(Mandatory = $true)][string[]]$Missions,
	[ValidateRange(0, 4)][int]$Skill = 2,
	[int]$Time = 900,
	[string]$GameDir = $env:PATH_FS2
)

$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"	# exe names (D-20261101-064106)
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
$rec = Join-Path $repo 'recordings\autoplay'
$out = Join-Path $repo ("replays\autoplay_" + (Get-Date -Format 'yyyyMMdd_HHmm'))
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
New-Item -ItemType Directory -Force $rec, $out | Out-Null
Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $GameDir 'data\missions')
$Missions = $Missions | ForEach-Object { $_ -split ',' }
$summary = Join-Path $out 'summary.txt'

foreach ($mission in $Missions) {
	$m = if ($Skill -eq 2) { $mission } else { "${mission}_skill$Skill" }
	# the recorder writes NAME_2 beside an existing file instead of replacing it
	foreach ($e in '.fs2rec', '.trace', '.autotest.log', '.fs2rec.record.log', '.fs2rec.replay.log') { Remove-Item (Join-Path $rec "$m$e") -ErrorAction SilentlyContinue }
	$p = Start-Process (Join-Path $GameDir (Get-FS2Exe r)) -WorkingDirectory $GameDir -Wait -PassThru `
		-ArgumentList "-window -autotest -at_mission $mission -at_autoplay -at_skill $Skill -at_time $Time -record `"$rec\$m.fs2rec`" -trace `"$rec\$m.trace`""
	Copy-Item (Join-Path $GameDir 'autotest.log') (Join-Path $rec "$m.autotest.log")
	$result = (Select-String -Path (Join-Path $rec "$m.autotest.log") -Pattern 'Autoplay result: (.*)' | Select-Object -Last 1)
	$result = if ($result) { $result.Matches[0].Groups[1].Value } else { 'no result line' }
	$line = "$m (skill $Skill) record exit=$($p.ExitCode) $result"
	foreach ($c in 'r', 'd') {
		$p = Start-Process (Join-Path $GameDir (Get-FS2Exe $c)) -WorkingDirectory $GameDir -Wait -PassThru `
			-ArgumentList "-window -autotest -replay `"$rec\$m.fs2rec`" -trace `"$out\${m}_$c.trace`""
		Copy-Item (Join-Path $rec "$m.fs2rec.replay.log") (Join-Path $out "$m.$c.replay.log")
		$res = (Select-String -Path (Join-Path $rec "$m.fs2rec.replay.log") -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Matches[0].Value
		$diff = & $td (Join-Path $rec "$m.trace") (Join-Path $out "${m}_$c.trace") | Select-Object -Last 1
		$line += " | $c exit=$($p.ExitCode) $res / $diff"
	}
	$line | Tee-Object -FilePath $summary -Append
}
