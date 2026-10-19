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
Then, once per run, tools\tools_check.ps1: every tool project builds in Release and does its basic job. It sends keys
to FRED, so leave the keyboard alone while it runs (about 3 minutes). Last, tools\warning_check.ps1: every project
builds in Release and Debug with no warning (D-20261019-024006; about 25 minutes, exes to its own folder).

Usage (PowerShell, from anywhere):
  tools\suite_check.ps1 [-Full] [-Out <dir>] [-GameDir <dir>]

Build Release and Debug first, and don't build or run the game from another session while this runs.
Results go to <Out>\summary.txt (default replays\suite_<quick|full>_<date-time>), one line per check, then PASS or FAIL.
About 50 minutes quick, 3 hours full.
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
# every tool builds and does its basic job (tools\tools_check.ps1; needs the desktop, sends keys to FRED)
$last = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'tools_check.ps1') -Game $GameDir | Select-Object -Last 1
if ($LASTEXITCODE -or ($last -notmatch '^PASS')) { $failed++ }
$line = "tools: $last"
$line | Add-Content $summary
$line
# no compiler, linker or MSBuild warning in any project (tools\warning_check.ps1)
$last = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'warning_check.ps1') -RepoDir $repo -Out (Join-Path $env:LOCALAPPDATA 'fs2_warning_check') | Select-Object -Last 1
if ($LASTEXITCODE -or ($last -notmatch '^PASS')) { $failed++ }
$line = "warnings: $last"
$line | Add-Content $summary
$line
$verdict = if ($failed) { "FAIL ($failed of $($checks.Count + 2) checks)" } else { 'PASS' }
$verdict | Add-Content $summary
$verdict
if ($failed) { exit 1 }
