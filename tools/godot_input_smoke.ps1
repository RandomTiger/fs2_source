<#
Live-input proof (G4): a windowed Godot run plays a live autotest mission on the game fiber and records it. Once with synthetic keyboard
and mouse events injected into Godot's input (runner.gd FS2_INJECT=1), once without. The recording's .record.log counts the input the game
took: the injected run must show more key, mouse-event and mouse-position entries than the baseline. Then the injected recording is
replayed natively and through Godot: both must say REPLAY OK and the traces must match (live input must not break replay).
Usage: tools\godot_input_smoke.ps1 [-Config r|d] [-Mission sm1-01] [-Out dir]
#>
param([string]$Config = 'r', [string]$Mission = 'sm1-01', [string]$Out,
	[string]$GameDir = $env:PATH_FS2, [string]$Godot)
if (-not $Godot) { if (-not $env:GODOT_ENGINE_4_7) { throw 'GODOT_ENGINE_4_7 is not set (folder holding the Godot 4.7.2 exes), and no -Godot given' }; $Godot = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64.exe' }
$repo = Split-Path $PSScriptRoot
if (-not $Out) { $Out = Join-Path $repo ('replays\godot_input_' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
$res = @()
function Run-Live($name, $inject) {
	$rec = "$Out\$name.fs2rec"
	$env:FS2_DLL = Join-Path $GameDir "FS2game64$Config.dll"; $env:FS2_ROOT = $GameDir; $env:FS2_FIBER = '1'; $env:FS2_FRAMES = '1'
	$env:FS2_INJECT = if ($inject) { '1' } else { '0' }
	$env:FS2_CMD = "-window -autotest -headless -at_mission $Mission -at_pilot Tom -at_time 90 -record `"$rec`" -trace `"$Out\$name.trace`""
	Set-Location $GameDir
	$p = Start-Process $Godot -ArgumentList "--path `"$(Join-Path $repo 'godot')`" --windowed --resolution 800x450" -PassThru -Wait -RedirectStandardOutput "$Out\$name.out.txt" -RedirectStandardError "$Out\$name.err.txt"
	Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD, Env:FS2_FIBER, Env:FS2_FRAMES, Env:FS2_INJECT -ErrorAction SilentlyContinue
	"$name godot exit=$($p.ExitCode)"
}
function Count($name, $pat) { $l = Select-String -Path "$Out\$name.fs2rec.record.log" -Pattern $pat -ErrorAction SilentlyContinue | Select-Object -First 1; if ($l) { ($l.Line.Trim() -split '\s+')[1] } else { 0 } }
$res += Run-Live 'base' $false
$res += Run-Live 'inject' $true
$res += (Select-String -Path "$Out\inject.out.txt" -Pattern '^inject:' | % { $_.Line })
foreach ($n in 'base', 'inject') {
	$res += "$n record.log: ev_key=$(Count $n '^\s+ev_key\s' ) ev_mouse=$(Count $n '^\s+ev_mouse\s')"
}
# replay the injected recording natively and in Godot
Set-Location $GameDir
$rc = "-window -autotest -headless -replay `"$Out\inject.fs2rec`" -trace `"$Out\replay_native.trace`""
$np = Start-Process ".\FS2vs2022${Config}64.exe" -ArgumentList $rc -PassThru -Wait
$res += "native replay exit=$($np.ExitCode): " + ((Select-String "$Out\inject.fs2rec.replay.log" -Pattern '^REPLAY|DESYNC|ERROR' | Select-Object -Last 1).Line)
$env:FS2_DLL = Join-Path $GameDir "FS2game64$Config.dll"; $env:FS2_ROOT = $GameDir; $env:FS2_FIBER = '1'
$env:FS2_CMD = "-window -autotest -headless -replay `"$Out\inject.fs2rec`" -trace `"$Out\replay_godot.trace`""
$p = Start-Process (Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64_console.exe') -ArgumentList "--headless --path `"$(Join-Path $repo 'godot')`"" -PassThru -Wait -RedirectStandardOutput "$Out\replay_godot.out.txt"
Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD, Env:FS2_FIBER -ErrorAction SilentlyContinue
$res += "godot replay exit=$($p.ExitCode): " + ((Select-String "$Out\inject.fs2rec.replay.log" -Pattern '^REPLAY|DESYNC|ERROR' | Select-Object -Last 1).Line)
$res += "TraceDiff live vs native replay: " + (& $td "$Out\inject.trace" "$Out\replay_native.trace" | Select-Object -Last 1)
$res += "TraceDiff live vs godot replay:  " + (& $td "$Out\inject.trace" "$Out\replay_godot.trace" | Select-Object -Last 1)
$res | Set-Content "$Out\input_smoke.txt"; $res
