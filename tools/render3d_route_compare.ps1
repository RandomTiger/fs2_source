<#
Render3D route compare: the same frames drawn on the CPU route and on the GPU route of one switch, pixel-compared with the
frame_compare thresholds (never widened). The CPU route is the frame runner's default (every GPU switch 0), the GPU route
sets the one switch to 1.
  tools\render3d_route_compare.ps1 -Switch FS2_WARP_GPU -Cases AutotestArrivals [-Out <dir>] [-Config r]
Needs PATH_FS2 and GODOT_ENGINE_4_7 (tools\godot_frame_runner.ps1). One game at a time. Exit 0 = every frame identical or within.
#>
param(
	[Parameter(Mandatory)][string]$Switch,
	[Parameter(Mandatory)][string[]]$Cases,
	[string]$Out = '',
	[string]$Config = 'r'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Out) { $Out = Join-Path $env:TEMP "r3d_route_$Switch" }
$fc = Join-Path $PSScriptRoot 'frame_compare.ps1'
$runner = Join-Path $PSScriptRoot 'godot_frame_runner.ps1'
$bad = 0
$was = [Environment]::GetEnvironmentVariable($Switch)
foreach ($route in @(@('cpu', '0'), @('gpu', '1'))) {
	[Environment]::SetEnvironmentVariable($Switch, $route[1])
	& $fc -Capture (Join-Path $Out $route[0]) -Config $Config -Cases $Cases -Runner $runner | Out-Null
	foreach ($l in (Get-Content (Join-Path $Out "$($route[0])\capture.txt"))) { "$($route[0]) $l"; if ($l -notmatch '^OK ') { $bad++ } }
}
[Environment]::SetEnvironmentVariable($Switch, $was)
$sum = & $fc -A (Join-Path $Out 'cpu') -B (Join-Path $Out 'gpu') -Out (Join-Path $Out 'diff') -Cases $Cases -NameA cpu -NameB gpu
$sum
$rep = Get-Content (Join-Path $Out 'diff\report.md')
$rep | Where-Object { $_ -match '^\| \S+ \d+ \|' }
$cm = [regex]::Match(($rep -join "`n"), 'changed (\d+), missing (\d+), other recording (\d+)')
if (-not $cm.Success) { 'FAIL no summary'; exit 1 }
if ($bad -or [int]$cm.Groups[1].Value -or [int]$cm.Groups[2].Value -or [int]$cm.Groups[3].Value) { "$Switch FAIL: $bad bad captures, changed $($cm.Groups[1].Value), missing $($cm.Groups[2].Value)"; exit 1 }
"$Switch OK"
exit 0
