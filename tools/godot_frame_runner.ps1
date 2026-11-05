<#
Runner for tools\frame_compare.ps1 -Capture -Runner (docs/VISUAL_COMPARE.md): replays one recording through the Godot host in a
shown window (host renderer, FS2_GR_HOST=1, every game frame drawn, FS2_SKIP_STALE=0, nebula fog on: FS2_NEBFOG=gpu unless set) with -shot_frames, so the game saves the
Godot pictures of the frames through the render sink's read-back. Returns Godot's exit code.
  tools\frame_compare.ps1 -Capture <dir> -Runner tools\godot_frame_runner.ps1
Needs godot\bin\fs2godot.dll, Godot 4.7.2 in $env:GODOT_ENGINE_4_7, and FS2game_<Layer>_64<Config>.dll in PATH_FS2 (tools\layer.ps1 Get-FS2Dll) ($env:FS2_FRAME_CONFIG, default r).
#>
param([string]$Rec, [string]$Trace, [string]$Frames, [string]$ShotDir)
$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"	# DLL names (D-20261101-064106)
if (-not $env:GODOT_ENGINE_4_7) { throw 'GODOT_ENGINE_4_7 is not set (folder holding the Godot 4.7.2 exes)' }
$godot = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64_console.exe'
$proj = Join-Path $repo 'godot'
$config = if ($env:FS2_FRAME_CONFIG) { $env:FS2_FRAME_CONFIG } else { 'r' }
$dll = Join-Path $env:PATH_FS2 (Get-FS2Dll $config)
foreach ($f in $godot, $dll, (Join-Path $proj 'bin\fs2godot.dll')) { if (-not (Test-Path $f)) { throw "Missing $f" } }
if (-not (Test-Path (Join-Path $proj '.godot\extension_list.cfg'))) { & $godot --headless --path $proj --import | Out-Null }
while (Get-Process ($FS2ProcessNames + 'Godot*') -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
$env:FS2_DLL = $dll; $env:FS2_ROOT = $env:PATH_FS2
$env:FS2_CMD = "-window -autotest -replay `"$Rec`" -trace `"$Trace`" -shot_frames $Frames -shot_dir `"$ShotDir`""
$env:FS2_GR_HOST = '1'; $env:FS2_SKIP_STALE = '0'
$nebfog = $env:FS2_NEBFOG; if (-not $nebfog) { $env:FS2_NEBFOG = 'gpu' }	# a picture compare needs fog as a player sees it
$bbrot = $env:FS2_BB_ROT; if (-not $bbrot) { $env:FS2_BB_ROT = '0' }	# a picture compare against the native poly path: rotated puffs stay on the poly path
Push-Location $env:PATH_FS2
$p = Start-Process $godot -ArgumentList "--path `"$proj`"" -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $ShotDir 'godot.out.txt') -RedirectStandardError (Join-Path $ShotDir 'godot.err.txt'); $null = $p.Handle
if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $code = 'TIMEOUT' } else { $code = $p.ExitCode }
Pop-Location
Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD, Env:FS2_GR_HOST, Env:FS2_SKIP_STALE -ErrorAction SilentlyContinue
if (-not $nebfog) { Remove-Item Env:FS2_NEBFOG -ErrorAction SilentlyContinue }
if (-not $bbrot) { Remove-Item Env:FS2_BB_ROT -ErrorAction SilentlyContinue }
return $code
