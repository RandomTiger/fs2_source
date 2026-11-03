<#
The six live flight device switches (D-20261102-204804): Pad -> Joystick -> Mouse -> Pad -> Mouse -> Joystick -> Pad in one run,
with the scripted joystick and pad both injected (-at_input both -at_flight cycle, code\Replay\InputTest.h). Each switch goes
through game_options_set_flight_device (the choice, Use_mouse_to_fly, joy_reassign), as the Game Options flight row does; the
checks say only the chosen device reaches the controls. Live only (not recorded), so nothing is replayed.

Per config: the native Win32 exe, the native x64 exe, and with -Godot the x64 game DLL in the Godot host (headless). The
autotest.log of each run must end "Flight switch: 6 of 6 transitions passed ... PASS" and the exit code be 0.

Usage: tools\flight_switch_check.ps1 [-Configs r,d] [-Out dir] [-GameDir dir] [-Godot] [-GodotExe path]
Exit 0 all pass, 1 any fail. The caller restores autotest.log; the run writes no fs2.ini key (autotest ignores the flight key).
#>
param(
	[string[]]$Configs = @('r', 'd'),
	[string]$Out,
	[string]$GameDir = $env:PATH_FS2,
	[switch]$Godot,
	[string]$GodotExe
)
$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if ($Godot -and -not $GodotExe) {
	if (-not $env:GODOT_ENGINE_4_7) { throw 'GODOT_ENGINE_4_7 is not set, and no -GodotExe given' }
	$GodotExe = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64_console.exe'
}
if (-not $Out) { $Out = Join-Path $repo ("replays\flight_switch_" + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$Configs = $Configs | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
Copy-Item (Join-Path $repo 'data\missions\Autotest*.fs2') (Join-Path $GameDir 'data\missions') -Force
$proj = Join-Path $repo 'godot'
$log = Join-Path $GameDir 'autotest.log'
$cmd = '-window -autotest -headless -at_mission AutotestJoy -at_input both -at_flight cycle -at_pilot AutotestPilot -at_time 60'

function Wait-NoGame { while (Get-Process ($FS2ProcessNames + 'fs2run', 'Godot*', 'FS2Godot*') -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 } }
function Run-One([string]$name, [scriptblock]$start) {
	Wait-NoGame
	Remove-Item $log -ErrorAction SilentlyContinue
	$p = & $start; $null = $p.Handle
	$code = if ($p.WaitForExit(600000)) { $p.ExitCode } else { Stop-Process $p -Force; 'TIMEOUT' }
	Copy-Item $log (Join-Path $Out "$name.autotest.log") -ErrorAction SilentlyContinue
	$lines = if (Test-Path $log) { Select-String $log -Pattern 'Flight switch: (?!started).*' | ForEach-Object { $_.Line.Substring($_.Line.IndexOf(']') + 2) } } else { @('NO LOG') }
	$last = $lines | Select-Object -Last 1
	$ok = ($code -eq 0) -and ($last -match '^Flight switch: 6 of 6 transitions passed.*PASS$')
	$res = [pscustomobject]@{ Name = $name; Ok = $ok; Line = "$(if ($ok) { 'PASS' } else { 'FAIL' }) $name exit=$code | $last" }
	foreach ($l in $lines) { "  $l" | Add-Content (Join-Path $Out 'summary.txt') }
	$res.Line | Add-Content (Join-Path $Out 'summary.txt')
	return $res
}

Push-Location $GameDir
$pass = 0; $fail = 0
foreach ($c in $Configs) {
	$runs = @(
		@("native_${c}", (Join-Path $GameDir (Get-FS2Exe $c))),
		@("native_${c}64", (Join-Path $GameDir (Get-FS2Exe "${c}64")))
	)
	foreach ($r in $runs) {
		if (-not (Test-Path $r[1])) { "FAIL $($r[0]): missing $($r[1])"; $fail++; continue }
		$exe = $r[1]
		$res = Run-One $r[0] { Start-Process $exe -ArgumentList $cmd -PassThru }
		$res.Line
		if ($res.Ok) { $pass++ } else { $fail++ }
	}
	if ($Godot) {
		$dll = Join-Path $GameDir (Get-FS2Dll $c)
		$env:FS2_DLL = $dll; $env:FS2_ROOT = $GameDir; $env:FS2_CMD = $cmd
		$pre = Join-Path $Out "godot_$c"
		$res = Run-One "godot_$c" { Start-Process $GodotExe -ArgumentList "--headless --path `"$proj`"" -PassThru -NoNewWindow -RedirectStandardOutput "$pre.out.txt" -RedirectStandardError "$pre.err.txt" }
		Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD -ErrorAction SilentlyContinue
		$res.Line
		if ($res.Ok) { $pass++ } else { $fail++ }
	}
}
Pop-Location
"DONE pass=$pass fail=$fail" | Add-Content (Join-Path $Out 'summary.txt')
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
