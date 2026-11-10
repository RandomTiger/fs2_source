<#
Replays the checked-in recordings and compares each replay's trace with the recorded one.

  recordings\autotest\*.fs2rec   replayed with -autotest
  recordings\played\*.fs2rec     hand-played recordings, replayed without it
  recordings\demo\*.fs2rec       the FreeSpace 2 demo set, replayed with -autotest in the demo folder (-DemoDir,
                                 default $env:PATH_FS2DEMO) in Release and Debug, whose traces must be identical; with no
                                 demo folder they are skipped (SKIP), never failed (D-20261018-221705)
  recordings\fs1\*.fs2rec        FreeSpace 1 missions, replayed with -autotest in -Fs1Dir ($env:PATH_FS1; skipped when unset)

Usage (PowerShell, from anywhere):
  tools\replay_check.ps1 [-Out <dir>] [-Missions sm1-01,SM1-04,...] [-Configs r] [-CrossCheck sm1-01] [-GameDir <dir>] [-Fs1Dir <dir>]

-Missions names are matched without case against both folders, so sm1-06 runs the autotest and the played SM1-06.
The game directory defaults to $env:PATH_FS2, where the build puts FS2_<Layer>_r.exe / _d.exe (tools\layer.ps1).
Results go to <Out>\summary.txt, one line per recording; <Out> defaults to replays\check_<date-time>.
Every recording replays in Release (-Configs). The -CrossCheck missions (default sm1-01) also replay in Debug,
and their Release and Debug traces must be byte-identical: on 2026-10-13 every recording gave identical traces in
both builds, and Debug-made recordings replayed in Release, so one mission is enough to catch the builds drifting.
With $env:FS2_PROCDUMP set to Sysinternals procdump.exe (downloaded per machine, never committed), each run is watched
and a crash leaves a dump in <Out>; dumps stay local (they hold game data). With or without it, a crash copies the exe,
its pdb and the game folder's DLLs, with their MD5s, to <Out>\crash_<folder>_<mission>_<config> before anything rebuilds.
A line passes when every run says "REPLAY OK" and "MATCH", and every autotest run exits 0 (no Assert, Int3 or Warning). Build both configs first, and don't
build or run the game from another session while this runs: only one copy of the game can run.
#>
param(
	[string]$Out,
	[string[]]$Missions,
	[string[]]$Configs = @('r'),
	[string[]]$CrossCheck = @('sm1-01'),
	[string]$GameDir = $env:PATH_FS2,
	[string]$DemoDir = $env:PATH_FS2DEMO,
	[string]$Fs1Dir = $env:PATH_FS1,
	[switch]$Headless	# autotest replays also get -headless (null renderer, no sound device; FS1 sets run at GR_640, #101)
)

$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"	# exe names (D-20261101-064106)
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Out) { $Out = Join-Path $repo ("replays\check_" + (Get-Date -Format 'yyyyMMdd_HHmm')) }
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
New-Item -ItemType Directory -Force $Out | Out-Null
# the test missions the autotest recordings play
Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $GameDir 'data\missions')
$demoOk = $DemoDir -and (Test-Path (Join-Path $DemoDir (Get-FS2Exe r)))
$summary = Join-Path $Out 'summary.txt'

$recs = @()
$recs += Get-ChildItem (Join-Path $repo 'recordings\autotest') -Filter *.fs2rec -ErrorAction SilentlyContinue | ForEach-Object { @{ File = $_; Autotest = $true; Folder = 'autotest'; Dir = $GameDir } }
# either folder may be empty, and git does not keep an empty folder
$recs += Get-ChildItem (Join-Path $repo 'recordings\played') -Filter *.fs2rec -ErrorAction SilentlyContinue | ForEach-Object { @{ File = $_; Autotest = $false; Folder = 'played'; Dir = $GameDir } }
# FS1 autotest recordings replay in the FS1 install, with the exes found there (docs/FS1_COMPAT.md)
if ($Fs1Dir) {
	$recs += Get-ChildItem (Join-Path $repo 'recordings\fs1') -Filter *.fs2rec -ErrorAction SilentlyContinue | ForEach-Object { @{ File = $_; Autotest = $true; Folder = 'fs1'; Dir = $Fs1Dir } }
}
$demoRecs = @(Get-ChildItem (Join-Path $repo 'recordings\demo') -Filter *.fs2rec -ErrorAction SilentlyContinue | ForEach-Object { @{ File = $_; Autotest = $true; Demo = $true; Folder = 'demo'; Dir = $DemoDir } })
# powershell -File passes "a,b" as one string
$Missions = $Missions | ForEach-Object { $_ -split ',' }
$Configs = $Configs | ForEach-Object { $_ -split ',' }
$CrossCheck = $CrossCheck | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
if ($Missions) { $recs = $recs | Where-Object { $Missions -contains $_.File.BaseName }; $demoRecs = @($demoRecs | Where-Object { $Missions -contains $_.File.BaseName }) }
if ($demoRecs -and -not $demoOk) {
	foreach ($r in $demoRecs) { "SKIP $($r.File.BaseName) (demo): no demo test folder with $(Get-FS2Exe r) (-DemoDir or PATH_FS2DEMO)" | Tee-Object -FilePath $summary -Append }
	$demoRecs = @()
}
$recs = @($recs) + $demoRecs
if (-not $recs) { throw "No recordings match: $Missions" }

