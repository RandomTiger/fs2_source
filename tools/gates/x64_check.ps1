# x64 build and headless replay of every recording, Release and Debug, for the x64 layers.
param([string[]]$Branches = @('PrePort64'))
$Branches = @($Branches | ForEach-Object { $_ -split ',' })
. "$PSScriptRoot\common.ps1"
$gh = Split-Path $RepoRoot -Parent
Require-Env PATH_FS2
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
$msb = Get-MSBuild
# The checkout of branch $b on this machine (#102): this checkout if it is on $b, else a worktree of this clone on $b,
# else a folder fs2_<b> or fs2_source_<b> beside it. None found is a FAIL, never a skip.
function Find-BranchRepo([string]$b) {
	if ((git -C $RepoRoot rev-parse --abbrev-ref HEAD) -eq $b) { return $RepoRoot }
	$wt = $null
	foreach ($l in (git -C $RepoRoot worktree list --porcelain)) {
		if ($l -like 'worktree *') { $wt = $l.Substring(9) }
		elseif ($l -eq "branch refs/heads/$b") { return (Resolve-Path $wt).Path }
	}
	foreach ($n in "fs2_$b", "fs2_source_$b") {
		$p = Join-Path $gh $n
		if ((Test-Path "$p\Freespace2.vcxproj") -and ((git -C $p rev-parse --abbrev-ref HEAD) -eq $b)) { return $p }
	}
	return $null
}
foreach ($b in $Branches) {
	$repo = Find-BranchRepo $b
	if (-not $repo) { "FAIL no checkout of $b found (this checkout, its worktrees, or $gh\fs2_$b)"; exit 1 }
	"$b checkout: $repo"
	foreach ($c in 'Release', 'Debug') {
		Remove-Item "$env:PATH_FS2\$(Get-FS2Exe "$(ConvertTo-FS2Config $c)64" $repo)" -ErrorAction SilentlyContinue
		$f = "$env:TEMP\x64_$b`_$c.txt"
		& $msb "$repo\Freespace2.vcxproj" /p:Configuration=$c /p:Platform=x64 "/p:SolutionDir=$repo\" /v:q /nologo /m 2>&1 | Out-File $f
		if (Select-String $f -Pattern ': (fatal )?error ') { "$b x64 $c BUILD FAILED"; exit 1 }
	}
	$o = "$env:TEMP\x64_$b"
	foreach ($e in 'r64', 'd64') {
		$l = & powershell -NoProfile -ExecutionPolicy Bypass -File "$repo\tools\nodraw_check.ps1" -Headless -Config $e -Out "$o\headless_$e" | Select-Object -Last 1
		"$b x64 $e`: $l"
		if ($l -notmatch 'fail=0') { "$b x64 FAIL"; exit 1 }
	}
	# FS1 and the FS1 demo headless too (D-20261021-125812, #102): this build's x64 exes go into each install, then
	# recordings\fs1 replays (FS1 at GR_640, #101) and the FS1 demo missions run. A missing install is a FAIL, never a skip.
	foreach ($v in 'PATH_FS1', 'PATH_FS1DEMO') {
		$d = [Environment]::GetEnvironmentVariable($v)
		if (-not $d -or -not (Test-Path $d)) { "FAIL set $v to its dev copy"; exit 1 }
		foreach ($e in 'r64', 'd64') { Copy-Item "$env:PATH_FS2\$(Get-FS2Exe $e $repo)" $d -Force }
	}
	$fs1 = (Get-ChildItem "$repo\recordings\fs1\*.fs2rec" | ForEach-Object BaseName) -join ','
	# -CrossCheck none: the default sm1-01 is also an FS1 name and would run the x86 exes
	$l = & powershell -NoProfile -ExecutionPolicy Bypass -File "$repo\tools\replay_check.ps1" -Headless -Configs r64,d64 -CrossCheck none -Missions $fs1 -Out "$o\fs1" | Select-Object -Last 1
	"$b x64 fs1: $l"
	if ("$l" -notmatch 'fail=0') { "$b x64 FAIL"; exit 1 }
	$l = & powershell -NoProfile -ExecutionPolicy Bypass -File "$repo\tools\fs1demo_run.ps1" -Headless -Configs r64,d64 -Out "$o\fs1demo" | Select-Object -Last 1
	"$b x64 fs1demo: $l"
	if ($l -ne 'PASS') { "$b x64 FAIL"; exit 1 }
}
'x64 PASS'
