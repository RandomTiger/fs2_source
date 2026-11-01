<#
Pixel compare of in-mission frames (docs/VISUAL_COMPARE.md).

Capture: replays each case of tools\frame_cases.json with -shot_frames and saves <Out>\<case>\frame_<F>.bmp.
  tools\frame_compare.ps1 -Capture <Out> [-Config r|d] [-Cases a,b] [-Runner <script>]
  Each replay must say REPLAY OK and its trace must match the recorded one, or the case is marked BAD in <Out>\capture.txt.
  -Runner replaces the native exe: a script taking -Rec -Trace -Frames -ShotDir that replays the recording in another
  renderer and returns the game's exit code (the Godot branch has tools\godot_frame_runner.ps1).

Compare: grades every frame of A against the same frame of B and writes <Out>\report.md and diff PNGs.
  tools\frame_compare.ps1 -A <dir> -B <dir> -Out <dir> [-NameA native] [-NameB godot]
  identical: every pixel the same.  within: pixels over the threshold <= pct_over % and PSNR >= min_psnr.  changed: otherwise.

Captures and diffs hold game pictures: keep them under replays\ (ignored) or the private reference repo, never commit them.
One copy of the game at a time.
#>
param(
	[string]$Capture,
	[string]$Config = 'r',
	[string[]]$Cases,
	[string]$Runner,
	[string]$A, [string]$B, [string]$Out,
	[string]$NameA = 'A', [string]$NameB = 'B',
	[string]$CaseFile
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"	# exe names (D-20261101-064106)
if (-not $CaseFile) { $CaseFile = Join-Path $PSScriptRoot 'frame_cases.json' }
$lib = Get-Content $CaseFile -Raw | ConvertFrom-Json
$Cases = $Cases | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
$list = @($lib.cases | Where-Object { (-not $Cases) -or ($Cases -contains $_.name) })
if (-not $list) { throw "No cases match: $Cases" }

function Get-Setting($case, $name) { if ($null -ne $case.$name) { $case.$name } else { $lib.$name } }

if ($Capture) {
	if (-not $env:PATH_FS2) { throw 'Set PATH_FS2' }
	$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
	New-Item -ItemType Directory -Force $Capture | Out-Null
	$summary = Join-Path $Capture 'capture.txt'
	Remove-Item $summary -ErrorAction SilentlyContinue
	Copy-Item (Join-Path $repo 'data\missions\*.fs2') (Join-Path $env:PATH_FS2 'data\missions')
	Push-Location $env:PATH_FS2
	foreach ($c in $list) {
		$rec = Join-Path $repo $c.recording
		$od = Join-Path $Capture $c.name
		Remove-Item $od -Recurse -Force -ErrorAction SilentlyContinue
		New-Item -ItemType Directory -Force $od | Out-Null
		$trace = Join-Path $od 'replay.trace'
		$frames = ($c.frames -join ',')
		while (Get-Process $FS2ProcessNames -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
		Remove-Item "$rec.replay.log" -ErrorAction SilentlyContinue
		if ($Runner) {
			$code = & $Runner -Rec $rec -Trace $trace -Frames $frames -ShotDir $od
		} else {
			$p = Start-Process ".\$(Get-FS2Exe $Config)" -ArgumentList "-window -autotest -replay `"$rec`" -trace `"$trace`" -shot_frames $frames -shot_dir `"$od`"" -PassThru
			if (-not $p.WaitForExit(1800000)) { Stop-Process $p -Force; $code = 'TIMEOUT' } else { $code = $p.ExitCode }
		}
		$res = 'NO LOG'
		if (Test-Path "$rec.replay.log") {
			Copy-Item "$rec.replay.log" (Join-Path $od 'replay.log')
			$res = (Select-String "$rec.replay.log" -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line
		}
		$diff = 'NO TRACE'
		if (Test-Path $trace) { $diff = (& $td ([IO.Path]::ChangeExtension($rec, '.trace')) $trace | Select-Object -Last 1) }
		$shots = @($c.frames | Where-Object { Test-Path (Join-Path $od ('frame_{0:D6}.bmp' -f $_)) }).Count
		$ok = ($res -match 'REPLAY OK') -and ($diff -match '^MATCH') -and ($shots -eq @($c.frames).Count)
		$line = "{0} {1} exit={2} {3} / {4} / shots {5} of {6}" -f $(if ($ok) { 'OK ' } else { 'BAD' }), $c.name, $code, $res, $diff, $shots, @($c.frames).Count
		$line | Add-Content $summary
		$line
	}
	Pop-Location
	return
}

if (-not ($A -and $B -and $Out)) { throw 'Give -Capture <dir>, or -A <dir> -B <dir> -Out <dir>' }
Add-Type -Path (Join-Path $PSScriptRoot 'frame_compare.cs') -ReferencedAssemblies System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$rows = @(); $counts = @{ identical = 0; within = 0; changed = 0; missing = 0 }
foreach ($c in $list) {
	$th = [int](Get-Setting $c 'threshold'); $po = [double](Get-Setting $c 'pct_over'); $mp = [double](Get-Setting $c 'min_psnr')
	$masks = $null
	if ($c.masks) { $masks = [int[]]@($c.masks | ForEach-Object { $_.rect } | ForEach-Object { $_ }) }
	foreach ($f in $c.frames) {
		$file = 'frame_{0:D6}.bmp' -f $f
		$fa = Join-Path (Join-Path $A $c.name) $file; $fb = Join-Path (Join-Path $B $c.name) $file
		$id = "$($c.name) $f"
		if (-not ((Test-Path $fa) -and (Test-Path $fb))) {
			$counts.missing++; $rows += "| $id | missing | | | | | | $(if (-not (Test-Path $fa)) { "no $NameA" } else { "no $NameB" }) |"; continue
		}
		$dp = Join-Path $Out ("{0}_{1:D6}_diff.png" -f $c.name, $f)
		$r = [FrameCompare]::Compare($fa, $fb, $masks, $th, $dp)
		if ($r.Error) { $counts.missing++; $rows += "| $id | error | | | | | | $($r.Error) |"; continue }
		if ($r.Differing -eq 0) { $grade = 'identical' }
		elseif (($r.PctOver -le $po) -and ($r.Psnr -ge $mp)) { $grade = 'within' }
		else { $grade = 'changed' }
		$counts[$grade]++
		$psnr = if ([double]::IsInfinity($r.Psnr)) { 'inf' } else { '{0:F1}' -f $r.Psnr }
		$diffCell = if ($r.Differing) { Split-Path $dp -Leaf } else { '' }
		$rows += "| $id | $grade | $($r.MaxDelta) | {0:F3} | {1:F2} | $psnr | {2:F4} | $diffCell |" -f (100.0 * $r.Differing / $r.Compared), $r.PctOver, $r.Ssim
	}
}
$report = @(
	"# Frame compare: $NameA vs $NameB",
	'',
	"$NameA = $A  ",
	"$NameB = $B  ",
	"Default threshold $($lib.threshold) (channel difference), within = over-threshold pixels <= $($lib.pct_over) % and PSNR >= $($lib.min_psnr) dB.",
	'',
	"**identical $($counts.identical), within $($counts.within), changed $($counts.changed), missing $($counts.missing)**",
	'',
	'| case frame | grade | max delta | % differing | % over threshold | PSNR dB | SSIM | diff (A, B, difference) |',
	'|---|---|---|---|---|---|---|---|'
) + $rows
$report | Set-Content (Join-Path $Out 'report.md')
$report[6]
