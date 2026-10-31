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
		Remove-Item "$env:PATH_FS2\FS2vs2022$(if ($c -eq 'Release') {'r'} else {'d'})64.exe" -ErrorAction SilentlyContinue
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
}
'x64 PASS'
