# Shared by dll_check.ps1 and godot_check.ps1 (dot-sourced): the recording sets of the Godot gate (D-20261022-143925)
# and where each one runs. Folders and flags follow tools\replay_check.ps1 (retail, fs1, demo) and tools\fs1demo_run.ps1.
#
#   autotest  recordings\autotest, the FS2 retail install ($env:PATH_FS2)
#   fs1       recordings\fs1, the FS1 retail install ($env:PATH_FS1, default C:\GOG Games\Freespace)
#   demo      recordings\demo, the FS2 demo test folder ($env:FS2DEMO_TEST, default C:\Games\FS2D_scratch), whose x64 exes and
#             DLLs are the FS2_DEMO build (tools\build_demo64.ps1)
#   fs1demo   no recordings: the four FS1 demo missions run under -autotest in the FS1 demo test folder ($env:FS1DEMO_PATH,
#             default C:\Games\FS1D_scratch); the only baseline is the native x64 trace of the same run
#
# Get-GateSet returns Dir, RecDir (null for fs1demo), Missions, Autotest-copy flag and a Flags scriptblock.
function Get-GateSet([string]$Set, [string]$Repo, [string]$Retail) {
	switch ($Set) {
		'autotest' { $dir = $Retail }
		'fs1' { $dir = if ($env:PATH_FS1) { $env:PATH_FS1 } else { 'C:\GOG Games\Freespace' } }
		'demo' { $dir = if ($env:FS2DEMO_TEST) { $env:FS2DEMO_TEST } else { 'C:\Games\FS2D_scratch' } }
		'fs1demo' { $dir = if ($env:FS1DEMO_PATH) { $env:FS1DEMO_PATH } else { 'C:\Games\FS1D_scratch' } }
		default { throw "Unknown -Set '$Set' (autotest, fs1, demo, fs1demo)" }
	}
	$s = @{ Name = $Set; Dir = $dir; CopyMissions = ($Set -eq 'autotest'); RecDir = $null; Missions = @() }
	if ($Set -eq 'fs1demo') {
		$s.Missions = @('btmdemo', 'demo01', 'demo02a', 'demo02b')
		$s.Flags = { param($m, $trace) "-window -autotest -headless -at_mission $m -at_pilot Tom -at_time 60 -trace `"$trace`"" }
	} else {
		$s.RecDir = Join-Path $Repo "recordings\$Set"
		$s.Missions = @(Get-ChildItem $s.RecDir -Filter *.fs2rec -ErrorAction SilentlyContinue | ForEach-Object { $_.BaseName })
		$rd = $s.RecDir
		$s.Flags = { param($m, $trace) "-window -autotest -headless -replay `"$rd\$m.fs2rec`" -trace `"$trace`"" }.GetNewClosure()
	}
	return $s
}

# Other sets run in a folder that is not the retail one: put this build's x64 exes and DLLs there (the demo set has its own
# FS2_DEMO build, built there by build_demo64.ps1, so nothing is copied for it).
function Install-GateBuild($s, [string]$Retail, [string]$Config) {
	if (-not (Test-Path $s.Dir)) { throw "No folder $($s.Dir) for set $($s.Name)" }
	if (($s.Name -eq 'fs1') -or ($s.Name -eq 'fs1demo')) {
		foreach ($f in "FS2vs2022${Config}64.exe", "FS2game64$Config.dll") { Copy-Item (Join-Path $Retail $f) $s.Dir -Force }
	}
}
