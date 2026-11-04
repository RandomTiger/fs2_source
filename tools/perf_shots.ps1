<#
Gate C helper for the performance pass: replays one recording through the Godot host in a shown window (host renderer) and saves the
window every -Every game frames as a numbered PNG (runner.gd FS2_SHOT_DIR), then prints one SHA-256 per shot. Run it before and after
a fix and diff the two lists: identical lists = the drawn frames are bit-identical. The PNGs stay in -Out (never committed).
  tools\perf_shots.ps1 -Set autotest -Mission sm2-07 -Run <scratch game folder> -Out <output folder> [-Every 300] [-Config r]
Prints the list and writes <Out>\hashes.txt. One game at a time: run it only when no other game runs.
#>
param(
	[Parameter(Mandatory)][string]$Set,
	[Parameter(Mandatory)][string]$Mission,
	[Parameter(Mandatory)][string]$Run,
	[Parameter(Mandatory)][string]$Out,
	[int]$Every = 300,
	[string]$Config = 'r',
	[int]$TimeoutMin = 20
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot\layer.ps1"	# exe and DLL names (D-20261101-064106)
if (-not $env:GODOT_ENGINE_4_7) { throw 'GODOT_ENGINE_4_7 is not set (folder holding the Godot 4.7.2 exes)' }
$godot = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64_console.exe'
$proj = Join-Path $repo 'godot'
$rec = Join-Path $repo "recordings\$Set\$Mission.fs2rec"
$dll = Join-Path $Run (Get-FS2Dll $Config)
foreach ($f in $godot, $rec, $dll, (Join-Path $proj 'bin\fs2godot.dll')) { if (-not (Test-Path $f)) { throw "Missing $f" } }
if (Get-Process ($FS2ProcessNames + 'fs2run', 'Godot*') -ErrorAction SilentlyContinue) { throw 'a game is running' }
Remove-Item $Out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Out | Out-Null
if ($Set -eq 'autotest') { Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $Run 'data\missions') -Force }
Remove-Item "$rec.replay.log", (Join-Path $Run 'autotest.log') -ErrorAction SilentlyContinue
$env:PATH_FS2 = $Run
$env:FS2_DLL = $dll; $env:FS2_ROOT = $Run
$env:FS2_CMD = "-window -autotest -replay `"$rec`""
$env:FS2_GR_HOST = '1'; $env:FS2_SHOT_DIR = $Out; $env:FS2_SHOT_EVERY = "$Every"
Set-Location $Run
$p = Start-Process $godot -ArgumentList "--path `"$proj`"" -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $Out 'run.out.txt') -RedirectStandardError (Join-Path $Out 'run.err.txt'); $null = $p.Handle
if (-not $p.WaitForExit($TimeoutMin * 60000)) { Stop-Process $p -Force; $code = 'TIMEOUT' } else { $code = $p.ExitCode }
Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD, Env:FS2_GR_HOST, Env:FS2_SHOT_DIR, Env:FS2_SHOT_EVERY -ErrorAction SilentlyContinue
$log = "$rec.replay.log"
$res = if (Test-Path $log) { (Select-String $log -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line } else { 'NO LOG' }
Remove-Item $log -ErrorAction SilentlyContinue
$h = Get-ChildItem $Out -Filter shot_*.png | Sort-Object Name | ForEach-Object { "{0} {1}" -f $_.Name, (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
$h | Set-Content (Join-Path $Out 'hashes.txt')
$h
"$Mission exit=$code $res shots=$(@($h).Count)"
