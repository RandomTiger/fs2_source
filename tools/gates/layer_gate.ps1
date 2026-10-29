# x86 layer gate: suite_check -Full (all 48 rendered, nodraw, headless r/d, recordings\fs1, tools, warnings), then fs1demo_gate.
param([string]$Repo, [string]$Tag)
. "$PSScriptRoot\common.ps1"
$OutRoot = if ($env:FS2_GATE_OUT) { $env:FS2_GATE_OUT } else { Join-Path $env:TEMP 'fs2_gates' }; New-Item -ItemType Directory -Force $OutRoot | Out-Null
Require-Env PATH_FS2, PATH_FS1
Initialize-Fs2DemoScratch; Initialize-Fs1DemoScratch	# FS2_PROCDUMP (optional) = procdump.exe, for crash dumps
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
if (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { 'FAIL the game is running'; "$Tag GATE DONE"; exit 1 }
git -C $Repo log --oneline -1
foreach ($d in $env:PATH_FS2, $env:PATH_FS1, $env:FS1DEMO_PATH) { Remove-Item "$d\FS2vs2022r.exe", "$d\FS2vs2022d.exe" -ErrorAction SilentlyContinue }
Get-ChildItem $Repo -Recurse -Filter code.lib | Remove-Item
$o = Join-Path $OutRoot "${Tag}_suite_full"; Remove-Item -Recurse -Force $o -ErrorAction SilentlyContinue
'--- suite_check -Full ' + (Get-Date -Format HH:mm)
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Repo 'tools\suite_check.ps1') -Full -Out $o | Select-Object -Last 3
Get-Content "$o\summary.txt"
'--- fs1demo_gate ' + (Get-Date -Format HH:mm)
$g = Join-Path $OutRoot "${Tag}_fs1demo_gate"; Remove-Item -Recurse -Force $g -ErrorAction SilentlyContinue
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Repo 'tools\fs1demo_gate.ps1') -Out $g | Select-Object -Last 15
Get-ChildItem $g -Recurse -Filter 'btmdemo_d*.log' | ForEach-Object {
	$a = @(Select-String $_.FullName -Pattern '^\[[ \d.]+\] (Warning|Error|Assert|Int3)')
	"btmdemo Debug ($($_.Name)) warning/error/assert/int3 lines: $($a.Count)"
	$a | ForEach-Object { $_.Line -replace '^\[[ \d.]+\] ', '' } | Group-Object | ForEach-Object { "  $($_.Count) x $($_.Name.Substring(0,[Math]::Min(200,$_.Name.Length)))" }
}
"$Tag GATE DONE " + (Get-Date -Format HH:mm)
