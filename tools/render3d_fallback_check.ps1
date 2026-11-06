<#
Render3D gate (D-20261105-165753): a shown replay with GPU nebula fog must draw every model on the GPU. Reads the exit line
"render3d: mesh draws on the polygon path: ..." and fails unless every counter is 0, skipped frames excepted.
  tools\render3d_fallback_check.ps1 -Run <game folder> [-Set autotest] [-Missions AutotestWeapons,AutotestNebActive] [-Config r]
One game at a time: run it only when no other game runs. Exit 0 = pass.
#>
param(
	[Parameter(Mandatory)][string]$Run,
	[string]$Fog = '',
	[string]$Set = 'autotest',
	[string[]]$Missions = @('AutotestWeapons', 'AutotestNebActive'),
	[string]$Config = 'r',
	[int]$TimeoutMin = 20
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot\layer.ps1"
if (-not $env:GODOT_ENGINE_4_7) { throw 'GODOT_ENGINE_4_7 is not set (folder holding the Godot 4.7.2 exes)' }
$godot = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64_console.exe'
$proj = Join-Path $repo 'godot'
$dll = Join-Path $Run (Get-FS2Dll $Config)
foreach ($f in $godot, $dll, (Join-Path $proj 'bin\fs2godot.dll')) { if (-not (Test-Path $f)) { throw "Missing $f" } }
if (Get-Process ($FS2ProcessNames + 'fs2run', 'Godot*') -ErrorAction SilentlyContinue) { throw 'a game is running' }
if ($Set -eq 'autotest') { Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $Run 'data\missions') -Force }
$fail = 0
foreach ($m in $Missions) {
	$rec = Join-Path $repo "recordings\$Set\$m.fs2rec"
	if (-not (Test-Path $rec)) { throw "Missing $rec" }
	$out = Join-Path $env:TEMP "r3d_fb_$m"; Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $out | Out-Null
	$env:PATH_FS2 = $Run; $env:FS2_DLL = $dll; $env:FS2_ROOT = $Run; $env:FS2_GR_HOST = '1'; if ($Fog) { $env:FS2_NEBFOG = $Fog } else { Remove-Item Env:FS2_NEBFOG -ErrorAction SilentlyContinue }
	$env:FS2_CMD = "-window -autotest -replay `"$rec`""
	Push-Location $Run
	$p = Start-Process $godot -ArgumentList "--path `"$proj`"" -PassThru -NoNewWindow -RedirectStandardOutput "$out\o.txt" -RedirectStandardError "$out\e.txt"; $null = $p.Handle
	if (-not $p.WaitForExit($TimeoutMin * 60000)) { Stop-Process $p -Force }
	Pop-Location
	Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD, Env:FS2_GR_HOST, Env:FS2_NEBFOG -ErrorAction SilentlyContinue
	$line = Select-String "$out\e.txt" -Pattern 'render3d: mesh draws on the polygon path:' | Select-Object -Last 1
	if (-not $line) { "$m FAIL: no counter line"; $fail++; continue }
	$nz = [regex]::Matches($line.Line, '(cpu_only|no z|fog unsupported|no frame|texture|fog state) (\d+)') | Where-Object { [int]$_.Groups[2].Value -ne 0 }
	$bl = Select-String "$out\e.txt" -Pattern 'render3d: beams: (\d+) offered, (\d+) drawn on the GPU, (\d+) declined' | Select-Object -Last 1
	$bd = if ($bl) { [int]$bl.Matches[0].Groups[3].Value } else { 0 }	# beams the sink declined (R3 row 1) fail the run
	$ll = Select-String "$out\e.txt" -Pattern 'render3d: lightning: (\d+) offered, (\d+) drawn on the GPU, (\d+) declined' | Select-Object -Last 1
	$ld = if ($ll) { [int]$ll.Matches[0].Groups[3].Value } else { 0 }	# lightning quads the sink declined (R3 row 5) fail the run
	$tl = Select-String "$out\e.txt" -Pattern 'render3d: trails: (\d+) offered, (\d+) drawn on the GPU, (\d+) declined' | Select-Object -Last 1
	$td = if ($tl) { [int]$tl.Matches[0].Groups[3].Value } else { 0 }	# trail polygons the sink declined (R3 row 2) fail the run
	$sl = Select-String "$out\e.txt" -Pattern 'render3d: shields: (\d+) offered, (\d+) drawn on the GPU, (\d+) declined' | Select-Object -Last 1
	$sd = if ($sl) { [int]$sl.Matches[0].Groups[3].Value } else { 0 }	# shield hit polygons the sink declined (R3 row 4) fail the run
	if ($nz) { "$m FAIL: " + (($nz | ForEach-Object { $_.Value }) -join ', '); $fail++ } elseif ($bd -ne 0) { "$m FAIL: $bd beams declined"; $fail++ } elseif ($td -ne 0) { "$m FAIL: $td trail polygons declined"; $fail++ } elseif ($sd -ne 0) { "$m FAIL: $sd shield polygons declined"; $fail++ } elseif ($ld -ne 0) { "$m FAIL: $ld lightning quads declined"; $fail++ } else { "$m OK (all fallback counters 0)" }
}
exit $fail
