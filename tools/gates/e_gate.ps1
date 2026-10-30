param([string]$Label = 'gate', [switch]$Full, [string]$Repo = '')
. "$PSScriptRoot\common.ps1"
Require-Env PATH_FS2
Initialize-Fs2DemoScratch	# FS2_PROCDUMP (optional) = procdump.exe, for crash dumps (#74)
if (-not $Repo) { $Repo = $RepoRoot }
"warning_check: " + (& powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\warning_check.ps1" -RepoDir $Repo | Select-Object -Last 1)
$sp=$PSScriptRoot
$r=$Repo
Get-ChildItem $r -Recurse -Filter code.lib | Remove-Item
Remove-Item "$env:PATH_FS2\FS2vs2022d.exe", "$env:PATH_FS2\FS2vs2022r.exe" -ErrorAction SilentlyContinue
& "$sp\build.ps1" -Repo $r -TraceDiff 2>&1 | Select-String 'exit'
foreach ($c in "Release","Debug") { "$c warnings: " + @(Select-String "$env:TEMP\fs2build\$c.log" -Pattern "warning [A-Z]+[0-9]+").Count }
# the demo replays run the exe in FS2DEMO_TEST, which every branch builds into: build it here so no other branch's exe is tested
$d = & powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\build_demo.ps1" | Select-Object -Last 3
"demo build: $($d -join ' / ')"
if ($Full) { $m = @() } else { $m = @(Get-ChildItem "$r\recordings\autotest" -Filter 'Autotest*.fs2rec' | ForEach-Object { $_.BaseName }) + @('sm1-01','sm3-06') }
$ma = if ($m) { @('-Missions', ($m -join ',')) } else { @() }
Remove-Item "$env:TEMP\w_$Label\summary.txt" -ErrorAction SilentlyContinue	# replay_check appends; old FAIL lines would show again
"quick: " + (& powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\replay_check.ps1" -Configs r,d -CrossCheck 'sm1-01,sm3-06' @ma -Out "$env:TEMP\w_$Label" | Select-Object -Last 1)
Select-String "$env:TEMP\w_$Label\summary.txt" -Pattern '^FAIL' | ForEach-Object { $_.Line.Substring(0, [Math]::Min(100, $_.Line.Length)) }
"tools: " + (& powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\tools_check.ps1" -Game $env:PATH_FS2 | Select-Object -Last 1)
