$sp=$PSScriptRoot
. "$PSScriptRoot\common.ps1"
Require-Env PATH_FS2
$r=$RepoRoot
Remove-Item "$env:PATH_FS2\$(Get-FS2Exe d $r)", "$env:PATH_FS2\$(Get-FS2Exe r $r)" -ErrorAction SilentlyContinue
$bad = @()	# steps that failed: any one makes the gate exit 1 (#142)
& "$sp\build.ps1" -Repo $r -TraceDiff 2>&1 | Select-String 'exit'; if ($LASTEXITCODE) { $bad += 'build' }
$m = @(Get-ChildItem "$r\recordings\autotest" -Filter 'Autotest*.fs2rec' | ForEach-Object { $_.BaseName }) + @('sm1-01','sm3-06')
$q = & powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\replay_check.ps1" -Configs r,d -CrossCheck 'sm1-01,sm3-06' -Missions ($m -join ',') -Out "$env:TEMP\rp_quick" | Select-Object -Last 1
if ($LASTEXITCODE) { $bad += 'replays' }
"quick: $q"
$tc = & powershell -NoProfile -ExecutionPolicy Bypass -File "$r\tools\tools_check.ps1" -Game $env:PATH_FS2 | Select-Object -Last 1
"tools: $tc"; if ($tc -notmatch 'PASS') { $bad += 'tools' }
$aw = & powershell -NoProfile -ExecutionPolicy Bypass -File "$sp\awards_check.ps1" -Configs r,d | Select-Object -Last 1
"awards: $aw"; if ($aw -notmatch 'PASS') { $bad += 'awards' }
if ($bad) { "rp_gate: FAIL ($($bad -join ', '))"; exit 1 }
'rp_gate: PASS'
