<#
Records missions under autotest into recordings\autotest, then replays each in Release and Debug and
compares the traces.

  tools\record_autotest.ps1 -Missions AutotestAI,sm1-01 [-Time 120] [-NoPlayerNpc] [-GameDir <dir>]
                            [-RecDir <dir>] [-Extra '<more game parameters>'] [-Fs1]

-RecDir puts the recordings somewhere other than recordings\autotest (the demo suite: recordings\demo).
-Extra is added to the recording's command line, for example '-at_pilot TomDemo -at_skill 2'.
-Fs1 records FreeSpace 1 missions into recordings\fs1 (unless -RecDir), in the FS1 install ($env:PATH_FS1 unless
-GameDir), with the exes found there; the test missions in data\missions are not copied in.

Automatic recordings run with -playernpc: the AI flies the player ship. A mission whose notes say
"autotest: player stays still" is still played with nobody flying the player (the game checks).
-NoPlayerNpc records without it.
The time limit is -Time, unless the mission file in data\missions has "-at_time N" in its notes.
An existing recording of the same mission is replaced; check the replays before committing.
Build both configs first, and don't run the game from another session while this runs.
#>
param(
	[Parameter(Mandatory = $true)][string[]]$Missions,
	[int]$Time = 120,
	[switch]$NoPlayerNpc,
	[string]$GameDir,
	[string]$RecDir,
	[string]$Extra = '',
	[switch]$Fs1
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { $GameDir = if ($Fs1) { $env:PATH_FS1 } else { $env:PATH_FS2 } }
if (-not $GameDir) { throw 'Set PATH_FS2 (PATH_FS1 with -Fs1) or pass -GameDir' }
$rec = if ($RecDir) { $RecDir } else { Join-Path $repo $(if ($Fs1) { 'recordings\fs1' } else { 'recordings\autotest' }) }
$out = Join-Path $repo ("replays\record_" + (Get-Date -Format 'yyyyMMdd_HHmm'))
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
New-Item -ItemType Directory -Force $rec, $out | Out-Null
if (-not $Fs1) { Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $GameDir 'data\missions') }
$Missions = $Missions | ForEach-Object { $_ -split ',' }
$summary = Join-Path $out 'summary.txt'

foreach ($m in $Missions) {
	$t = $Time
	$file = Join-Path $repo "data\missions\$m.fs2"
	if (Test-Path $file) {
		$notes = [regex]::Match((Get-Content -Raw $file), '(?s)\$Notes:(.*?)\$End Notes:').Groups[1].Value
		$found = [regex]::Match($notes, '-at_time (\d+)')
		if ($found.Success) { $t = [int]$found.Groups[1].Value }
	}
	foreach ($e in '.fs2rec', '.trace', '.autotest.log', '.fs2rec.record.log', '.fs2rec.replay.log') { Remove-Item (Join-Path $rec "$m$e") -ErrorAction SilentlyContinue }
	$npc = if ($NoPlayerNpc) { '' } else { ' -playernpc' }
	$p = Start-Process (Join-Path $GameDir 'FS2vs2022r.exe') -WorkingDirectory $GameDir -Wait -PassThru `
		-ArgumentList "-window -autotest -at_mission $m -at_time $t$npc $Extra -record `"$rec\$m.fs2rec`" -trace `"$rec\$m.trace`""
	Copy-Item (Join-Path $GameDir 'autotest.log') (Join-Path $rec "$m.autotest.log")
	$line = "$m ($t s$npc) record exit=$($p.ExitCode) " + (Get-Content (Join-Path $rec "$m.fs2rec.record.log") -Tail 1)
	foreach ($c in 'r', 'd') {
		$p = Start-Process (Join-Path $GameDir "FS2vs2022$c.exe") -WorkingDirectory $GameDir -Wait -PassThru `
			-ArgumentList "-window -autotest -replay `"$rec\$m.fs2rec`" -trace `"$out\${m}_$c.trace`""
		Copy-Item (Join-Path $rec "$m.fs2rec.replay.log") (Join-Path $out "$m.$c.replay.log")
		$res = (Select-String -Path (Join-Path $rec "$m.fs2rec.replay.log") -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Matches[0].Value
		$diff = & $td (Join-Path $rec "$m.trace") (Join-Path $out "${m}_$c.trace") | Select-Object -Last 1
		$line += " | $c exit=$($p.ExitCode) $res / $diff"
	}
	$line | Tee-Object -FilePath $summary -Append
}
