param([string]$Repo, [string]$Label, [switch]$Full)
# Merge-up gate for PrePort32 and above: the suite (which builds the game incrementally and counts its warnings), then
# for a merge the clean all-projects warning build and the tools check (D-20261019-235231, 235232). Last line PASS or FAIL.
$sp = $PSScriptRoot
. "$sp\common.ps1"
Require-Env PATH_FS2
Initialize-Fs2DemoScratch	# FS2_PROCDUMP (optional) = procdump.exe, for crash dumps (#74)
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
Get-ChildItem $Repo -Recurse -Filter code.lib | Remove-Item
& "$sp\build.ps1" -Repo $Repo -TraceDiff -Configs @() 2>&1 | Select-String 'exit'
$f = if ($Full) { @('-Full') } else { @() }
$out = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\suite_check.ps1" @f -Out "$env:TEMP\$Label"
$out
$bad = @()
# An empty pipeline in ( ) -notmatch is an empty array, which is false: a suite that never started used to PASS here.
# Take the last line as a string, and fail when the suite wrote no replay summary.
$last = [string]($out | Where-Object { $_ -and "$_".Trim() } | Select-Object -Last 1)
if ($last -notmatch '^PASS') { $bad += 'suite' }
if (-not (Test-Path "$env:TEMP\$Label\replay\summary.txt")) { 'suite: no replay\summary.txt, the suite produced no result'; $bad += 'suite (no result)' }
Select-String "$env:TEMP\$Label\replay\summary.txt" -Pattern '^FAIL' -ErrorAction SilentlyContinue | ForEach-Object { $_.Line.Substring(0, [Math]::Min(160, $_.Line.Length)) }
if (-not $Full) {
	$t = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\tools_check.ps1" -Game $env:PATH_FS2 | Select-Object -Last 1
	"tools: $t"; if ($t -notmatch '^PASS') { $bad += 'tools' }
	$w = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\warning_check.ps1" -RepoDir $Repo -Out (Join-Path $env:LOCALAPPDATA 'fs2_warning_check') | Select-Object -Last 1
	"warning_check: $w"; if ($w -notmatch '^PASS') { $bad += 'warnings' }
}
if ($bad) { "FAIL $($bad -join ', ')" } else { 'PASS' }
