<#
Render3D switch-off check: with a GPU switch at 0 the CPU route must still draw. Three proofs, one game at a time:
  1. the fallback check (tools\render3d_fallback_check.ps1) passes on the mission with the switch at 0,
  2. the run's counter line for the effect says 0 offered (nothing went to the sink: the CPU route drew it),
  3. the Godot frames with the switch at 0 are identical to the frame runner's default (the CPU baseline) and within the GPU route's, frame_compare thresholds never widened.
  tools\render3d_switch_off_check.ps1 -Run <game folder> [-Switch FS2_WARP_GPU] [-Counter 'warp cone'] [-Case AutotestArrivals] [-Config r]
Needs PATH_FS2 (= -Run) and GODOT_ENGINE_4_7. Exit 0 = pass.
#>
param(
	[Parameter(Mandatory)][string]$Run,
	[string]$Switch = 'FS2_WARP_GPU',
	[string]$Counter = 'warp cone',
	[string]$Case = 'AutotestArrivals',
	[string]$Config = 'r',
	[string]$Out = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Out) { $Out = Join-Path $env:TEMP "r3d_off_$Switch" }
$env:PATH_FS2 = $Run
$fail = 0
[Environment]::SetEnvironmentVariable($Switch, '0')
try {
	# 1 and 2: the fallback check, then its counter line
	$r = & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\render3d_fallback_check.ps1" -Run $Run -Missions $Case -Config $Config
	$r
	if ($LASTEXITCODE -ne 0) { "$Case FAIL: fallback check with $Switch=0"; $fail++ }
	$line = Select-String (Join-Path $env:TEMP "r3d_fb_$Case\e.txt") -Pattern "render3d: ${Counter}: (\d+) offered" | Select-Object -Last 1
	if (-not $line) { "$Case FAIL: no '$Counter' counter line"; $fail++ }
	elseif ([int]$line.Matches[0].Groups[1].Value -ne 0) { "$Case FAIL: $($line.Line.Trim()) with $Switch=0 (the sink drew it)"; $fail++ }
	else { "$Case OK: $($line.Line.Trim())" }
	# 3: the frame runner's default (every switch 0) is the CPU baseline: the switch given as 0 must give the identical frames,
	# and those frames must equal the GPU route's within the thresholds, which they can only do if the CPU route drew the effect
	$fc = Join-Path $PSScriptRoot 'frame_compare.ps1'
	$runner = Join-Path $PSScriptRoot 'godot_frame_runner.ps1'
	foreach ($cap in @(@('base', $null), @('off', '0'), @('gpu', '1'))) {
		[Environment]::SetEnvironmentVariable($Switch, $cap[1])
		& $fc -Capture (Join-Path $Out $cap[0]) -Config $Config -Cases $Case -Runner $runner | Out-Null
		foreach ($l in (Get-Content (Join-Path $Out "$($cap[0])\capture.txt"))) { "$($cap[0]) $l"; if ($l -notmatch '^OK ') { $fail++ } }
	}
	foreach ($pair in @(@('base', 'off', 'identical'), @('off', 'gpu', 'within'))) {
		& $fc -A (Join-Path $Out $pair[0]) -B (Join-Path $Out $pair[1]) -Out (Join-Path $Out "diff_$($pair[0])_$($pair[1])") -Cases $Case -NameA $pair[0] -NameB $pair[1] | Out-Null
		$rep = Get-Content (Join-Path $Out "diff_$($pair[0])_$($pair[1])\report.md")
		$rep | Where-Object { $_ -match '^\| \S+ \d+ \|' } | ForEach-Object { "$($pair[0]) vs $($pair[1]) $_" }
		$cm = [regex]::Match(($rep -join "`n"), 'identical (\d+), within (\d+), changed (\d+), missing (\d+), other recording (\d+)')
		$okc = if ($pair[2] -eq 'identical') { [int]$cm.Groups[2].Value + [int]$cm.Groups[3].Value } else { [int]$cm.Groups[3].Value }
		if (-not $cm.Success -or $okc -or [int]$cm.Groups[4].Value -or [int]$cm.Groups[5].Value) { "$Case FAIL: $($pair[0]) vs $($pair[1]) not $($pair[2])"; $fail++ }
	}
} finally { [Environment]::SetEnvironmentVariable($Switch, $null) }
if ($fail) { "$Switch=0 FAIL ($fail)"; exit 1 }
"$Switch=0 OK"
exit 0
