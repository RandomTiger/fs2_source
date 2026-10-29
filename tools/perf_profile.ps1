<#
Performance profile driver (performance pass, step 1). Measurement only. Runs one recording through the Godot host in a shown window at
120 Hz game pace (FS2_PACE=1, the game does not sleep, no -trace) with the profiler on (runner.gd FS2_PERF_*), then summarizes.
  -Set      autotest (FS2 retail) | fs1
  -Mission  recording base name (recordings\<Set>\<Mission>.fs2rec)
  -Run      the game folder (own scratch copy; never the shared install)
  -Sample   stack sampler Hz (0 = clean timing run)
  -Config   r (default) or d
One game at a time: refuses to start while an FS2vs2022*, fs2run or Godot process of this scratch runs.
#>
param(
	[Parameter(Mandatory)][string]$Set,
	[Parameter(Mandatory)][string]$Mission,
	[Parameter(Mandatory)][string]$Run,
	[string]$Tag,
	[int]$Sample = 0,
	[string]$Config = 'r',
	[string]$Out,
	[int]$TimeoutMin = 20,
	[string]$Extra = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $env:GODOT_ENGINE_4_7) { $env:GODOT_ENGINE_4_7 = 'C:\dev\Tools\godot_4.7.2' }
$godot = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64_console.exe'
$proj = Join-Path $repo 'godot'
$rec = Join-Path $repo "recordings\$Set\$Mission.fs2rec"
$dll = Join-Path $Run "FS2game64$Config.dll"
foreach ($f in $godot, $rec, $dll, (Join-Path $proj 'bin\fs2godot.dll')) { if (-not (Test-Path $f)) { throw "Missing $f" } }
if (-not $Tag) { $Tag = "${Set}_${Mission}_" + $(if ($Sample) { "s$Sample" } else { 'clean' }) }
if (-not $Out) { $Out = Join-Path $Run 'perf_out' }
New-Item -ItemType Directory -Force $Out | Out-Null
if ($Set -eq 'autotest') { Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $Run 'data\missions') -Force }
if (-not (Test-Path (Join-Path $proj '.godot\extension_list.cfg'))) { & $godot --headless --path $proj --import | Out-Null }
Remove-Item "$rec.replay.log", (Join-Path $Run 'autotest.log') -ErrorAction SilentlyContinue
$env:PATH_FS2 = $Run
$env:FS2_DLL = $dll; $env:FS2_ROOT = $Run
$env:FS2_CMD = "-window -autotest -replay `"$rec`" $Extra"
$env:FS2_GR_HOST = '1'; $env:FS2_PACE = '1'
$env:FS2_PERF_DIR = $Out; $env:FS2_PERF_TAG = $Tag; $env:FS2_PERF_SAMPLE_HZ = "$Sample"; $env:FS2_PERF_SYMS = $Run
Set-Location $Run
$t0 = Get-Date
$p = Start-Process $godot -ArgumentList "--path `"$proj`"" -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $Out "$Tag.out.txt") -RedirectStandardError (Join-Path $Out "$Tag.err.txt"); $null = $p.Handle
if (-not $p.WaitForExit($TimeoutMin * 60000)) { Stop-Process $p -Force; $code = 'TIMEOUT' } else { $code = $p.ExitCode }
Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD, Env:FS2_GR_HOST, Env:FS2_PACE, Env:FS2_PERF_DIR, Env:FS2_PERF_TAG, Env:FS2_PERF_SAMPLE_HZ, Env:FS2_PERF_SYMS -ErrorAction SilentlyContinue
$log = "$rec.replay.log"
$res = if (Test-Path $log) { (Select-String $log -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line } else { 'NO LOG' }
Remove-Item $log -ErrorAction SilentlyContinue
"{0} exit={1} {2} wall={3:N0}s" -f $Tag, $code, $res, ((Get-Date) - $t0).TotalSeconds
