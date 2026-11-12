# Long install paths (#269): the game and replay_check run from an install whose path is longer than the original
# code's MAX_PATH_LEN (128) and near MAX_PATH (260). A directory junction at the end of a deeply nested folder
# points at the install, so nothing is copied and the game sees the long path as its working directory.
# Each length replays one short autotest recording in Release through tools\replay_check.ps1 -GameDir <long path>.
# PASS needs "DONE pass=1 fail=0" at every length; a run that prints nothing is a FAIL (#196).
# The junction is removed with rmdir (never a recursive delete, which would follow it into the install).
# 230 is the longest default: the exe name adds 25 characters, and past 260 Windows will not start the exe at all.
# Before fix review 73 (#269) Release crashed from a 78-character install path (0xC0000409 or 0xC0000005, no trace).
# Since the fix: PASS at 78, 140, 200 and 230. At 230 replay_check's Copy-Item of the test missions prints an error
# (PowerShell 5.1 cannot write past 260 characters); the replay still passes because the missions are already installed.
# Usage: longpath_check.ps1 [-GameDir <install>] [-Lengths 140,230][-Mission Autotest] [-Config r]
param([string]$GameDir = $env:PATH_FS2, [string[]]$Lengths = @('140', '230'),[string]$Mission = 'Autotest', [string]$Config = 'r')
. "$PSScriptRoot\common.ps1"
if (-not $GameDir) { 'FAIL environment variable PATH_FS2 is not set (see the machine profile)'; exit 1 }
if (-not (Test-Path "$GameDir\$(Get-FS2Exe $Config)")) { "FAIL no $(Get-FS2Exe $Config) in $GameDir (build first)"; exit 1 }
$Lengths = $Lengths | ForEach-Object { "$_" -split ',' } | ForEach-Object { [int]$_ }

$root = Join-Path $env:TEMP 'fs2_longpath'
$bad = @()
foreach ($len in $Lengths) {
	# nest 20-character folders until one more would pass the target, then pad the junction name to reach it exactly
	$dir = $root
	while (($dir.Length + 21 + 10) -le $len) { $dir = Join-Path $dir ('d' * 20) }
	$pad = $len - $dir.Length - 1
	if ($pad -lt 1) { $bad += "length ${len}: shorter than $root"; continue }
	$link = Join-Path $dir ('g' * $pad)
	New-Item -ItemType Directory -Force $dir | Out-Null
	cmd /c mklink /J "$link" "$GameDir" | Out-Null
	if (-not (Test-Path "$link\$(Get-FS2Exe $Config)")) { $bad += "length ${len}: could not make the junction $link"; continue }
	$out = Join-Path $env:TEMP "longpath_check_$len"
	Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
	$last = & powershell -NoProfile -ExecutionPolicy Bypass -File "$RepoRoot\tools\replay_check.ps1" -GameDir $link -Missions $Mission -Configs $Config -CrossCheck none -Out $out | Select-Object -Last 1
	cmd /c rmdir "$link"
	if (Test-Path $link) { $bad += "length ${len}: junction $link not removed" }
	$res = (Get-Content "$out\summary.txt" -ErrorAction SilentlyContinue | Select-Object -First 1)
	"length $len ($($link.Length) chars): $last  $res"
	if ("$last" -notmatch 'DONE pass=1 fail=0') { $bad += "length ${len}: $last $res" }
}
# only folders are left once the junctions are gone; a junction still there stops the cleanup so the install is safe
if (-not ($bad -match 'not removed')) { Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue }
if ($bad) { $bad; 'longpath_check: FAIL'; exit 1 }
'longpath_check: PASS'
