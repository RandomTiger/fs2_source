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

# fs1demo rule (steward D-20261026-203028): no recording and no trace baseline, so a run passes when exit is 0, autotest.log says
# PASS, and it has Asserts 0, Int3 0, warnings 0 and no Assert/Int3/Error/Warning line. Applied to the native and the Godot/DLL run.
$Fs1DemoRule = 'fs1demo rule D-20261026-203028: exit 0 + autotest.log PASS + 0 Assert/Int3/Error/warnings, no TraceDiff'
function Autotest-Result([string]$logSrc, [string]$dest) {
	if (-not (Test-Path $logSrc)) { return 'AUTOTEST NO LOG' }
	Copy-Item $logSrc $dest -Force
	$c = (Select-String $dest -Pattern 'Asserts (\d+), Int3 (\d+), warnings (\d+)' | Select-Object -Last 1)
	$bad = @(Select-String $dest -Pattern '^\[[ \d.]+\] (Warning|Error|Assert|Int3)')
	if (-not (Select-String $dest -Pattern '^\[[ \d.]+\] PASS' -Quiet)) { return 'AUTOTEST NOT PASS' }
	if (-not $c -or $c.Matches[0].Groups[1].Value -ne '0' -or $c.Matches[0].Groups[2].Value -ne '0' -or $c.Matches[0].Groups[3].Value -ne '0') { return 'AUTOTEST PASS but counts not 0' }
	if ($bad.Count) { return "AUTOTEST PASS but $($bad[0].Line.Trim())" }
	return 'AUTOTEST PASS'
}

# Restart check (D-055) and forced-Error check (P1, steward D-20261022-143927), shared by dll_check and godot_check.
# Two missions of the set run one after the other in ONE host process ($Launch gets both command lines).
#   restart  both runs must pass: exit 0, REPLAY OK, trace = the recorded one (fs1demo: exit 0 + autotest.log clean-run rule)
#   fatal    an Error() is forced in the first run (FS2_FORCE_ERROR_FRAME); the DLL hands it back (FS2_FATAL_RETURN=1), the host
#            process stays up and the second run must pass as above. The first run is not scored except that it must report the fatal.
# $Launch: scriptblock(cmd1, cmd2, outPrefix) -> exit code of the host process; it writes <outPrefix>.out.txt / .err.txt.
# Returns one summary line starting with PASS or FAIL.
function Test-HostRestart($gs, [string]$Run, [string]$Out, [string]$TraceDiff, [scriptblock]$Launch, [string]$Kind, [string]$Host2) {
    $recs = @($gs.Missions)
    if ($recs -contains 'sm1-01' -and $recs -contains 'sm3-06') { $a = 'sm1-01'; $b = 'sm3-06' } else { $a = $recs[0]; $b = $recs[1] }
    if (-not $b) { return "FAIL $Kind check ($($gs.Name)): needs two missions" }
    $t1 = Join-Path $Out "${Kind}_1.trace"; $t2 = Join-Path $Out "${Kind}_2.trace"
    $ra = if ($gs.RecDir) { Join-Path $gs.RecDir "$a.fs2rec" } else { $null }
    $rb = if ($gs.RecDir) { Join-Path $gs.RecDir "$b.fs2rec" } else { $null }
    Remove-Item $t1, $t2, "$ra.replay.log", "$rb.replay.log", (Join-Path $Run 'autotest.log') -ErrorAction SilentlyContinue
    $c1 = (& $gs.Flags $a '%T') -replace '%T', $t1
    $c2 = (& $gs.Flags $b '%T') -replace '%T', $t2
    # a fatal Error() mid-mission leaves the game not bit-identical for a replay (see godot_questions.md), so the run after it plays live
    $live = ($Kind -eq 'fatal') -or (-not $gs.RecDir)
    if ($live -and $gs.RecDir) { $c2 = "-window -autotest -headless -at_mission $b -at_time 30" }
    if ($Kind -eq 'fatal') { $env:FS2_FATAL_RETURN = '1'; $env:FS2_FORCE_ERROR_FRAME = '150' }
    $code = & $Launch $c1 $c2 (Join-Path $Out "${Host2}_$Kind")
    Remove-Item Env:FS2_FATAL_RETURN, Env:FS2_FORCE_ERROR_FRAME -ErrorAction SilentlyContinue
    $line = "$Kind check ($($gs.Name): $a then $b, one process) $Host2 exit=$code"
    $ok = ($code -eq 0)
    $txt = ''
    foreach ($f in "${Host2}_$Kind.out.txt", "${Host2}_$Kind.err.txt") { $p = Join-Path $Out $f; if (Test-Path $p) { $txt += (Get-Content -Raw $p) } }
    if ($Kind -eq 'fatal') {
        if ($txt -match 'fatal error') { $line += ' | fatal handed back, process alive' } else { $ok = $false; $line += ' | NO FATAL SEEN' }
    }
    if (-not $live) {
        if ($Kind -eq 'restart') {
            $v = (& $TraceDiff ([IO.Path]::ChangeExtension($ra, '.trace')) $t1 | Select-String '^(MATCH|DIVERGED)' | Select-Object -First 1).Line
            $line += " | first: $v"; if ($v -notmatch '^MATCH') { $ok = $false }
        }
        $v = (& $TraceDiff ([IO.Path]::ChangeExtension($rb, '.trace')) $t2 | Select-String '^(MATCH|DIVERGED)' | Select-Object -First 1).Line
        $line += " | second: $v"; if ($v -notmatch '^MATCH') { $ok = $false }
        $l = if (Test-Path "$rb.replay.log") { (Select-String "$rb.replay.log" -Pattern 'REPLAY OK|DESYNC.*|ERROR.*' | Select-Object -First 1).Line } else { 'NO LOG' }
        $line += " | $l"; if ($l -notmatch '^REPLAY OK') { $ok = $false }
        Remove-Item "$ra.replay.log", "$rb.replay.log" -ErrorAction SilentlyContinue
    } else {
        $r = Autotest-Result (Join-Path $Run 'autotest.log') (Join-Path $Out "${Host2}_$Kind.autotest.log")
        $line += " | second: $r [$Fs1DemoRule]"; if ($r -ne 'AUTOTEST PASS') { $ok = $false }
    }
    return $(if ($ok) { "PASS $line" } else { "FAIL $line" })
}
