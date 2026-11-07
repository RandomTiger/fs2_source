# The debriefing's forced awards (-at_awards, docs/AUTOTEST.md): one plain tour, then one with awards for each config.
# PASS needs every tour to pass, the awards named once in each awards log and never in the plain one, the same screens
# in the same order, no Assert or Int3 in Debug, and the pilot files unchanged by the awards.
# Usage: awards_check.ps1 [-GameDir <install>] [-Configs r,d]    A set whose exes are there. Run one set at a time.
#        awards_check.ps1 -CheckLog <autotest.log>               Only the awards checks on one log, to test this script.
param([string]$GameDir = $env:PATH_FS2, [string[]]$Configs = @('r', 'd'), [string]$CheckLog)
. "$PSScriptRoot\common.ps1"
$Configs = $Configs | ForEach-Object { $_ -split ',' }

function Get-Screens($log) { @(Select-String $log -Pattern "Screen '([^']+)':" | ForEach-Object { $_.Matches[0].Groups[1].Value }) }
function Test-AwardsLog($log) {
	$named = @(Select-String $log -Pattern "Menu tour: awards medal '[^']+', rank '[^']+', badge '[^']+'").Count
	$skip = @(Select-String $log -Pattern 'Menu tour: awards SKIP').Count
	if ($named -ne 1) { "awards line $named times in $log" }
	if ($skip) { "awards SKIP in $log" }
}

if ($CheckLog) {
	$bad = @(Test-AwardsLog $CheckLog)
	if ($bad) { $bad; 'awards_check: FAIL'; exit 1 }
	'awards_check: PASS'
	exit 0
}

if (-not $GameDir) { 'FAIL environment variable PATH_FS2 is not set (see the machine profile)'; exit 1 }
function Get-PilotHash { (Get-ChildItem "$GameDir\data\players" -Recurse -File | Sort-Object FullName | Get-FileHash -Algorithm SHA256 | ForEach-Object { $_.Hash }) -join ',' }
function Invoke-Tour($out, $config, $extra) {
	Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
	$last = & powershell -NoProfile -ExecutionPolicy Bypass -File "$RepoRoot\tools\menu_check.ps1" -GameDir $GameDir -Configs $config -Out $out @extra | Select-Object -Last 1
	if ("$last" -notmatch 'DONE pass=\d+ fail=0') { "tour $config $($extra -join ' '): $last" }
}

$bad = @()
$base = Join-Path $env:TEMP 'awards_check'
$plainDir = "$base\plain"
$bad += @(Invoke-Tour $plainDir 'd' @())
$plainLog = "$plainDir\menus_d.autotest.log"
$screens = Get-Screens $plainLog
if (Select-String $plainLog -Pattern 'Menu tour: awards' -Quiet) { $bad += 'awards line in the plain tour' }
$pilots = Get-PilotHash
foreach ($c in $Configs) {
	$dir = "$base\awards_$c"
	$bad += @(Invoke-Tour $dir $c @('-Awards', 'mrb'))
	$log = "$dir\menus_$c.autotest.log"
	$bad += @(Test-AwardsLog $log)
	if ((Get-Screens $log) -join '|' -ne ($screens -join '|')) { $bad += "screens differ from the plain tour: $c" }
	if ((Get-PilotHash) -ne $pilots) { $bad += "pilot files changed: $c" }
	if (($c -eq 'd') -and -not (Select-String $log -Pattern 'Asserts 0, Int3 0' -Quiet)) { $bad += 'Assert or Int3 in Debug' }
}
if ($bad) { $bad; 'awards_check: FAIL'; exit 1 }
'awards_check: PASS'
