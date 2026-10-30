<#
L1 smoke gate (GATING_PLAN_v2, D-20261027-102606), native x86 side. For PrePort32 and above; the run ends "L1 green" or "L1 FAIL".
Not the gate: only L2 (layer_gate.ps1 / p_gate.ps1 -Full) is called "the gate".

  l1_gate.ps1 -Repo <worktree> [-Label x] [-Seed n] [-Only a,fs1/sm1-01,...] [-NoBuild] [-Out dir]

Runs, Release and Debug, TraceDiff 0 (FS1 demo: clean-run rule D-20261026-203028):
  1. build Release + Debug (build.ps1), 0 warnings; the FS2 demo build (tools\build_demo.ps1)
  2. menu tours: FS2 retail and FS1 retail (tools\menu_tour_all.ps1)
  3. feature autotests, the fixed FS1 x4 + FS2 x4 + one mission per demo set (l1_set.txt, owned by the manager)
  4. 3 random missions from the rest of the pool; the seed is printed first and logged in l1_runs.log in the gate output folder
     (rerun the same picks with -Seed). A random fail is a real fail.
-Only runs just those missions (name, or set/name when two sets share it) and nothing else: the walk-back step
("L1 of just the failing mission per commit"). -NoBuild skips step 1 (the exes must already be built from this commit).
On a fail it writes failure.txt in the output folder: mission, set, config, first diverging frame, TraceDiff lines.
Needs the machine to itself: it runs the game; do not build or run it elsewhere meanwhile.
#>
param(
	[Parameter(Mandatory)][string]$Repo,
	[string]$Label = 'L1',
	[int]$Seed = 0,
	[string[]]$Only,
	[switch]$NoBuild,
	[string]$Out,
	[string]$SetFile
)
$sp = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $SetFile) { $SetFile = Join-Path $sp 'l1_set.txt' }
. "$sp\l1_lib.ps1"
$start = Get-Date
. "$sp\common.ps1"
$OutRoot = if ($env:FS2_GATE_OUT) { $env:FS2_GATE_OUT } else { Join-Path $env:TEMP 'fs2_gates' }; New-Item -ItemType Directory -Force $OutRoot | Out-Null
Require-Env PATH_FS2, PATH_FS1
Initialize-Fs2DemoScratch; Initialize-Fs1DemoScratch
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
$fs1 = $env:PATH_FS1; $fs1d = $env:FS1DEMO_PATH
if (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { 'L1 FAIL the game is running'; exit 1 }
if (-not $Out) { $Out = Join-Path $OutRoot ("${Label}_" + $start.ToString('yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$td = Join-Path $Repo 'tools\bin\TraceDiff.exe'
$plan = Get-L1Plan $Repo $SetFile $Seed $Only
$commit = (git -C $Repo log --oneline -1)
"L1 start $($start.ToString('HH:mm')), $Repo, $commit"
Show-L1Plan $plan

$steps = @(); $fails = @()
function Step($name, $ok, $detail) { $script:steps += "$name`: $detail"; if (-not $ok) { $script:fails += [pscustomobject]@{ Mission = $name; Set = '-'; Config = '-'; Frame = ''; Text = $detail } } }
function Fail($mission, $set, $config, $frame, $text) { $script:fails += [pscustomobject]@{ Mission = $mission; Set = $set; Config = $config; Frame = $frame; Text = $text } }
function RunPs($f, $a) { @(& powershell -NoProfile -ExecutionPolicy Bypass -File $f @a 2>&1 | ForEach-Object { "$_" }) }
$setOf = @{ autotest = 'fs2'; fs1 = 'fs1'; demo = 'fs2demo' }
function Finish {
	$end = Get-Date
	$r = @("L1 summary: start $($start.ToString('HH:mm')), end $($end.ToString('HH:mm')), $commit", "seed $($plan.Seed)  random: $(@($plan.Random | ForEach-Object { "$($_.Set)/$($_.Name)" }) -join ', ')") + $steps
	if ($fails) {
		$r += "L1 FAIL ($($fails.Count) failures) - close gates on this branch; walk back with -Only <mission>"
		$ft = foreach ($f in $fails) { "FAIL mission $($f.Mission) | set $($f.Set) | config $($f.Config) | first diverging frame $(if ($f.Frame) { $f.Frame } else { 'n/a' })`n      $($f.Text)" }
		$ft | Set-Content (Join-Path $Out 'failure.txt')
		$r += $ft | ForEach-Object { "  $_" }
	} else { $r += 'L1 green' }
	$r | Set-Content (Join-Path $Out 'l1_summary.txt')
	Add-Content (Join-Path $OutRoot 'l1_runs.log') "$($start.ToString('yyyy-MM-dd HH:mm')) native $Repo seed $($plan.Seed) random $(@($plan.Random | ForEach-Object { "$($_.Set)/$($_.Name)" }) -join ',') -> $(if ($fails) { 'FAIL' } else { 'green' })"
	'----'
	$r
	"output: $Out"
	if ($fails) { exit 1 } else { exit 0 }
}

# 1. build (L0)
if (-not $NoBuild) {
	"build $((Get-Date).ToString('HH:mm'))"
	foreach ($d in $env:PATH_FS2, $fs1, $fs1d) { Remove-Item "$d\FS2vs2022r.exe", "$d\FS2vs2022d.exe" -ErrorAction SilentlyContinue }
	Get-ChildItem $Repo -Recurse -Filter code.lib | Remove-Item
	$b = RunPs "$sp\build.ps1" @('-Repo', $Repo, '-TraceDiff')
	foreach ($c in 'Release', 'Debug') {
		$w = @(Select-String "$env:TEMP\fs2build\$c.log" -Pattern 'warning [A-Z]+[0-9]+' -ErrorAction SilentlyContinue).Count
		$e = @($b | Where-Object { $_ -match "^$c exit (\d+)" })
		$ok = ($e.Count -eq 1) -and ($e[0] -match 'exit 0$') -and ($w -eq 0)
		Step "build $c" $ok "$($e -join ''), $w warnings"
	}
	if (-not ($b -match '^TraceDiff exit 0')) { Step 'build TraceDiff' $false ($b -join ' / ') }
	$d = RunPs (Join-Path $Repo 'tools\build_demo.ps1') @()
	Step 'demo build' ($d[-1] -match '^PASS') "$($d[-1])"
	if ($fails) { Finish }
}
foreach ($x in 'FS2vs2022r.exe', 'FS2vs2022d.exe') {
	foreach ($d in $fs1, $fs1d) { if ((Test-Path $d) -and (Test-Path "$env:PATH_FS2\$x")) { Copy-Item "$env:PATH_FS2\$x" $d -Force } }
}

# 2. menu tours: FS2 retail and FS1 retail, Release and Debug, with the exes just built (-NoBuild: menu_tour_all would rebuild)
if (-not $plan.Only) {
	"tours $((Get-Date).ToString('HH:mm'))"
	$t = RunPs "$($Repo)\tools\menu_tour_all.ps1" @('-Sets', 'fs2,fs1', '-Configs', 'r,d', '-NoBuild', '-Fs2Dir', $env:PATH_FS2, '-Fs1Dir', $fs1, '-Out', "$Out\tour")
	$t | Set-Content "$Out\tour.txt"
	foreach ($l in $t) {
		if ($l -match "^(fs2|fs1) (r|d) Screen '([^']+)': FAIL (.*)") { Fail "menu screen $($Matches[3])" $Matches[1] $Matches[2] '' "menu tour screen failed: $($Matches[4])" }
		elseif ($l -match '^(fs2|fs1) (r|d) exit=(?!0\b)') { Fail 'menu tour' $Matches[1] $Matches[2] '' $l }
		elseif ($l -match '^(FAIL|SKIP) ') { Fail 'menu tour' '-' '-' '' "$l (a skip is not green)" }
	}
	$done = $t | Where-Object { $_ -match '^DONE pass=(\d+) fail=(\d+)' } | Select-Object -Last 1
	$ends = @($t | Where-Object { $_ -match '^(fs2|fs1) (r|d) exit=0 Menu tour:' }).Count
	Step 'menu tours (FS2, FS1)' (($done -eq 'DONE pass=4 fail=0') -and ($ends -eq 4)) "$done, $ends of 4 builds finished clean"
}

# 3+4. replays: feature tests, fixed and random missions. FS2 retail + FS2 demo in one replay_check (no FS1 dir, so its
# same-named recordings stay out), FS1 retail in another (its names never match recordings\autotest).
function Replay($label, $missions, $fs1Dir) {
	if (-not $missions) { return }
	"replay $label ($($missions.Count) missions) $((Get-Date).ToString('HH:mm'))"
	$o = "$Out\replay_$label"
	$env:PATH_FS1 = $fs1Dir
	$a = @('-Configs', 'r,d', '-CrossCheck', 'sm1-01', '-Missions', ($missions -join ','), '-Out', $o, '-GameDir', $env:PATH_FS2, '-DemoDir', $env:FS2DEMO_TEST)
	if ($fs1Dir) { $a += '-Fs1Dir', $fs1Dir }	# none for the FS2 run: its same-named FS1 recordings stay out
	$r = RunPs "$($Repo)\tools\replay_check.ps1" $a
	$env:PATH_FS1 = $fs1
	$bad = @(Read-ReplayFailures "$o\summary.txt")
	foreach ($f in $bad) {
		$set = if ($f.Folder) { $setOf[$f.Folder] } else { '?' }
		$frame = ''; $txt = $f.Why
		if ($f.Folder -and ($f.Config -match '^(r|d)$') -and ($f.Why -match 'DIVERGED|NO TRACE')) {
			$d = Get-L1Divergence $td (Join-Path $Repo "recordings\$($f.Folder)\$($f.Name).trace") "$o\$($f.Folder)_$($f.Name)_$($f.Config).trace"
			$frame = $d.Frame; $txt += "`n      " + $d.Text
		}
		Fail $f.Name $set $f.Config $frame $txt
	}
	$done = $r | Where-Object { $_ -match '^DONE ' } | Select-Object -Last 1
	if (-not $done) { Fail "replay $label" '-' '-' '' "replay_check did not finish: $(($r | Select-Object -Last 3) -join ' / ')" }
	$script:steps += "replay $label`: $done"
	if (($done -notmatch 'fail=0') -and -not $bad) { Fail "replay $label" '-' '-' '' "$done" }
}
$fs2List = @($plan.Features + $plan.Fs2 + $plan.Fs2Demo | Select-Object -Unique)
if ($plan.Fs2.Count + $plan.Fs2Demo.Count + $plan.Features.Count) { Replay 'fs2' $fs2List '' }
if ($plan.Fs1.Count) { Replay 'fs1' @($plan.Fs1) $fs1 }

# FS1 demo: no recording, so the clean-run rule (exit 0, no Assert/Int3/Error/Warning), Release and Debug
if ($plan.Fs1Demo.Count) {
	"fs1demo $((Get-Date).ToString('HH:mm'))"
	$r = RunPs "$($Repo)\tools\fs1demo_run.ps1" @('-Missions', ($plan.Fs1Demo -join ','), '-Configs', 'r,d', '-GameDir', $fs1d, '-Out', "$Out\fs1demo")
	foreach ($l in $r) { if ($l -match '^(\S+) (r|d) (TIMEOUT|exit=(?!0\b)|NO LOG)') { Fail $Matches[1] 'fs1demo' $Matches[2] '' "$l (clean-run rule, no trace)" } }
	if (($r | Select-Object -Last 1) -ne 'PASS') {
		if (-not ($fails | Where-Object { $_.Set -eq 'fs1demo' })) { Fail ($plan.Fs1Demo -join ',') 'fs1demo' 'r/d' '' "fs1demo_run: $(($r | Select-Object -Last 3) -join ' / ')" }
	}
	$script:steps += "fs1demo $($plan.Fs1Demo -join ','): $($r | Select-Object -Last 1)"
}
Finish
