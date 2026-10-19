<#
Replays the checked-in recordings and compares each replay's trace with the recorded one.

  recordings\autotest\*.fs2rec   replayed with -autotest
  recordings\played\*.fs2rec     hand-played recordings, replayed without it
  recordings\demo\*.fs2rec       the FreeSpace 2 demo set, replayed with -autotest in the demo folder (-DemoDir,
                                 default $env:FS2DEMO_TEST) in Release and Debug, whose traces must be identical; with no
                                 demo folder they are skipped (SKIP), never failed (D-20261018-221705)

Usage (PowerShell, from anywhere):
  tools\replay_check.ps1 [-Out <dir>] [-Missions sm1-01,SM1-04,...] [-Configs r] [-CrossCheck sm1-01] [-GameDir <dir>]

-Missions names are matched without case against both folders, so sm1-06 runs the autotest and the played SM1-06.
The game directory defaults to $env:PATH_FS2, where the build puts FS2vs2022r.exe / FS2vs2022d.exe.
Results go to <Out>\summary.txt, one line per recording; <Out> defaults to replays\check_<date-time>.
Every recording replays in Release (-Configs). The -CrossCheck missions (default sm1-01) also replay in Debug,
and their Release and Debug traces must be byte-identical: on 2026-10-13 every recording gave identical traces in
both builds, and Debug-made recordings replayed in Release, so one mission is enough to catch the builds drifting.
With $env:FS2_PROCDUMP set to Sysinternals procdump.exe (downloaded per machine, never committed), each run is watched
and a crash leaves a dump in <Out>; dumps stay local (they hold game data).
A line passes when every run says "REPLAY OK" and "MATCH", and every autotest run exits 0 (no Assert, Int3 or Warning). Build both configs first, and don't
build or run the game from another session while this runs: only one copy of the game can run.
#>
param(
	[string]$Out,
	[string[]]$Missions,
	[string[]]$Configs = @('r'),
	[string[]]$CrossCheck = @('sm1-01'),
	[string]$GameDir = $env:PATH_FS2,
	[string]$DemoDir = $env:FS2DEMO_TEST
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Out) { $Out = Join-Path $repo ("replays\check_" + (Get-Date -Format 'yyyyMMdd_HHmm')) }
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
New-Item -ItemType Directory -Force $Out | Out-Null
# the test missions the autotest recordings play
Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $GameDir 'data\missions')
$demoOk = $DemoDir -and (Test-Path (Join-Path $DemoDir 'FS2vs2022r.exe'))
$summary = Join-Path $Out 'summary.txt'

$recs = @()
$recs += Get-ChildItem (Join-Path $repo 'recordings\autotest') -Filter *.fs2rec -ErrorAction SilentlyContinue | ForEach-Object { @{ File = $_; Autotest = $true } }
# either folder may be empty, and git does not keep an empty folder
$recs += Get-ChildItem (Join-Path $repo 'recordings\played') -Filter *.fs2rec -ErrorAction SilentlyContinue | ForEach-Object { @{ File = $_; Autotest = $false } }
$demoRecs = @(Get-ChildItem (Join-Path $repo 'recordings\demo') -Filter *.fs2rec -ErrorAction SilentlyContinue | ForEach-Object { @{ File = $_; Autotest = $true; Demo = $true } })
# powershell -File passes "a,b" as one string
$Missions = $Missions | ForEach-Object { $_ -split ',' }
$Configs = $Configs | ForEach-Object { $_ -split ',' }
$CrossCheck = $CrossCheck | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
if ($Missions) { $recs = $recs | Where-Object { $Missions -contains $_.File.BaseName }; $demoRecs = @($demoRecs | Where-Object { $Missions -contains $_.File.BaseName }) }
if ($demoRecs -and -not $demoOk) {
	foreach ($r in $demoRecs) { "SKIP $($r.File.BaseName) (demo): no demo test folder with FS2vs2022r.exe (-DemoDir or FS2DEMO_TEST)" | Tee-Object -FilePath $summary -Append }
	$demoRecs = @()
}
$recs = @($recs) + $demoRecs
if (-not $recs) { throw "No recordings match: $Missions" }

