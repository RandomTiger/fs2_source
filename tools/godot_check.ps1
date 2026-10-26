<#
Godot gate v1 (G2, D-20261022-143925): replays recordings through stock `godot --headless` + the fs2godot extension +
fs2game.dll, driven by godot\runner.gd with the same -headless -replay -trace arguments the native exe takes.
Pass = Godot exit 0, replay log REPLAY OK, TraceDiff MATCH against the native x64 trace of the same configuration
(and against the recorded trace). Exit codes as dll_check/autotest: 0 all pass, 1 any fail.

Usage: tools\godot_check.ps1 [-Config r|d] [-Set autotest|fs1|...] [-Missions a,b] [-Quick] [-Out dir] [-NoNative]
-Quick  the per-commit quick set (14 custom + sm1-01 + sm3-06), taken from the 'custom' list below if present.
Needs: godot\bin\fs2godot.dll (cmake build, godot\README.md), Godot in C:\dev\Tools\godot_4.7.2, FS2game64<c>.dll,
FS2vs2022<c>64.exe, tools\bin\TraceDiff.exe. Restart check (two recordings in one process) is reported "not yet run".
#>
param(
	[string]$Config = 'r',
	[string]$Out,
	[string[]]$Missions,
	[string]$Set = 'autotest',
	[string]$GameDir = $env:PATH_FS2,
	[string]$Godot = 'C:\dev\Tools\godot_4.7.2\Godot_v4.7.2-stable_win64_console.exe',
	[switch]$Quick,
	[switch]$NoNative
)
$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Out) { $Out = Join-Path $repo ("replays\godot_" + $Config + '_' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
$dll = Join-Path $GameDir "FS2game64$Config.dll"
$exe = ".\FS2vs2022${Config}64.exe"
$proj = Join-Path $repo 'godot'
foreach ($f in $td, $dll, $Godot, (Join-Path $proj 'bin\fs2godot.dll')) { if (-not (Test-Path $f)) { throw "Missing $f" } }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $GameDir 'data\missions')
$summary = Join-Path $Out 'summary.txt'
Remove-Item $summary -ErrorAction SilentlyContinue

$Missions = $Missions | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
if ($Quick -and -not $Missions) {
	$Missions = @('sm1-01', 'sm3-06') + (Get-ChildItem (Join-Path $repo "recordings\$Set") -Filter *.fs2rec |
		ForEach-Object { $_.BaseName } | Where-Object { $_ -notmatch '^sm\d' })
}
if (-not $Missions) { $Missions = Get-ChildItem (Join-Path $repo "recordings\$Set") -Filter *.fs2rec | ForEach-Object { $_.BaseName } }
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
	$log = "$rec.replay.log"
	if (-not (Test-Path $log)) { return 'NO LOG' }
	Copy-Item $log $dest
	return (Select-String $dest -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line
}

if (-not (Test-Path (Join-Path $proj '.godot\extension_list.cfg'))) { & $Godot --headless --path $proj --import | Out-Null }
Set-Location $GameDir
$pass = 0; $fail = 0
foreach ($m in $Missions) {
	$rec = Join-Path $repo "recordings\$Set\$m.fs2rec"
	$rtrace = Join-Path $repo "recordings\$Set\$m.trace"
	$ntrace = Join-Path $Out "${m}_native.trace"
	$gtrace = Join-Path $Out "${m}_godot.trace"
	Remove-Item $ntrace, $gtrace, "$rec.replay.log" -ErrorAction SilentlyContinue
	$ok = $true
	$line = $m
	$flags = "-window -autotest -headless -replay `"$rec`" -trace `"%T`""
	if (-not $NoNative) {
		Wait-NoGame
		$p = Start-Process $exe -ArgumentList ($flags -replace '%T', $ntrace) -PassThru; $null = $p.Handle
		if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $ncode = 'TIMEOUT' } else { $ncode = $p.ExitCode }
		$nres = Replay-Result $rec (Join-Path $Out "${m}_native.replay.log")
		$line += " | native exit=$ncode $nres"
		if (($ncode -ne 0) -or ($nres -notmatch '^REPLAY OK')) { $ok = $false }
		Remove-Item "$rec.replay.log" -ErrorAction SilentlyContinue
	}
	Wait-NoGame
	$env:FS2_DLL = $dll; $env:FS2_ROOT = $GameDir; $env:FS2_CMD = ($flags -replace '%T', $gtrace)
	$p = Start-Process $Godot -ArgumentList "--headless --path `"$proj`"" -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $Out "${m}_godot.out.txt") -RedirectStandardError (Join-Path $Out "${m}_godot.err.txt"); $null = $p.Handle
	if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $gcode = 'TIMEOUT' } else { $gcode = $p.ExitCode }
	$gres = Replay-Result $rec (Join-Path $Out "${m}_godot.replay.log")
	$line += " | godot exit=$gcode $gres"
	if (($gcode -ne 0) -or ($gres -notmatch '^REPLAY OK')) { $ok = $false }
	if (-not $NoNative) {
		$v = Verdict $ntrace $gtrace
		$line += " / vs native: $v"
		if ($v -notmatch '^MATCH') { $ok = $false }
	}
	$v = Verdict $rtrace $gtrace
	$line += " / vs recorded: $v"
	if ($v -notmatch '^MATCH') { $ok = $false }
	$counts = Select-String (Join-Path $GameDir 'autotest.log') -Pattern 'Asserts \d+, Int3 \d+, warnings \d+' -ErrorAction SilentlyContinue | Select-Object -Last 1
	if ($counts) { $line += " ($($counts.Matches[0].Value))" }
	if (($Config -eq 'd') -and -not (Select-String (Join-Path $GameDir 'autotest.log') -Pattern 'guards: assert' -Quiet)) { $ok = $false; $line += ' GUARDS NOT ASSERTING' }
	if ($ok) { $pass++ } else { $fail++; $line = "FAIL $line" }
	$line | Add-Content $summary
	$line
}
Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD -ErrorAction SilentlyContinue
$r = "restart check (two recordings in one process): not yet run (needs P2)"
$r | Add-Content $summary; $r
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
