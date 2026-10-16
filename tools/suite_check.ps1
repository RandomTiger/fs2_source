<#
The PrePort suite (decision D-042): every check over one set of recordings.

  quick (default)  the custom test missions (recordings\autotest\Autotest*) and the campaign spot-checks sm1-01
                   and sm3-06. Run before each commit.
  -Full            every recording. Run once a day and before a branch is called done; note the date and the
                   result in STATUS.md, or it did not happen.

Each set goes through:
  replay     tools\replay_check.ps1 in Release and Debug, with sm1-01 and sm3-06 also compared across the builds
  nodraw     tools\nodraw_check.ps1, Release
  model      tools\sound_model_check.ps1 (no answer from the sound device)
  headless   tools\nodraw_check.ps1 -Headless in Release and Debug (no window, graphics or sound device)

Usage (PowerShell, from anywhere):
  tools\suite_check.ps1 [-Full] [-Out <dir>] [-GameDir <dir>]

Build Release and Debug first, and don't build or run the game from another session while this runs.
Results go to <Out>\summary.txt (default replays\suite_<quick|full>_<date-time>), one line per check, then PASS or FAIL.
About 25 minutes quick, 2.5 hours full.
#>
param(
	[switch]$Full,
	[string]$Out,
	[string]$GameDir = $env:PATH_FS2
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
$set = if ($Full) { 'full' } else { 'quick' }
if (-not $Out) { $Out = Join-Path $repo ("replays\suite_${set}_" + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$summary = Join-Path $Out 'summary.txt'

$missions = @()
if (-not $Full) {
	$missions = @(Get-ChildItem (Join-Path $repo 'recordings\autotest') -Filter 'Autotest*.fs2rec' | ForEach-Object { $_.BaseName }) + @('sm1-01', 'sm3-06')
}
$m = @()
if ($missions) { $m = @('-Missions', ($missions -join ',')) }

$checks = @(
	@{ Name = 'replay';     Script = 'replay_check.ps1';      Args = @('-Configs', 'r,d', '-CrossCheck', 'sm1-01,sm3-06') },
	@{ Name = 'nodraw';     Script = 'nodraw_check.ps1';      Args = @() },
	@{ Name = 'model';      Script = 'sound_model_check.ps1'; Args = @() },
	@{ Name = 'headless_r'; Script = 'nodraw_check.ps1';      Args = @('-Headless') },
	@{ Name = 'headless_d'; Script = 'nodraw_check.ps1';      Args = @('-Headless', '-Config', 'd') }
)

"Suite ($set), $(Get-Date -Format 'yyyy-MM-dd HH:mm'), $(if ($missions) { $missions.Count } else { 'all' }) recordings" | Set-Content $summary
$failed = 0
foreach ($c in $checks) {
	$a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot $c.Script), '-GameDir', $GameDir, '-Out', (Join-Path $Out $c.Name)) + $c.Args + $m
	$last = & powershell @a | Select-Object -Last 1
	if ($LASTEXITCODE -or ($last -notmatch 'fail=0')) { $failed++ }
	$line = "$($c.Name): $last"
	$line | Add-Content $summary
	$line
}
$verdict = if ($failed) { "FAIL ($failed of $($checks.Count) checks)" } else { 'PASS' }
$verdict | Add-Content $summary
$verdict
if ($failed) { exit 1 }
