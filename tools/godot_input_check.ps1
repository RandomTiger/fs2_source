<#
Joystick and pad through Godot's own input (batch GodotPad). The game's scripted input tests (-at_input joy / pad, code\Replay\InputTest.h)
normally replace the host hooks. Here the host keeps its hooks: -at_input joyhost / padhost make the game hand the script to the Godot
host, which feeds it into Godot's Input as synthetic joypad events (device 0), and the game reads it back through the real Godot
hooks (joy_read, pad_poll). The same known-answer checks run on what comes back.

Per config and per test (joy, pad), in the gate folder:
  a  native x64 run with -at_input <joy|pad>: the log must say PASS, trace T_n
  b  stock `godot --headless` + fs2godot + FS2game64<c>.dll with -at_input <joy|pad>host, recording R: PASS, trace T_g
  c  TraceDiff T_n vs T_g must MATCH
  d  R replayed natively and in Godot: REPLAY OK, both traces MATCH the recorded one

Usage: tools\godot_input_check.ps1 [-Config r|d] [-Out dir] [-GameDir dir] [-Tests joy,pad] [-Packaged]
-Packaged  run (b) and (d) through the packaged FS2Godot.exe in GameDir (godot_pkg_check copies it there) instead of the stock Godot.
Exit 0 all pass, 1 any fail. Needs the same builds as godot_check.ps1 and the AutotestJoy / AutotestPilot files in GameDir.
#>
param(
	[string]$Config = 'r',
	[string]$Out,
	[string]$GameDir = $env:PATH_FS2,
	[string]$Godot = 'C:\dev\Tools\godot_4.7.2\Godot_v4.7.2-stable_win64_console.exe',
	[string[]]$Tests = @('joy', 'pad'),
	[switch]$Packaged
)
$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
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
	$rec = Join-Path $Out "$t.fs2rec"
	$ok = $true; $line = "$mission ($Config)"

	# a, b, c are live runs, so their frame timing follows the wall clock: on a loaded machine a native and a Godot run can differ in
	# mission time without any input difference. One repeat of the whole a-b-c is allowed; both attempts are logged.
	for ($try = 1; $try -le 2; $try++) {
		$ok = $true; $line = "$mission ($Config)"
		# a: native, the script replaces the hooks
		Remove-Item (Join-Path $GameDir 'autotest.log') -ErrorAction SilentlyContinue
		$code = Run-Native "$base -at_input $t -record `"$(Join-Path $Out "${t}_native.fs2rec")`" -trace `"$tn`""
		$res = Script-Result $word
		Copy-Item (Join-Path $GameDir 'autotest.log') (Join-Path $Out "${t}_native.autotest.log") -ErrorAction SilentlyContinue
		$line += " | native exit=$code $res"
		if (($code -ne 0) -or ($res -notmatch 'PASS$')) { $ok = $false }

		# b: Godot, the script goes in as synthetic joypad events and is read back through Godot's Input
		Remove-Item (Join-Path $GameDir 'autotest.log'), "$rec.record.log" -ErrorAction SilentlyContinue
		$code = Run-Godot "$base -at_input ${t}host -record `"$rec`" -trace `"$tg`"" (Join-Path $Out "${t}_godot")
		$res = Script-Result $word
		Copy-Item (Join-Path $GameDir 'autotest.log') (Join-Path $Out "${t}_godot.autotest.log") -ErrorAction SilentlyContinue
		$line += " | godot exit=$code $res $(Log-Counts)"
		if (($code -ne 0) -or ($res -notmatch 'PASS$')) { $ok = $false }

		# c: the two runs read the same
		$v = Verdict $tn $tg
		$line += " / native vs godot: $v"
		if ($v -notmatch '^MATCH') { $ok = $false }

		if ($ok) { break }
		if ($try -eq 1) { "retry $mission ($Config) after: $line" | Add-Content $summary }
	}

	# d: the recording replays (natively and in Godot) to the trace it was recorded with
	$rtrace = Join-Path $Out "${t}_replay_native.trace"; $gtrace = Join-Path $Out "${t}_replay_godot.trace"
	Remove-Item "$rec.replay.log" -ErrorAction SilentlyContinue
	$code = Run-Native "-window -autotest -headless -replay `"$rec`" -trace `"$rtrace`""
	$rr = if (Test-Path "$rec.replay.log") { (Select-String "$rec.replay.log" -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line } else { 'NO LOG' }
	$line += " | replay native exit=$code $rr"
	if (($code -ne 0) -or ($rr -notmatch '^REPLAY OK')) { $ok = $false }
	Remove-Item "$rec.replay.log" -ErrorAction SilentlyContinue
	$code = Run-Godot "-window -autotest -headless -replay `"$rec`" -trace `"$gtrace`"" (Join-Path $Out "${t}_replay_godot")
	$rr = if (Test-Path "$rec.replay.log") { (Select-String "$rec.replay.log" -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line } else { 'NO LOG' }
	$line += " | replay godot exit=$code $rr"
	if (($code -ne 0) -or ($rr -notmatch '^REPLAY OK')) { $ok = $false }
	foreach ($pair in @(@('recorded vs replay native', $tg, $rtrace), @('recorded vs replay godot', $tg, $gtrace))) {
		$v = Verdict $pair[1] $pair[2]
		$line += " / $($pair[0]): $v"
		if ($v -notmatch '^MATCH') { $ok = $false }
	}
	if ($Config -eq 'd' -and -not (Select-String (Join-Path $GameDir 'autotest.log') -Pattern 'guards: assert' -Quiet)) { $ok = $false; $line += ' GUARDS NOT ASSERTING' }
	if ($ok) { $pass++ } else { $fail++; $line = "FAIL $line" }
	$line | Add-Content $summary
	$line
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
