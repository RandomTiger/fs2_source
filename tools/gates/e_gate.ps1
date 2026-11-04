param([string]$Label = 'gate', [switch]$Full, [string]$Repo = '')
. "$PSScriptRoot\common.ps1"
Require-Env PATH_FS2
Initialize-Fs2DemoScratch	# FS2_PROCDUMP (optional) = procdump.exe, for crash dumps (#74)
if (-not $Repo) { $Repo = $RepoRoot }
$bad = @()	# steps that failed: any one makes the gate exit 1 (#142)
$w = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\warning_check.ps1" -RepoDir $Repo | Select-Object -Last 1
"warning_check: $w"; if ($w -notmatch 'PASS') { $bad += 'warning_check' }
$sp=$PSScriptRoot
$r=$Repo
Get-ChildItem $r -Recurse -Filter code.lib | Remove-Item
$exeDirs = @($env:PATH_FS2) + @($env:PATH_FS1 | Where-Object { $_ -and (Test-Path $_) })	# the FS1 replays run the exe in PATH_FS1
foreach ($x in $exeDirs) { Remove-Item "$x\$(Get-FS2Exe d $r)", "$x\$(Get-FS2Exe r $r)" -ErrorAction SilentlyContinue }
& "$sp\build.ps1" -Repo $r -TraceDiff 2>&1 | Select-String 'exit'; if ($LASTEXITCODE) { $bad += 'build' }
if ($env:PATH_FS1 -and (Test-Path $env:PATH_FS1)) { foreach ($e in 'r', 'd') { Copy-Item "$env:PATH_FS2\$(Get-FS2Exe $e $r)" $env:PATH_FS1 -Force -ErrorAction SilentlyContinue } }
foreach ($c in "Release","Debug") { "$c warnings: " + @(Select-String "$env:TEMP\fs2build\$c.log" -Pattern "warning [A-Z]+[0-9]+").Count }
# the demo replays run the exe in PATH_FS2DEMO, which every branch builds into: build it here so no other branch's exe is tested
$d = & powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\build_demo.ps1" | Select-Object -Last 3
"demo build: $($d -join ' / ')"; if (($d | Select-Object -Last 1) -notmatch 'PASS') { $bad += 'demo build' }
if ($Full) { $m = @() } else { $m = @(Get-ChildItem "$r\recordings\autotest" -Filter 'Autotest*.fs2rec' | ForEach-Object { $_.BaseName }) + @('sm1-01','sm3-06') }
$ma = if ($m) { @('-Missions', ($m -join ',')) } else { @() }
Remove-Item "$env:TEMP\w_$Label\summary.txt" -ErrorAction SilentlyContinue	# replay_check appends; old FAIL lines would show again
$q = & powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\replay_check.ps1" -Configs r,d -CrossCheck 'sm1-01,sm3-06' @ma -Out "$env:TEMP\w_$Label" | Select-Object -Last 1
if ($LASTEXITCODE) { $bad += 'replays' }
"quick: $q"
Select-String "$env:TEMP\w_$Label\summary.txt" -Pattern '^FAIL' | ForEach-Object { $_.Line.Substring(0, [Math]::Min(100, $_.Line.Length)) }
$tc = & powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\tools_check.ps1" -Game $env:PATH_FS2 | Select-Object -Last 1
"tools: $tc"; if ($tc -notmatch 'PASS') { $bad += 'tools' }
if ($bad) { "e_gate: FAIL ($($bad -join ', '))"; exit 1 }
'e_gate: PASS'
