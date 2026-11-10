<#
Records missions under autoplay (the autotest that tries to win, code\Replay\Autoplay.h) into
recordings\autoplay, then replays each in Release and compares the traces.

  tools\record_autoplay.ps1 [-Missions sm1-01,sm1-02] [-Skill 2] [-Time 1800] [-GameDir <dir>] [-Fs1]

-Fs1 plays the FreeSpace 1 main campaign (sm1-01 to sm3-09a, 29 missions) and Silent Threat
(md-01 to md-12) in the FS1 install
($env:PATH_FS1 unless -GameDir) into recordings\autoplay_fs1, copying this build's exes there first.

Campaign missions only (Tom, 2026-11-07): the default is every FS2 campaign mission, and the
Autotest* missions, made to test features, are skipped. None is left out (Tom, 2026-11-07): every mission
must be shown completable, so loop1-2, loop2-1, sm3-04 and FS1's sm1-06a are back in, pass or not.

Always passes -at_skill (default 2), never the pilot's level (D-20261107-124843); a run at another
level is a separate, labelled run: its files get a _skillN suffix.
-Time is the mission-time limit (AUTOPLAY_DEFAULT_TIME, 1800, unless given); $TimeCaps below raises it
for named missions (D-20261108-034420), unless -Time is given.
The summary lists each mission's "Autoplay result" line (WON, FAILED, PARTIAL, STALLED, TIMEOUT).
The results are a tracked baseline, not a gate; the replays must still sync (D-20261107-124845).
An existing recording of the same mission is replaced; check the replays before committing.
Build Release first, and don't run the game from another session while this runs.
#>
param(
	[string[]]$Missions = @('sm1-01', 'sm1-02', 'sm1-03', 'sm1-04', 'sm1-05', 'sm1-06', 'sm1-07', 'sm1-08', 'sm1-09', 'sm1-10',
		'loop1-1', 'loop1-2', 'loop1-3', 'loop2-1', 'loop2-2',
		'sm2-01', 'sm2-02', 'sm2-03', 'sm2-04', 'sm2-05', 'sm2-06', 'sm2-07', 'sm2-08', 'sm2-09', 'sm2-10',
		'sm3-01', 'sm3-02', 'sm3-03', 'sm3-04', 'sm3-05', 'sm3-06', 'sm3-07', 'sm3-08', 'sm3-09', 'sm3-10'),
	[ValidateRange(0, 4)][int]$Skill = 2,
	[int]$Time = 1800,
	[string]$GameDir,
	[switch]$Fs1
)

$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"	# exe names (D-20261101-064106)
if (-not $GameDir) { $GameDir = if ($Fs1) { $env:PATH_FS1 } else { $env:PATH_FS2 } }
if (-not $GameDir) { throw 'Set PATH_FS2 (PATH_FS1 with -Fs1) or pass -GameDir' }
# -Fs1: the FreeSpace 1 main campaign, in the FS1 install, into recordings\autoplay_fs1
if ($Fs1 -and -not $PSBoundParameters.ContainsKey('Missions')) {
	$Missions = @('sm1-01') + (2..10 | ForEach-Object { 'sm1-{0:d2}a' -f $_ }) + (1..10 | ForEach-Object { 'sm2-{0:d2}a' -f $_ }) + (1..9 | ForEach-Object { 'sm3-{0:d2}a' -f $_ })
	# then Silent Threat, md-01 to md-12 in mdisk.vp (Tom, 2026-11-07)
	$Missions += (1..12 | ForEach-Object { 'md-{0:d2}' -f $_ })
}
# the build links into PATH_FS2; the FS1 install needs the same exes
if ($Fs1 -and ($GameDir -ne $env:PATH_FS2)) { foreach ($c in 'r', 'd') { Copy-Item (Join-Path $env:PATH_FS2 (Get-FS2Exe $c)) $GameDir -Force -ErrorAction SilentlyContinue } }
$rec = Join-Path $repo $(if ($Fs1) { 'recordings\autoplay_fs1' } else { 'recordings\autoplay' })
$out = Join-Path $repo ("replays\autoplay_" + (Get-Date -Format 'yyyyMMdd_HHmm'))
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
New-Item -ItemType Directory -Force $rec, $out | Out-Null
$Missions = $Missions | ForEach-Object { $_ -split ',' } | Where-Object { $_ -notlike 'Autotest*' }
$summary = Join-Path $out 'summary.txt'
# per-mission time caps (D-20261108-034420): mission time in seconds, only where measurements show the
# goal still progressing at the default cap; given -Time overrides them all
$TimeCaps = @{
	'md-12' = @{ Time = 6000; Why = 'the Hades (800,000 hull) still losing hull at 1800 s'; Decision = 'D-20261108-034420' }
}

foreach ($mission in $Missions) {
	$m = if ($Skill -eq 2) { $mission } else { "${mission}_skill$Skill" }
	$cap = if ($TimeCaps.ContainsKey($mission) -and -not $PSBoundParameters.ContainsKey('Time')) { $TimeCaps[$mission].Time } else { $Time }
	# the recorder writes NAME_2 beside an existing file instead of replacing it
	foreach ($e in '.fs2rec', '.trace', '.autotest.log', '.fs2rec.record.log', '.fs2rec.replay.log') { Remove-Item (Join-Path $rec "$m$e") -ErrorAction SilentlyContinue }
	$p = Start-Process (Join-Path $GameDir (Get-FS2Exe r)) -WorkingDirectory $GameDir -Wait -PassThru `
		-ArgumentList "-window -autotest -at_mission $mission -at_autoplay -at_skill $Skill -at_time $cap -record `"$rec\$m.fs2rec`" -trace `"$rec\$m.trace`""
	Copy-Item (Join-Path $GameDir 'autotest.log') (Join-Path $rec "$m.autotest.log")
	$result = (Select-String -Path (Join-Path $rec "$m.autotest.log") -Pattern 'Autoplay result: (.*)' | Select-Object -Last 1)
	$result = if ($result) { $result.Matches[0].Groups[1].Value } else { 'no result line' }
	$line = "$m (skill $Skill, cap $cap s) record exit=$($p.ExitCode) $result"
	foreach ($c in 'r') {	# Release only at this stage (Tom, 2026-11-07)
		$p = Start-Process (Join-Path $GameDir (Get-FS2Exe $c)) -WorkingDirectory $GameDir -Wait -PassThru `
			-ArgumentList "-window -autotest -replay `"$rec\$m.fs2rec`" -trace `"$out\${m}_$c.trace`""
		Copy-Item (Join-Path $rec "$m.fs2rec.replay.log") (Join-Path $out "$m.$c.replay.log")
		$res = (Select-String -Path (Join-Path $rec "$m.fs2rec.replay.log") -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Matches[0].Value
		$diff = & $td (Join-Path $rec "$m.trace") (Join-Path $out "${m}_$c.trace") | Select-Object -Last 1
		$line += " | $c exit=$($p.ExitCode) $res / $diff"
	}
	$line | Tee-Object -FilePath $summary -Append
}
