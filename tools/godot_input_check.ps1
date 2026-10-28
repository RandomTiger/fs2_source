<#
Joystick and pad through Godot's own input (batch GodotPad). The game's scripted input tests (-at_input joy / pad, code\Replay\InputTest.h)
normally replace the host hooks. Here the host keeps its hooks: -at_input joyhost / padhost make the game hand the script to the Godot
host, which feeds it into Godot's Input as synthetic joypad events (device 0), and the game reads it back through the real Godot
hooks (joy_read, pad_poll). The same known-answer checks run on what comes back.

Per config and per test (joy, pad), in the gate folder:
  a  native x64 run with -at_input <joy|pad>: the log must say PASS, trace T_n
  b  stock `godot --headless` + fs2godot + FS2game64<c>.dll with -at_input <joy|pad>host, recording R: PASS, trace T_g
  c  each recording (native's and Godot's) replayed natively and in Godot: REPLAY OK, trace MATCHes the one it was recorded with
     (the two live traces are compared and logged but not scored: live frame timing follows the wall clock)

Usage: tools\godot_input_check.ps1 [-Config r|d] [-Out dir] [-GameDir dir] [-Tests joy,pad] [-Packaged]
-Packaged  run (b) and (d) through the packaged FS2Godot.exe in GameDir (godot_pkg_check copies it there) instead of the stock Godot.
Exit 0 all pass, 1 any fail. Needs the same builds as godot_check.ps1 and the AutotestJoy / AutotestPilot files in GameDir.
#>
param(
	[string]$Config = 'r',
	[string]$Out,
	[string]$GameDir = $env:PATH_FS2,
	[string]$Godot,	# default: Godot_v4.7.2-stable_win64_console.exe in $env:GODOT_ENGINE_4_7,
	[string[]]$Tests = @('joy', 'pad'),
	[switch]$Packaged
)
$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Godot -and -not $Packaged) { if (-not $env:GODOT_ENGINE_4_7) { throw 'GODOT_ENGINE_4_7 is not set, and no -Godot given' }; $Godot = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64_console.exe' }
if (-not $Out) { $Out = Join-Path $repo ("replays\godot_input_" + $Config + '_' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
$dll = Join-Path $GameDir "FS2game64$Config.dll"
$exe = Join-Path $GameDir "FS2vs2022${Config}64.exe"
$proj = Join-Path $repo 'godot'
$pkg = Join-Path $GameDir 'FS2Godot.exe'
$need = @($td, $dll, $exe, (Join-Path $proj 'bin\fs2godot.dll'))
if ($Packaged) { $need += $pkg } else { $need += $Godot }
foreach ($f in $need) { if (-not (Test-Path $f)) { throw "Missing $f" } }
$Tests = $Tests | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
Copy-Item (Join-Path $repo 'data\missions\Autotest*.fs2') (Join-Path $GameDir 'data\missions') -Force
if (-not $Packaged -and -not (Test-Path (Join-Path $proj '.godot\extension_list.cfg'))) { & $Godot --headless --path $proj --import | Out-Null }
$summary = Join-Path $Out 'summary.txt'
Remove-Item $summary -ErrorAction SilentlyContinue

function Wait-NoGame { while (Get-Process FS2vs2022*, fs2run, Godot*, FS2Godot* -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 } }
function Verdict([string]$a, [string]$b) {
	if (-not ((Test-Path $a) -and (Test-Path $b))) { return 'NO TRACE' }
	$o = & $td $a $b
	$v = ($o | Select-String '^(MATCH|DIVERGED)' | Select-Object -First 1).Line
	if (-not $v) { $v = "NO VERDICT: $($o | Select-Object -Last 1)" }
	return $v
}
function Run-Native([string]$cmd) {
	Wait-NoGame
	$p = Start-Process $exe -ArgumentList $cmd -PassThru; $null = $p.Handle
	if (-not $p.WaitForExit(900000)) { Stop-Process $p -Force; return 'TIMEOUT' }
	return $p.ExitCode
}
function Run-Godot([string]$cmd, [string]$pre) {
	Wait-NoGame
	$env:FS2_DLL = $dll; $env:FS2_CMD = $cmd
	if ($Packaged) {	# as shipped: run from the data folder, no root given (the launcher takes the exe's folder)
		$p = Start-Process $pkg -ArgumentList "--headless" -WorkingDirectory $GameDir -PassThru -NoNewWindow -RedirectStandardOutput "$pre.out.txt" -RedirectStandardError "$pre.err.txt"
	} else {
		$env:FS2_ROOT = $GameDir
		$p = Start-Process $Godot -ArgumentList "--headless --path `"$proj`"" -PassThru -NoNewWindow -RedirectStandardOutput "$pre.out.txt" -RedirectStandardError "$pre.err.txt"
	}
	$null = $p.Handle
	$code = if ($p.WaitForExit(900000)) { $p.ExitCode } else { Stop-Process $p -Force; 'TIMEOUT' }
	Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD -ErrorAction SilentlyContinue
	return $code
}
function Script-Result([string]$what) {	# the script's own PASS line in autotest.log
	$log = Join-Path $GameDir 'autotest.log'
	if (-not (Test-Path $log)) { return 'NO LOG' }
	$l = Select-String $log -Pattern "^\[[ \d.]+\] $what script: \d+ checks, \d+ failed: (PASS|FAIL)" | Select-Object -Last 1
	if (-not $l) { return 'NO SCRIPT RESULT' }
	return $l.Line.Substring($l.Line.IndexOf(']') + 2)
}
function Log-Counts() {
	$c = Select-String (Join-Path $GameDir 'autotest.log') -Pattern 'Asserts \d+, Int3 \d+, warnings \d+' -ErrorAction SilentlyContinue | Select-Object -Last 1
	if ($c) { return $c.Matches[0].Value } else { return '' }
}

Set-Location $GameDir
$fail = 0; $pass = 0
foreach ($t in $Tests) {
	$name = if ($t -eq 'joy') { 'Joy' } else { 'Pad' }
	$mission = "Autotest$name"
	$word = if ($t -eq 'joy') { 'Joystick' } else { 'Pad' }
	$base = "-window -autotest -headless -at_mission $mission -at_pilot AutotestPilot -at_time 20"
	$tn = Join-Path $Out "${t}_native.trace"; $tg = Join-Path $Out "${t}_godot.trace"
	$recN = Join-Path $Out "${t}_native.fs2rec"; $recG = Join-Path $Out "${t}_godot.fs2rec"
	$ok = $true; $line = "$mission ($Config)"
	# the game never overwrites an output file, so start clean
	Remove-Item $tn, $tg, $recN, $recG, "$recN.record.log", "$recG.record.log" -ErrorAction SilentlyContinue

	# a: native, the script replaces the hooks (live, recorded)
	Remove-Item (Join-Path $GameDir 'autotest.log') -ErrorAction SilentlyContinue
	$code = Run-Native "$base -at_input $t -record `"$recN`" -trace `"$tn`""
	$res = Script-Result $word
	Copy-Item (Join-Path $GameDir 'autotest.log') (Join-Path $Out "${t}_native.autotest.log") -ErrorAction SilentlyContinue
	$line += " | native exit=$code $res"
	if (($code -ne 0) -or ($res -notmatch 'PASS$')) { $ok = $false }

	# b: Godot, the script goes in as synthetic joypad events and is read back through Godot's Input (live, recorded)
	Remove-Item (Join-Path $GameDir 'autotest.log') -ErrorAction SilentlyContinue
	$code = Run-Godot "$base -at_input ${t}host -record `"$recG`" -trace `"$tg`"" (Join-Path $Out "${t}_godot")
	$res = Script-Result $word
	Copy-Item (Join-Path $GameDir 'autotest.log') (Join-Path $Out "${t}_godot.autotest.log") -ErrorAction SilentlyContinue
	$line += " | godot exit=$code $res $(Log-Counts)"
	if (($code -ne 0) -or ($res -notmatch 'PASS$')) { $ok = $false }

	# the two live runs are only logged, not scored: their frame timing follows the wall clock, so mission time can differ by a
	# frame or two on a loaded machine with identical input (seen natively against natively too)
	$line += " / live native vs godot (not scored): $(Verdict $tn $tg)"

	# c: each recording replays, natively and in Godot, to the trace it was recorded with (the deterministic comparison)
	foreach ($r in @(@('native', $recN, $tn), @('godot', $recG, $tg))) {
		foreach ($host_ in 'native', 'godot') {
			$out_ = Join-Path $Out "${t}_$($r[0])rec_replay_$host_.trace"
			Remove-Item $out_, "$($r[1]).replay.log" -ErrorAction SilentlyContinue
			$cmd = "-window -autotest -headless -replay `"$($r[1])`" -trace `"$out_`""
			$code = if ($host_ -eq 'native') { Run-Native $cmd } else { Run-Godot $cmd (Join-Path $Out "${t}_$($r[0])rec_replay_godot") }
			$rr = if (Test-Path "$($r[1]).replay.log") { (Select-String "$($r[1]).replay.log" -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line } else { 'NO LOG' }
			$v = Verdict $r[2] $out_
			$line += " | $($r[0]) rec in $host_ exit=$code $rr / $v"
			if (($code -ne 0) -or ($rr -notmatch '^REPLAY OK') -or ($v -notmatch '^MATCH')) { $ok = $false }
		}
	}
	if ($Config -eq 'd' -and -not (Select-String (Join-Path $GameDir 'autotest.log') -Pattern 'guards: assert' -Quiet)) { $ok = $false; $line += ' GUARDS NOT ASSERTING' }
	if ($ok) { $pass++ } else { $fail++; $line = "FAIL $line" }
	$line | Add-Content $summary
	$line
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