# One copy of the game at a time: a second one closes straight away and writes no trace
function Wait-NoGame {
	while (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
}

$pass = 0; $fail = 0
foreach ($r in $recs) {
	$rec = $r.File.FullName
	$name = $r.File.BaseName
	$dir = if ($r.Demo) { $DemoDir } else { $GameDir }
	Set-Location $dir
	# the autotest sm1-07 and the hand-played SM1-07 differ only in case, so output names carry the folder (D-033)
	$folder = if ($r.Demo) { 'demo' } elseif ($r.Autotest) { 'autotest' } else { 'played' }
	$line = if ($r.Demo) { "$name (demo)" } elseif ($r.Autotest) { $name } else { "$name (played)" }
	$ok = $true
	$cfgs = $Configs
	if ($r.Demo -or ($r.Autotest -and ($CrossCheck -contains $name))) { $cfgs = @('r', 'd') }
	foreach ($e in $cfgs) {
		$trace = Join-Path $Out "${folder}_${name}_$e.trace"
		$log = Join-Path $Out "${folder}_${name}_$e.replay.log"
		Remove-Item $trace, "$rec.replay.log" -ErrorAction SilentlyContinue
		$flags = "-window -replay `"$rec`" -trace `"$trace`""
		if ($r.Autotest) { $flags = "-window -autotest -replay `"$rec`" -trace `"$trace`"" }
		Wait-NoGame
		$p = Start-Process ".\FS2vs2022$e.exe" -ArgumentList $flags -PassThru
		# FS2_PROCDUMP (path to Sysinternals procdump.exe): a crash leaves a full dump in <Out> (#74). Dumps hold game
		# data from memory, so they stay local: never commit or post one, only the stack read from it.
		$pd = $null
		if ($env:FS2_PROCDUMP -and (Test-Path $env:FS2_PROCDUMP)) {
			$dumps = @(Get-ChildItem $Out -Filter *.dmp -ErrorAction SilentlyContinue).Count
			$pd = Start-Process $env:FS2_PROCDUMP -ArgumentList "-accepteula -e -ma -o $($p.Id) `"$Out`"" -PassThru -WindowStyle Hidden
		}
		if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $line += " | $e TIMEOUT"; $ok = $false; continue }
		if ($pd) {
			if (-not $pd.WaitForExit(120000)) { Stop-Process $pd -Force }
			if (@(Get-ChildItem $Out -Filter *.dmp -ErrorAction SilentlyContinue).Count -gt $dumps) { $line += " | $e CRASH DUMP in $Out"; $ok = $false }
		}
		$res = 'NO LOG'
		if (Test-Path "$rec.replay.log") {
			Copy-Item "$rec.replay.log" $log
			$res = (Select-String $log -Pattern 'REPLAY OK|DESYNC|ERROR' | Select-Object -First 1).Line
		}
		$diff = 'NO TRACE'
		if (Test-Path $trace) { $diff = (& $td ([IO.Path]::ChangeExtension($rec, '.trace')) $trace | Select-Object -Last 1) }
		if (($res -notmatch 'REPLAY OK') -or ($diff -notmatch '^MATCH')) { $ok = $false }
		$line += " | $e exit=$($p.ExitCode) $res / $diff"
		# an autotest replay exits 0 only with no Assert, Int3 or Warning (D-029, D-077); a hand-played one exits 1 on success (D-033)
		if ($r.Autotest) {
			$counts = Select-String (Join-Path $dir 'autotest.log') -Pattern 'Asserts \d+, Int3 \d+, warnings \d+' -ErrorAction SilentlyContinue | Select-Object -Last 1
			if ($counts) { $line += " ($($counts.Matches[0].Value))" }
			if ($p.ExitCode -ne 0) { $ok = $false }
		}
	}
	if ($cfgs.Count -gt 1) {
		$hashes = $cfgs | ForEach-Object { $tf = Join-Path $Out "${folder}_${name}_$_.trace"; if (Test-Path $tf) { (Get-FileHash $tf).Hash } else { 'none' } }
		if (($hashes | Select-Object -Unique).Count -eq 1) { $line += ' | builds identical' } else { $line += ' | builds DIFFER'; $ok = $false }
	}
	if ($ok) { $pass++ } else { $fail++; $line = "FAIL $line" }
	$line | Add-Content $summary
	$line
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
