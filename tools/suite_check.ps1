<#
The PrePort suite (decision D-042): the checks over one set of recordings.

  quick (default)  before each commit, about 25 minutes:
                     - the test missions (recordings\autotest\Autotest*)
                     - the campaign missions that have broken before (recordings\regression.txt)
                     - the day's guests: -Guests recordings from each data set (FreeSpace 2 campaign, FreeSpace 2 demo,
                       and later FreeSpace 1 and its demo), a different few each day so every recording comes round
                     - the menu tour (tools\menu_check.ps1)
  -Full            once a day and before a branch is called done, about 3 hours: every recording, every check, plus the
                   tools check and the no-warnings build. Note the date and the result in STATUS.md, or it did not happen.

First, the game builds in Release and Debug (incremental, into GameDir) with no compiler, linker or MSBuild warning:
the per-commit half of the zero-warnings gate (D-20261019-235231). Then tools\build_demo.ps1 builds the FS2_DEMO exe into FS2DEMO_TEST (SKIP without it); demo recordings replay there.
Checks:
  replay     tools\replay_check.ps1 in Release and Debug, with sm1-01 and sm3-06 also compared across the builds
  nodraw     tools\nodraw_check.ps1, Release (retail recordings)
  headless   tools\nodraw_check.ps1 -Headless in Release and Debug (retail recordings)
  menus      tools\menu_check.ps1 in Release and Debug
  restart    tools\restart_check.ps1: sm1-01 then sm3-06 in one process, Release and Debug (D-055)
  -Full only:
  model      tools\sound_model_check.ps1 (no answer from the sound device)
  tools      tools\tools_check.ps1: every tool project builds in Release and does its basic job. It sends keys to FRED,
             so leave the keyboard alone while it runs (about 3 minutes).
  warnings   tools\warning_check.ps1: every project builds in Release and Debug with no warning (D-20261019-024006;
             about 25 minutes, exes to its own folder).

Usage (PowerShell, from anywhere):
  tools\suite_check.ps1 [-Full] [-Out <dir>] [-GameDir <dir>] [-Guests 2] [-Day <n>]

