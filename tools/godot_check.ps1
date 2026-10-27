<#
Godot gate v1 (G2, D-20261022-143925): replays recordings through stock `godot --headless` + the fs2godot extension +
fs2game.dll, driven by godot\runner.gd with the same -headless -replay -trace arguments the native exe takes.
Pass = Godot exit 0, replay log REPLAY OK, TraceDiff MATCH against the native x64 trace of the same configuration
(and against the recorded trace). Exit codes as dll_check/autotest: 0 all pass, 1 any fail.

Usage: tools\godot_check.ps1 [-Config r|d] [-Set autotest|fs1|demo|fs1demo] [-Missions a,b] [-Quick] [-Out dir] [-NoNative] [-Fiber 1|stress]
-Quick  the per-commit quick set (14 custom + sm1-01 + sm3-06), taken from the 'custom' list below if present.
Needs: godot\bin\fs2godot.dll (cmake build, godot\README.md), Godot in C:\dev\Tools\godot_4.7.2, FS2game64<c>.dll,
FS2vs2022<c>64.exe, tools\bin\TraceDiff.exe. Also runs the restart check (two recordings in one process) and the forced-Error check (P1) through Godot.
#>
param(
	[string]$Config = 'r',
	[string]$Out,
	[string[]]$Missions,
	[string]$Set = 'autotest',
	[string]$GameDir = $env:PATH_FS2,
	[string]$Godot = 'C:\dev\Tools\godot_4.7.2\Godot_v4.7.2-stable_win64_console.exe',
	[switch]$Quick,
	[switch]$NoNative,
	[string]$Fiber	# '1' or 'stress': the game runs on a fiber in the Godot run (G3); the native baseline never does
)
$repo = Split-Path $PSScriptRoot
Remove-Item Env:FS2_FIBER -ErrorAction SilentlyContinue	# the native baseline must not inherit it
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
. (Join-Path $PSScriptRoot 'gate_sets.ps1')
$gs = Get-GateSet $Set $repo $GameDir
$run = $gs.Dir
if ($run -ne $GameDir) { Install-GateBuild $gs $GameDir $Config }
if (-not $Out) { $Out = Join-Path $repo ("replays\godot_" + $Config + '_' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
$dll = Join-Path $run "FS2game64$Config.dll"
$exe = ".\FS2vs2022${Config}64.exe"
$proj = Join-Path $repo 'godot'
foreach ($f in $td, $dll, $Godot, (Join-Path $proj 'bin\fs2godot.dll')) { if (-not (Test-Path $f)) { throw "Missing $f" } }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
if ($gs.CopyMissions) { Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $run 'data\missions') }
$summary = Join-Path $Out 'summary.txt'
Remove-Item $summary -ErrorAction SilentlyContinue

$Missions = $Missions | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
if ($Quick -and -not $Missions) {
	$Missions = @('sm1-01', 'sm3-06') + (Get-ChildItem (Join-Path $repo "recordings\$Set") -Filter *.fs2rec |
		ForEach-Object { $_.BaseName } | Where-Object { $_ -match '^Autotest' })
}
if (-not $Missions) { $Missions = $gs.Missions }
if (-not $Missions) { throw 'No missions' }

function Wait-NoGame { while (Get-Process FS2vs2022*, fs2run, Godot* -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 } }
function Verdict([string]$a, [string]$b) {
	if (-not (Test-Path $b)) { return 'NO TRACE' }
	$o = & $td $a $b
	$v = ($o | Select-String '^(MATCH|DIVERGED)' | Select-Object -First 1).Line
	if (-not $v) { $v = "NO VERDICT: $($o | Select-Object -Last 1)" }
	return $v
}
function Replay-Result([string]$rec, [string]$dest) {
	if (-not $gs.RecDir) { return Autotest-Result (Join-Path $run 'autotest.log') $dest }	# fs1demo rule
	$log = "$rec.replay.log"
	if (-not (Test-Path $log)) { return 'NO LOG' }
	Copy-Item $log $dest
	return (Select-String $dest -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line
}

if (-not (Test-Path (Join-Path $proj '.godot\extension_list.cfg'))) { & $Godot --headless --path $proj --import | Out-Null }
$good = if ($gs.RecDir) { '^REPLAY OK' } else { '^AUTOTEST PASS' }
Set-Location $run
$pass = 0; $fail = 0
foreach ($m in $Missions) {
	$rec = if ($gs.RecDir) { Join-Path $gs.RecDir "$m.fs2rec" } else { Join-Path $Out "$m.fs2rec" }	# fs1demo has none: the log name only
	$rtrace = if ($gs.RecDir) { Join-Path $gs.RecDir "$m.trace" } else { $null }
	$ntrace = Join-Path $Out "${m}_native.trace"
	$gtrace = Join-Path $Out "${m}_godot.trace"
	Remove-Item $ntrace, $gtrace, "$rec.replay.log", (Join-Path $run 'autotest.log') -ErrorAction SilentlyContinue
	$ok = $true
	$line = $m
	$flags = & $gs.Flags $m '%T'
	if (-not $NoNative) {
		Wait-NoGame
		$p = Start-Process $exe -ArgumentList ($flags -replace '%T', $ntrace) -PassThru; $null = $p.Handle
		if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $ncode = 'TIMEOUT' } else { $ncode = $p.ExitCode }
		$nres = Replay-Result $rec (Join-Path $Out "${m}_native.replay.log")
		$line += " | native exit=$ncode $nres"
		if (($ncode -ne 0) -or ($nres -notmatch $good)) { $ok = $false }
		Remove-Item "$rec.replay.log" -ErrorAction SilentlyContinue
	}
	Remove-Item (Join-Path $run 'autotest.log') -ErrorAction SilentlyContinue
	Wait-NoGame
	$env:FS2_DLL = $dll; $env:FS2_ROOT = $run; $env:FS2_CMD = ($flags -replace '%T', $gtrace)
	if ($Fiber) { $env:FS2_FIBER = $Fiber }
	$p = Start-Process $Godot -ArgumentList "--headless --path `"$proj`"" -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $Out "${m}_godot.out.txt") -RedirectStandardError (Join-Path $Out "${m}_godot.err.txt"); $null = $p.Handle
	if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $gcode = 'TIMEOUT' } else { $gcode = $p.ExitCode }
	Remove-Item Env:FS2_FIBER -ErrorAction SilentlyContinue
	$gres = Replay-Result $rec (Join-Path $Out "${m}_godot.replay.log")
	$line += " | godot exit=$gcode $gres"
	if (($gcode -ne 0) -or ($gres -notmatch $good)) { $ok = $false }
	if ($gs.RecDir -and -not $NoNative) {
		$v = Verdict $ntrace $gtrace
		$line += " / vs native: $v"
		if ($v -notmatch '^MATCH') { $ok = $false }
	}
	if (-not $gs.RecDir) { $line += " [$Fs1DemoRule]" }
	if ($rtrace) {
		$v = Verdict $rtrace $gtrace
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
Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD -ErrorAction SilentlyContinue
$launch = {
    param($c1, $c2, $pre)
    Wait-NoGame
    $env:FS2_DLL = $dll; $env:FS2_ROOT = $run; $env:FS2_CMD = $c1; $env:FS2_CMD2 = $c2
    if ($Fiber) { $env:FS2_FIBER = $Fiber }
    $p = Start-Process $Godot -ArgumentList "--headless --path `"$proj`"" -PassThru -NoNewWindow -RedirectStandardOutput "$pre.out.txt" -RedirectStandardError "$pre.err.txt"; $null = $p.Handle
    $code = if ($p.WaitForExit(1800000)) { $p.ExitCode } else { Stop-Process $p -Force; 'TIMEOUT' }
    Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD, Env:FS2_CMD2, Env:FS2_FIBER -ErrorAction SilentlyContinue
    return $code
}
foreach ($kind in 'restart', 'fatal') {
    $r = Test-HostRestart $gs $run $Out $td $launch $kind 'godot'
    $r | Add-Content $summary; $r
    if ($r -notmatch '^PASS') { $fail++ } else { $pass++ }
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
