<#
Replays every recording through fs2game.dll with fs2run.exe (no Godot) and compares each trace with the native x64
exe's trace of the same configuration (P5, D-20261022-143922 iii). Both run headless (-headless, no window, null
renderer, no sound device). A recording passes when the native run and the DLL run both exit 0, both replay logs say
REPLAY OK, and TraceDiff exits 0 comparing the two traces. TraceDiff also compares the DLL trace with the recorded one.

Usage: tools\dll_check.ps1 [-Config r|d] [-Out <dir>] [-Missions a,b,...] [-Set autotest|fs1|demo|fs1demo] [-GameDir <dir>] [-NoNative]

-Config   r (Release, the default) or d (Debug): selects FS2vs2022<c>64.exe and FS2game64<c>.dll in the game dir.
-Set      the folder under recordings\ (default autotest).
-NoNative skip the native run and compare with the recorded trace only (quick; not the gate).
Build first: tools\build_dll.ps1 or MSBuild fs2game\fs2game.vcxproj and tools\fs2run\fs2run.vcxproj (x64), and
Freespace2.vcxproj x64 for the native exe, plus TraceDiff. Results: <Out>\summary.txt, exit 1 on any fail.
#>
param(
	[string]$Config = 'r',
	[string]$Out,
	[string[]]$Missions,
	[string]$Set = 'autotest',
	[string]$GameDir = $env:PATH_FS2,
	[switch]$NoNative
)
$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
. (Join-Path $PSScriptRoot 'gate_sets.ps1')
$gs = Get-GateSet $Set $repo $GameDir
$run = $gs.Dir
if ($run -ne $GameDir) { Install-GateBuild $gs $GameDir $Config }
if (-not $Out) { $Out = Join-Path $repo ("replays\dll_" + $Config + '_' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
$runner = Join-Path $repo 'tools\bin\fs2run.exe'
$dll = Join-Path $run "FS2game64$Config.dll"
$exe = ".\FS2vs2022${Config}64.exe"
foreach ($f in $td, $runner, $dll) { if (-not (Test-Path $f)) { throw "Missing $f" } }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
if ($gs.CopyMissions) { Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $run 'data\missions') }
$summary = Join-Path $Out 'summary.txt'
Remove-Item $summary -ErrorAction SilentlyContinue

$Missions = $Missions | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
if (-not $Missions) { $Missions = $gs.Missions }
if (-not $Missions) { throw 'No missions' }

function Wait-NoGame { while (Get-Process FS2vs2022*, fs2run -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 } }

function Verdict([string]$a, [string]$b) {
	if (-not (Test-Path $b)) { return 'NO TRACE' }
	$o = & $td $a $b
	$v = ($o | Select-String '^(MATCH|DIVERGED)' | Select-Object -First 1).Line
	if (-not $v) { $v = "NO VERDICT: $($o | Select-Object -Last 1)" }
	return $v
}

# the replay log is written next to the recording; give back its verdict line
function Replay-Result([string]$rec, [string]$dest) {
	$log = "$rec.replay.log"
	if (-not (Test-Path $log)) { return 'NO LOG' }
	Copy-Item $log $dest
	return (Select-String $dest -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line
}

Set-Location $run
$pass = 0; $fail = 0
foreach ($m in $Missions) {
	$rec = if ($gs.RecDir) { Join-Path $gs.RecDir "$m.fs2rec" } else { Join-Path $Out "$m.fs2rec" }	# fs1demo has none: the log name only
	$rtrace = if ($gs.RecDir) { Join-Path $gs.RecDir "$m.trace" } else { $null }
	$ntrace = Join-Path $Out "${m}_native.trace"
	$dtrace = Join-Path $Out "${m}_dll.trace"
	Remove-Item $ntrace, $dtrace, "$rec.replay.log" -ErrorAction SilentlyContinue
	$ok = $true
	$line = $m
	$flags = & $gs.Flags $m '%T'
	if (-not $NoNative) {
		Wait-NoGame
		$p = Start-Process $exe -ArgumentList ($flags -replace '%T', $ntrace) -PassThru
		if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $ncode = 'TIMEOUT' } else { $ncode = $p.ExitCode }
		$nres = Replay-Result $rec (Join-Path $Out "${m}_native.replay.log")
		$line += " | native exit=$ncode $nres"
		if (($ncode -ne 0) -or ($gs.RecDir -and ($nres -notmatch '^REPLAY OK'))) { $ok = $false }
		Remove-Item "$rec.replay.log" -ErrorAction SilentlyContinue
	}
	Wait-NoGame
	$p = Start-Process $runner -ArgumentList "`"$dll`" `"$run`" `"$($flags -replace '%T', $dtrace -replace '"', '\"')`"" -PassThru -NoNewWindow; $null = $p.Handle
	if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $dcode = 'TIMEOUT' } else { $dcode = $p.ExitCode }
	$dres = Replay-Result $rec (Join-Path $Out "${m}_dll.replay.log")
	$line += " | dll exit=$dcode $dres"
	if (($dcode -ne 0) -or ($gs.RecDir -and ($dres -notmatch '^REPLAY OK'))) { $ok = $false }
	if (-not $NoNative) {
		$v = Verdict $ntrace $dtrace
		$line += " / vs native: $v"
		if ($v -notmatch '^MATCH') { $ok = $false }
	}
	if ($rtrace) {
		$v = Verdict $rtrace $dtrace
		$line += " / vs recorded: $v"
		if ($v -notmatch '^MATCH') { $ok = $false }
	}
	$counts = Select-String (Join-Path $run 'autotest.log') -Pattern 'Asserts \d+, Int3 \d+, warnings \d+' -ErrorAction SilentlyContinue | Select-Object -Last 1
	if ($counts) { $line += " ($($counts.Matches[0].Value))" }
	if (($Config -eq 'd') -and -not (Select-String (Join-Path $run 'autotest.log') -Pattern 'guards: assert' -Quiet)) { $ok = $false; $line += ' GUARDS NOT ASSERTING' }
	if ($ok) { $pass++ } else { $fail++; $line = "FAIL $line" }
	$line | Add-Content $summary
	$line
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