-Day picks the guests (default: today's day of the year), so a failing run can be repeated with the same set.
A recording that fails from a code regression joins recordings\regression.txt, with why, when the fix lands
(D-20261019-235232). Copy the guest line into the commit's STATUS.md note.
Don't build or run the game from another session while this runs.
Results go to <Out>\summary.txt (default replays\suite_<quick|full>_<date-time>), one line per check, then PASS or FAIL.
#>
param(
	[switch]$Full,
	[string]$Out,
	[string]$GameDir = $env:PATH_FS2,
	[int]$Guests = 2,
	[int]$Day = (Get-Date).DayOfYear
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
$set = if ($Full) { 'full' } else { 'quick' }
if (-not $Out) { $Out = Join-Path $repo ("replays\suite_${set}_" + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$summary = Join-Path $Out 'summary.txt'

function Get-Names($dir, $filter = '*.fs2rec') {
	@(Get-ChildItem (Join-Path $repo "recordings\$dir") -Filter $filter -ErrorAction SilentlyContinue | ForEach-Object { $_.BaseName } | Sort-Object)
}
# $Guests names from a data set's list, starting at a place that moves on each day
function Get-Guests($names) {
	if (-not $names) { return @() }
	$n = [Math]::Min($Guests, $names.Count)
	$start = ($Day * $Guests) % $names.Count
	@(0..($n - 1) | ForEach-Object { $names[($start + $_) % $names.Count] })
}

$retail = @(); $demo = @(); $guestLine = ''
if (-not $Full) {
	$custom = Get-Names 'autotest' 'Autotest*.fs2rec'
	$regression = @(Get-Content (Join-Path $repo 'recordings\regression.txt') | ForEach-Object { ($_ -replace '#.*', '').Trim() } | Where-Object { $_ })
	# a data set's guests come from its recordings that are not already in the set
	$campaign = @(Get-Names 'autotest' | Where-Object { ($custom -notcontains $_) -and ($regression -notcontains $_) })
	$fs2Guests = Get-Guests $campaign
	$demoGuests = Get-Guests (Get-Names 'demo')
	$retail = $custom + $regression + $fs2Guests
	$demo = $demoGuests
	$guestLine = "guests (day $Day): FreeSpace 2 $($fs2Guests -join ', '); FreeSpace 2 demo $($demoGuests -join ', ')"
}
$m = @(); $mReplay = @()
if ($retail) {
	$m = @('-Missions', ($retail -join ','))
	$mReplay = @('-Missions', (($retail + $demo) -join ','))
}

$checks = @(
	@{ Name = 'replay';     Script = 'replay_check.ps1';      Args = @('-Configs', 'r,d', '-CrossCheck', 'sm1-01,sm3-06') + $mReplay },
	@{ Name = 'nodraw';     Script = 'nodraw_check.ps1';      Args = @() + $m },
	@{ Name = 'headless_r'; Script = 'nodraw_check.ps1';      Args = @('-Headless') + $m },
	@{ Name = 'headless_d'; Script = 'nodraw_check.ps1';      Args = @('-Headless', '-Config', 'd') + $m },
	@{ Name = 'menus';      Script = 'menu_check.ps1';        Args = @() },
	@{ Name = 'restart';    Script = 'restart_check.ps1';     Args = @() }
)
if ($Full) {
	$checks += @{ Name = 'model'; Script = 'sound_model_check.ps1'; Args = @() }
}

"Suite ($set), $(Get-Date -Format 'yyyy-MM-dd HH:mm'), $(if ($retail) { ($retail + $demo).Count } else { 'all' }) recordings" | Set-Content $summary
if ($guestLine) { $guestLine | Add-Content $summary; $guestLine }
$failed = 0; $total = 0
function Add-Result($name, $last, $ok) {
	$script:total++
	if (-not $ok) { $script:failed++ }
	$line = "${name}: $last"
	$line | Add-Content $summary
	$line
}
# the game, incremental: only what the commit changed recompiles, so its warnings show (warning_check.ps1 is the clean
# build of every project, in -Full). The exe is deleted first: MSBuild does not relink an up-to-date worktree, and
# another worktree's exe may be in GameDir.
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
$env:PATH_FS2 = $GameDir
foreach ($c in 'Release', 'Debug') {
	$blog = Join-Path $Out "build_$c.log"
	Remove-Item (Join-Path $GameDir ($(if ($c -eq 'Release') { 'FS2vs2022r.exe' } else { 'FS2vs2022d.exe' }))) -ErrorAction SilentlyContinue
	& $msb (Join-Path $repo 'Freespace2.vcxproj') "/p:Configuration=$c" /p:Platform=Win32 "/p:SolutionDir=$repo\" /m /v:m /nologo > $blog 2>&1
	$rc = $LASTEXITCODE
	$warn = @(Select-String $blog -Pattern ': warning [A-Z]+\d+' | ForEach-Object { $_.Line.Trim() } | Sort-Object -Unique)
	$warn | Select-Object -First 10
	Add-Result "build $c" "exit $rc, $($warn.Count) warnings" ((-not $rc) -and ($warn.Count -eq 0))
}
# the FS2_DEMO exe, built from this checkout into FS2DEMO_TEST, so a demo build that does not compile fails the suite
# (D-20261019-175929, 213450). Without FS2DEMO_TEST it is a SKIP, which no merge-up or branch-done claim may rest on.
if ($env:FS2DEMO_TEST) {
	$last = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build_demo.ps1') | Select-Object -Last 1
	Add-Result 'demo build' $last ((-not $LASTEXITCODE) -and ($last -match '^PASS'))
} else {
	'demo build: SKIP (FS2DEMO_TEST not set)' | Add-Content $summary
}
foreach ($c in $checks) {
	$a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot $c.Script), '-GameDir', $GameDir, '-Out', (Join-Path $Out $c.Name)) + $c.Args
	$last = & powershell @a | Select-Object -Last 1
	Add-Result $c.Name $last ((-not $LASTEXITCODE) -and ($last -match 'fail=0'))
}
if ($Full) {
	# every tool builds and does its basic job (tools\tools_check.ps1; needs the desktop, sends keys to FRED)
	$last = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'tools_check.ps1') -Game $GameDir | Select-Object -Last 1
	Add-Result 'tools' $last ((-not $LASTEXITCODE) -and ($last -match '^PASS'))
	# no compiler, linker or MSBuild warning in any project (tools\warning_check.ps1)
	$last = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'warning_check.ps1') -RepoDir $repo -Out (Join-Path $env:LOCALAPPDATA 'fs2_warning_check') | Select-Object -Last 1
	Add-Result 'warnings' $last ((-not $LASTEXITCODE) -and ($last -match '^PASS'))
}
$verdict = if ($failed) { "FAIL ($failed of $total checks)" } else { 'PASS' }
$verdict | Add-Content $summary
$verdict
if ($failed) { exit 1 }
