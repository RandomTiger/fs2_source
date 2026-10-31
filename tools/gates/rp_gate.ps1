$sp=$PSScriptRoot
. "$PSScriptRoot\common.ps1"
Require-Env PATH_FS2
$r=$RepoRoot
Remove-Item "$env:PATH_FS2\FS2vs2022d.exe", "$env:PATH_FS2\FS2vs2022r.exe" -ErrorAction SilentlyContinue
& "$sp\build.ps1" -Repo $r -TraceDiff 2>&1 | Select-String 'exit'
$m = @(Get-ChildItem "$r\recordings\autotest" -Filter 'Autotest*.fs2rec' | ForEach-Object { $_.BaseName }) + @('sm1-01','sm3-06')
"quick: " + (& powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\replay_check.ps1" -Configs r,d -CrossCheck 'sm1-01,sm3-06' -Missions ($m -join ',') -Out "$env:TEMP\rp_quick" | Select-Object -Last 1)
"tools: " + (& powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\tools_check.ps1" -Game $env:PATH_FS2 | Select-Object -Last 1)
