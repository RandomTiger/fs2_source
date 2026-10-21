<#
On-demand visual check of the FS1 retail screens: runs the menu tour with screenshots on FS1 retail data and compares
each screen with Tom's retail reference captures (private repo fs2_reference, fs1_retail). A tool, never a gate: run it
when you want a list of static UI differences to look at. docs/FS1_COMPAT.md, "Visual check", has the details.

Usage (PowerShell, from anywhere):
  tools\fs1_visual_check.ps1 [-Reference <fs2_reference dir>] [-GameDir <dir>] [-Config r] [-Mission <file>]
                             [-TrainingMission <file>] [-Shots <dir>] [-Out <dir>]

-Reference   the fs2_reference checkout. Defaults to $env:FS2_REFERENCE.
-GameDir     the FS1 retail install holding the build's exes. Defaults to $env:PATH_FS1.
-Shots       compare an existing shot folder and don't run the game.
-Out         defaults to <Reference>\fs1_retail_check\<time>, which fs2_reference ignores. Never inside fs2_source:
             the captures, diffs and report hold Volition game art.

Writes <Out>\report.md (every screen), <Out>\TODO.md (one entry per difference, with reference, ours and diff images)
and <Out>\diff\<screen>.png. Masks are <Reference>\fs1_retail\masks.json.
#>
param(
	[string]$Reference = $env:FS2_REFERENCE,
	[string]$GameDir = $env:PATH_FS1,
	[string]$Config = 'r',
	[string]$Mission,
	[string]$TrainingMission,	# off: the tour does not take a training briefing yet (it logs it as briefing/briefing)
	[string]$Shots,
	[string]$Out
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
if (-not $Reference) { throw 'Set FS2_REFERENCE or pass -Reference (the fs2_reference checkout)' }
$refRoot = Join-Path $Reference 'fs1_retail'
$maskFile = Join-Path $refRoot 'masks.json'
if (-not (Test-Path $maskFile)) { throw "No $maskFile" }
if (-not $Out) { $Out = Join-Path $Reference ('fs1_retail_check\' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
if ($Out.StartsWith((Resolve-Path $repo).Path, [StringComparison]::OrdinalIgnoreCase)) {
	throw "-Out is inside fs2_source ($repo). The captures are game art: put them outside the repo."
}

Add-Type -Path (Join-Path $PSScriptRoot 'visual_compare.cs') -ReferencedAssemblies System.Drawing

# --- run the tour ------------------------------------------------------------------------------------------------
$logs = @()
if (-not $Shots) {
	if (-not $GameDir) { throw 'Set PATH_FS1 or pass -GameDir' }
	$running = Get-Process FS2vs2022*, FS, Freespace* -ErrorAction SilentlyContinue
	if ($running) { throw "A game is running ($(($running | ForEach-Object { $_.ProcessName }) -join ', ')). Close it and run again." }
	$Shots = Join-Path $Out 'shots'
	$runs = @(@{ name = 'tour'; mission = $Mission })
	if ($TrainingMission) { $runs += @{ name = 'training'; mission = $TrainingMission } }
	Push-Location $GameDir
	try {
		foreach ($run in $runs) {
			$dir = Join-Path $Shots $run.name
			New-Item -ItemType Directory -Force $dir | Out-Null
			$log = Join-Path $GameDir 'autotest.log'
			Remove-Item $log -ErrorAction SilentlyContinue
			$argList = "-window -autotest -at_menus -at_shots `"$dir`""
			if ($run.mission) { $argList += " -at_mission $($run.mission)" }
			$p = Start-Process ".\FS2vs2022$Config.exe" -ArgumentList $argList -PassThru
			if (-not $p.WaitForExit(600000)) { Stop-Process $p -Force }
			if (Test-Path $log) {
				$copy = Join-Path $Out "$($run.name).autotest.log"
				Copy-Item $log $copy
				$logs += $copy
			}
		}
	} finally { Pop-Location }
} else {
	$logs = @(Get-ChildItem $Shots -Recurse -Filter '*.log' | ForEach-Object { $_.FullName })
}

# what the tour said about each screen: Screen '<category>/<screen>': PASS|FAIL <reason>|SKIP <reason>
$said = @{}
foreach ($l in $logs) {
	foreach ($m in (Select-String $l -Pattern "Screen '([^/']+)/([^']+)': (.*)$")) {
		$said[$m.Matches[0].Groups[2].Value] = $m.Matches[0].Groups[3].Value.Trim()
	}
}

# newest shot of each screen, from any run: <shots>\[<run>\]<category>\<screen>.png|bmp
$shot = @{}
if (Test-Path $Shots) {
	Get-ChildItem $Shots -Recurse -Include '*.png', '*.bmp' | Sort-Object LastWriteTime | ForEach-Object { $shot[$_.BaseName] = $_.FullName }
}

# --- masks -------------------------------------------------------------------------------------------------------
$masks = Get-Content -Raw $maskFile | ConvertFrom-Json
# PowerShell unrolls arrays returned from functions, so the rectangles go straight into typed lists
function AddRects($to, $list) { foreach ($e in $list) { if ($e) { $to.Add([int[]]$e.rect) } } }
function ScreenMasks($id) {
	$ign = New-Object 'System.Collections.Generic.List[int[]]'
	$only = New-Object 'System.Collections.Generic.List[int[]]'
	$ids = @($id)
	if ($id -like '*_help') { $ids = @($id -replace '_help$', '') + $id }
	foreach ($i in $ids) {
		$s = $masks.screens.$i
		if ($s) { AddRects $ign $s.ignore; AddRects $only $s.only }
	}
	if ($masks.ourCursor -and $masks.ourCursor[2] -gt 0) { $ign.Add([int[]]$masks.ourCursor) }
	@{ ignore = $ign.ToArray(); only = $only.ToArray() }
}

function Rel($path) {
	$from = New-Object Uri ($Out.TrimEnd('\') + '\')
	[Uri]::UnescapeDataString($from.MakeRelativeUri((New-Object Uri $path)).ToString())
}

# --- compare -----------------------------------------------------------------------------------------------------
$diffDir = Join-Path $Out 'diff'
$oursDir = Join-Path $Out 'ours'
New-Item -ItemType Directory -Force $diffDir, $oursDir | Out-Null
$report = @('# FS1 retail visual check', '', "Run $(Get-Date -Format 'yyyy-MM-dd HH:mm'), shots: $Shots, masks: $maskFile", '',
	'| Screen | Result |', '|---|---|')
$todo = @('# FS1 retail visual check: TODO', '', "From the run of $(Get-Date -Format 'yyyy-MM-dd HH:mm'). Fix later; not a gate.", '')
$setup = @()
$counts = @{ same = 0; differs = 0; missing = 0; error = 0 }

foreach ($ref in (Get-ChildItem $refRoot -Recurse -Filter '*.png' | Where-Object { $_.Name -notlike '*.mask.png' } | Sort-Object FullName)) {
	$id = $ref.BaseName
	$cat = $ref.Directory.Name
	if (-not $shot.ContainsKey($id)) {
		$why = if ($said.ContainsKey($id)) { "not captured (tour: $($said[$id]))" } else { 'not reached' }
		$report += "| $cat/$id | $why |"
		$counts.missing++
		continue
	}
	New-Item -ItemType Directory -Force (Join-Path $oursDir $cat) | Out-Null
	$ours = Join-Path $oursDir "$cat\$id.png"
	[VisualCompare]::ToPng($shot[$id], $ours)
	$m = ScreenMasks $id
	$maskPng = Join-Path $ref.DirectoryName "$id.mask.png"
	if (-not (Test-Path $maskPng)) { $maskPng = $null }
	$diff = Join-Path $diffDir "$id.png"
	$r = [VisualCompare]::Compare($ref.FullName, $ours, $m.ignore, $m.only, $maskPng,
		[int]$masks.threshold, [int]$masks.minPixels, [int]$masks.maxShift, $diff)
	if ($r.Error) {
		$setup += "- $cat/${id}: $($r.Error)"
		$report += "| $cat/$id | setup error: $($r.Error) |"
		$counts.error++
		continue
	}
	if ($r.Regions.Count -eq 0) {
		Remove-Item $diff -ErrorAction SilentlyContinue
		$report += "| $cat/$id | same |"
		$counts.same++
		continue
	}
	$counts.differs++
	$report += "| $cat/$id | $($r.Regions.Count) difference(s), [diff]($(Rel $diff)) |"
	foreach ($g in $r.Regions) {
		$todo += "- [ ] **$cat/$id** #$($g.Index): $($g.Text)"
		$todo += "  [reference]($(Rel $ref.FullName)) | [ours]($(Rel $ours)) | [diff]($(Rel $diff))"
	}
}

# shots with no reference (an extra screen, or a misnamed one)
$refIds = @(Get-ChildItem $refRoot -Recurse -Filter '*.png' | ForEach-Object { $_.BaseName })
$extra = @($shot.Keys | Where-Object { $refIds -notcontains $_ } | Sort-Object)

$head = @("Same $($counts.same), differ $($counts.differs), not captured $($counts.missing), setup errors $($counts.error).", '')
if ($setup.Count) { $head += @('## Setup errors', '', 'Not visual differences: the shot is not comparable (size or mode).', '') + $setup + '' }
if ($extra.Count) { $head += @("Shots with no reference: $($extra -join ', ')", '') }
$report = $report[0..3] + $head + $report[4..($report.Count - 1)]
Set-Content -Encoding UTF8 (Join-Path $Out 'report.md') $report
Set-Content -Encoding UTF8 (Join-Path $Out 'TODO.md') $todo
"$($head[0]) Report: $(Join-Path $Out 'report.md')"