# One copy of the game at a time: a second one closes straight away and writes no trace
function Wait-NoGame {
	# Gate pause (#214, D-20261107-094737): while %TEMP%\fs2_gate.pause exists, hold before the next launch; resume
	# only when the marker is gone and no game runs, checked together. %TEMP%\fs2_gate.paused (time, runner, PID)
	# confirms a hold; it never stops or changes a run, and nothing reads it to decide anything.
	$pause = "$env:TEMP\fs2_gate.pause"
	$paused = "$env:TEMP\fs2_gate.paused"
	$held = $null
	do {
		while (Test-Path -LiteralPath $pause) {
			if (-not $held) {
				$held = Get-Date; $next = $held.AddMinutes(10)
				try { Set-Content -LiteralPath $paused -Value "$($held.ToString('o')) replay_check $PID" -Encoding ascii -ErrorAction Stop } catch { }
				Write-Host "Gate pause: $pause found at $($held.ToString('HH:mm:ss')); replay_check holds before its next launch (PID $PID). Delete that file to carry on."
			} elseif ((Get-Date) -ge $next) {
				$next = $next.AddMinutes(10)
				Write-Host "Gate pause: still held by $pause at $(Get-Date -Format 'HH:mm:ss'), $([int]((Get-Date) - $held).TotalMinutes) min so far (replay_check, PID $PID). Delete that file to carry on."
			}
			Start-Sleep -Seconds 2
		}
		while (Get-Process $FS2ProcessNames -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
	} while (Test-Path -LiteralPath $pause)
	if ($held) {
		try { if (((Get-Content -LiteralPath $paused -TotalCount 1 -ErrorAction Stop) -split ' ')[-1] -eq "$PID") { Remove-Item -LiteralPath $paused -ErrorAction Stop } } catch { }
		Write-Host "Gate pause: $pause gone at $(Get-Date -Format 'HH:mm:ss'); replay_check carries on after $('{0:0.0}' -f ((Get-Date) - $held).TotalMinutes) min held."
	}
}

$pass = 0; $fail = 0
$nonref = @()	# recordings not made in the reference configuration (D-056)
foreach ($r in $recs) {
	$rec = $r.File.FullName
	$name = $r.File.BaseName
	$dir = $r.Dir
	Set-Location $dir
	# the autotest sm1-07 and the hand-played SM1-07 differ only in case, so output names carry the folder (D-033)
	$folder = $r.Folder
	$line = if ($folder -eq 'autotest') { $name } else { "$name ($folder)" }
	$ok = $true
	$cfgs = $Configs
	if ($r.Demo -or ($r.Autotest -and ($CrossCheck -contains $name))) { $cfgs = @('r', 'd') }
	foreach ($e in $cfgs) {
		$trace = Join-Path $Out "${folder}_${name}_$e.trace"
		$log = Join-Path $Out "${folder}_${name}_$e.replay.log"
		Remove-Item $trace, "$rec.replay.log" -ErrorAction SilentlyContinue
		$flags = "-window -replay `"$rec`" -trace `"$trace`""
		if ($r.Autotest) { $flags = "-window -autotest -replay `"$rec`" -trace `"$trace`"" }
		if ($Headless -and $r.Autotest) { $flags += ' -headless' }
		Wait-NoGame
		$p = Start-Process ".\$(Get-FS2Exe $e)" -ArgumentList $flags -PassThru
		# FS2_PROCDUMP (path to Sysinternals procdump.exe): a crash leaves a full dump in <Out> (#74). Dumps hold game
		# data from memory, so they stay local: never commit or post one, only the stack read from it.
		$pd = $null
		if ($env:FS2_PROCDUMP -and (Test-Path $env:FS2_PROCDUMP)) {
			$dumps = @(Get-ChildItem $Out -Filter *.dmp -ErrorAction SilentlyContinue).Count
			$pd = Start-Process $env:FS2_PROCDUMP -ArgumentList "-accepteula -e -ma -o $($p.Id) `"$Out`"" -PassThru -WindowStyle Hidden
		}
		if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $line += " | $e TIMEOUT"; $ok = $false; continue }
		# A crash (NTSTATUS exit, e.g. 0xC0000374) keeps the exe, its pdb and the game folder's DLLs, with MD5s, before any
		# later build can overwrite them (D-20261107-193458: the 2026-11-07 Enhanced crash exe was lost to a rebuild).
		# ($null -lt 0 is true in PowerShell: a launch that failed has no exit code and is not a crash)
		if (($null -ne $p.ExitCode) -and ($p.ExitCode -lt 0)) {
			$keep = Join-Path $Out "crash_${folder}_${name}_$e"
			New-Item -ItemType Directory -Force $keep | Out-Null
			$exe = Get-FS2Exe $e
			$bins = @(".\$exe", ".\$([IO.Path]::ChangeExtension($exe, '.pdb'))") + @(Get-ChildItem . -Filter *.dll | ForEach-Object { $_.FullName })
			$bins | Where-Object { Test-Path $_ } | ForEach-Object { Copy-Item $_ $keep; "$((Get-FileHash $_ -Algorithm MD5).Hash)  $(Split-Path $_ -Leaf)" } | Set-Content (Join-Path $keep 'md5.txt')
			$line += " | $e CRASH 0x$('{0:X8}' -f $p.ExitCode) binaries kept in $keep"; $ok = $false
		}
		if ($pd) {
			if (-not $pd.WaitForExit(120000)) { Stop-Process $pd -Force }
			if (@(Get-ChildItem $Out -Filter *.dmp -ErrorAction SilentlyContinue).Count -gt $dumps) { $line += " | $e CRASH DUMP in $Out"; $ok = $false }
		}
		$res = 'NO LOG'
		if (Test-Path "$rec.replay.log") {
			Copy-Item "$rec.replay.log" $log
			$res = (Select-String $log -Pattern 'REPLAY OK|DESYNC|ERROR' | Select-Object -First 1).Line
			if ((Select-String $log -Pattern 'Not the reference configuration' -Quiet)) { $nonref += $name }
		}
		# the game's autotest.log, which says where a stuck run hung (D-053)
		if (Test-Path '.\autotest.log') { Copy-Item '.\autotest.log' (Join-Path $Out "${folder}_${name}_$e.autotest.log") }
		$diff = 'NO TRACE'
		if (Test-Path $trace) { $diff = (& $td ([IO.Path]::ChangeExtension($rec, '.trace')) $trace | Select-Object -Last 1) }
		# TraceDiff exits 2 when it cannot read either trace (missing, not a trace, other version): a FAIL, never a pass (D-20261021-125813)
		if ((Test-Path $trace) -and ($LASTEXITCODE -eq 2)) { $diff = 'TRACE UNREADABLE (TraceDiff exit 2: missing recorded trace, or another trace version)' }
		if (("$res" -notmatch 'REPLAY OK') -or ("$diff" -notmatch '^MATCH')) { $ok = $false }
		$line += " | $e exit=$($p.ExitCode) $res / $diff"
		# an autotest replay exits 0 only with no Assert, Int3 or Warning (D-029, D-077); a hand-played one exits 1 on success (D-033)
		if ($r.Autotest) {
			$counts = Select-String (Join-Path $dir 'autotest.log') -Pattern 'Asserts \d+, Int3 \d+, warnings \d+' -ErrorAction SilentlyContinue | Select-Object -Last 1
			if ($counts) { $line += " ($($counts.Matches[0].Value))" }
			if ($p.ExitCode -ne 0) { $ok = $false }
			if ($Headless -and -not (Select-String (Join-Path $dir 'autotest.log') -Pattern 'No graphics device' -Quiet)) { $ok = $false; $line += ' NOT HEADLESS' }
			# a Debug build must prove its array guards fire (D-20261018-030012)
			if (($e -eq 'd') -and -not (Select-String (Join-Path $dir 'autotest.log') -Pattern 'guards: assert' -Quiet)) { $ok = $false; $line += ' GUARDS NOT ASSERTING' }
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
$done = "DONE pass=$pass fail=$fail"
if ($nonref) { $done += " (not the reference configuration: $(($nonref | Select-Object -Unique) -join ', '))" }
$done | Add-Content $summary
$done
if ($fail) { exit 1 }
