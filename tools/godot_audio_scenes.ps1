<#
Plays the audio assessment scenes (recordings\audio, docs\AUDIO_ASSESSMENT.md) through the shown Godot host with sound, one after
another, at the recorded speed. D-20261019-110414 makes these the port's audio reference; D-20261029-060953 is the interim sound
(Godot's window handle given to the game's DirectSound, fs2_set_window_handle).

  tools\godot_audio_scenes.ps1 [-Scenes AudioA,AudioB,...] [-Config r|d] [-GameDir <dir>] [-Godot <exe>] [-Out <dir>] [-Pause]

Every replay uses FS2_REPLAY_SOUND_MODEL=1 (gameplay does not depend on the sound device) and -replay_pace (recorded speed), the same
as tools\audio_assessment.ps1, and runs in a real Godot window with the host renderer, so the game takes the sound device. Each run's
stderr has the "fs2api: sound" start line (the handle DirectSound used and what it made of it). The trace of each replay is compared
with the recording's own; a TraceDiff other than MATCH is a fault. -Pause waits for Enter between scenes (a listener).
Needs GODOT_ENGINE_4_7 (or -Godot), godot\bin\fs2godot.dll, FS2game64<c>.dll and FS2vs2022<c>64.exe in the game folder, and
nobody else running the game from it.
#>
param(
	[string[]]$Scenes,
	[string]$Config = 'r',
	[string]$GameDir = $env:PATH_FS2,
	[string]$Godot,
	[string]$Out,
	[switch]$Pause
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Godot) {
	if (-not $env:GODOT_ENGINE_4_7) { throw 'GODOT_ENGINE_4_7 is not set (folder holding the Godot 4.7.2 exes), and no -Godot given' }
	$Godot = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64.exe'
}
$recDir = Join-Path $repo 'recordings\audio'
$Scenes = @($Scenes | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if (-not $Scenes) { $Scenes = @(Get-ChildItem $recDir -Filter 'Audio*.fs2rec' | ForEach-Object { $_.BaseName }) }
if (-not $Out) { $Out = Join-Path $repo ('replays\godot_audio_' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
$dll = Join-Path $GameDir "FS2game64$Config.dll"
$proj = Join-Path $repo 'godot'
foreach ($f in $td, $Godot, $dll, (Join-Path $proj 'bin\fs2godot.dll')) { if (-not (Test-Path $f)) { throw "Missing $f" } }
if (Get-Process FS2vs2022*, fs2run, Godot* -ErrorAction SilentlyContinue) { throw 'The game or Godot is already running' }
Copy-Item (Join-Path $repo 'data\missions\Audio*.fs2') (Join-Path $GameDir 'data\missions')
if (-not (Test-Path (Join-Path $proj '.godot\extension_list.cfg'))) { & $Godot --headless --path $proj --import | Out-Null }

$bad = 0
Set-Location $GameDir
foreach ($s in $Scenes) {
	$rec = Join-Path $recDir "$s.fs2rec"
	$trace = Join-Path $Out "$s.trace"
	$notes = [regex]::Match((Get-Content -Raw (Join-Path $repo "data\missions\$s.fs2")), 'scene \w: ([^\r\n]*)').Groups[1].Value
	if ($Pause) { Read-Host "`n$s`: $notes`nPress Enter to play" | Out-Null } else { Write-Host "`n$s`: $notes" }
	$env:FS2_DLL = $dll; $env:FS2_ROOT = $GameDir; $env:FS2_GR_HOST = '1'; $env:FS2_FIBER = '1'; $env:FS2_REPLAY_SOUND_MODEL = '1'
	$env:FS2_CMD = "-window -autotest -replay `"$rec`" -replay_pace -trace `"$trace`""
	$p = Start-Process $Godot -ArgumentList "--path `"$proj`"" -PassThru -RedirectStandardOutput (Join-Path $Out "$s.out.txt") -RedirectStandardError (Join-Path $Out "$s.err.txt")
	$null = $p.Handle
	$code = if ($p.WaitForExit(1800000)) { $p.ExitCode } else { Stop-Process $p -Force; 'TIMEOUT' }
	Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_GR_HOST, Env:FS2_FIBER, Env:FS2_REPLAY_SOUND_MODEL, Env:FS2_CMD -ErrorAction SilentlyContinue
	$res = if (Test-Path "$rec.replay.log") { (Select-String "$rec.replay.log" -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line } else { 'NO LOG' }
	Remove-Item "$rec.replay.log" -ErrorAction SilentlyContinue
	$v = if (Test-Path $trace) { ((& $td (Join-Path $recDir "$s.trace") $trace) | Select-String '^(MATCH|DIVERGED)' | Select-Object -First 1).Line } else { 'NO TRACE' }
	$snd = (Select-String (Join-Path $Out "$s.err.txt") -Pattern 'fs2api: sound' | Select-Object -First 1).Line
	if (-not $snd) { $snd = 'NO SOUND START LINE' }
	$line = "$s exit=$code $res / vs recorded: $v | $snd"
	if (($code -ne 0) -or ($res -notmatch '^REPLAY OK') -or ($v -notmatch '^MATCH') -or ($snd -notmatch 'snd_is_inited 1')) { $line = "FAIL $line"; $bad++ }
	$line | Add-Content (Join-Path $Out 'summary.txt')
	$line
}
if ($bad) { exit 1 }
