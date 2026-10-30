# FS1Compat per-commit gate (D-20261022-152103 a+b; Tom's shape, D-20261020-211001): Release and Debug Rebuild
# (exes copied into the FS1 install when the project links into PATH_FS2), every recording in recordings\fs1
# replayed identical in Release and Debug, tools_check, and the FS1 retail menu tour r/d to its end
# (tools\menu_check.ps1 -GameDir on the FS1 install: exit 0, 0 FAIL, SKIP lines quoted).
# No FS2 suite and no demo build: FS2 is tested at Enhanced. No known-failure exceptions. Last line PASS or FAIL.
# Run one at a time; build or run nothing else meanwhile.
param(
	[string]$Repo = '',
	[string]$Label = 'fc_gate',
	[string]$Fs1Dir = '',
	[switch]$NoTour
)
. "$PSScriptRoot\common.ps1"
Require-Env PATH_FS2, PATH_FS1	# FS2_PROCDUMP (optional) = procdump.exe, for crash dumps
if (-not $Repo) { $Repo = $RepoRoot }
if (-not $Fs1Dir) { $Fs1Dir = $env:PATH_FS1 }
$fs2 = $env:PATH_FS2
$env:PATH_FS1 = $Fs1Dir
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
$out = Join-Path $env:TEMP $Label
New-Item -ItemType Directory -Force $out | Out-Null
$bad = @()
if (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { 'FAIL the game is running'; exit 1 }
"repo $Repo at $(git -C $Repo log --oneline -1)"
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msb "$Repo\tools\TraceDiff\TraceDiff.vcxproj" /p:Configuration=Release /p:Platform=Win32 "/p:SolutionDir=$Repo\" /m /v:q /nologo > "$out\td.txt" 2>&1
"TraceDiff exit $LASTEXITCODE"; if ($LASTEXITCODE) { $bad += 'TraceDiff' }
# every worktree links into the same game folders: delete first so no other branch's exe survives a no-op link.
# Since the Enhanced merge-down (24cd57f) the project links into PATH_FS2 (one exe for every data set); an older
# FS1Compat linked into PATH_FS1. Either way the exes are taken from wherever the build put them.
foreach ($d in $fs2, $Fs1Dir) { Remove-Item "$d\FS2vs2022r.exe", "$d\FS2vs2022d.exe" -ErrorAction SilentlyContinue }
foreach ($c in 'Release', 'Debug') {
	Get-ChildItem $Repo -Recurse -Filter code.lib | Remove-Item
	& $msb "$Repo\Freespace2.vcxproj" /t:Rebuild /nodeReuse:false "/p:Configuration=$c" /p:Platform=Win32 "/p:SolutionDir=$Repo\" /m /v:q /nologo > "$out\build_$c.txt" 2>&1
	"build $c exit $LASTEXITCODE"; if ($LASTEXITCODE) { $bad += "build $c" }
}
foreach ($e in 'r', 'd') {
	if (-not (Test-Path "$Fs1Dir\FS2vs2022$e.exe") -and (Test-Path "$fs2\FS2vs2022$e.exe")) { Copy-Item "$fs2\FS2vs2022$e.exe" $Fs1Dir }
	if (-not (Test-Path "$Fs1Dir\FS2vs2022$e.exe")) { "FAIL no FS2vs2022$e.exe in $Fs1Dir"; $bad += "exe $e" } }
if (-not $bad) {
	$f = @(Get-ChildItem "$Repo\recordings\fs1" -Filter *.fs2rec | ForEach-Object { $_.BaseName })
	$r = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\replay_check.ps1" -Configs r,d -CrossCheck ($f -join ',') -Missions ($f -join ',') -GameDir $fs2 -Fs1Dir $Fs1Dir -Out "$out\fs1"
	# -Missions also picks recordings\autotest\sm1-01 (same name), replayed in the FS2 folder with whatever exe is there:
	# not this branch's build and not FS1 data, so only the (fs1) lines count
	$r = @($r | Where-Object { $_ -match '\(fs1\)' })
	$r | Where-Object { $_ -match '^(FAIL|SKIP)' } | ForEach-Object { $_.Substring(0, [Math]::Min(200, $_.Length)) }
	$n = @($r | Where-Object { $_ -match '^\S+ \(fs1\) \|.*\| builds identical$' }).Count
	"fs1 replays: $n of $($f.Count) identical r/d, $(@($r | Where-Object { $_ -match '^FAIL ' }).Count) FAIL"
	if (@($r | Where-Object { $_ -match '^FAIL ' }).Count -or ($r -match '^SKIP') -or $n -ne $f.Count) { $bad += 'fs1 replays' }
	$t = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\tools_check.ps1" -Game $fs2 -RepoDir $Repo
	$t | Where-Object { $_ -match '^(FAIL|STALE)' } | ForEach-Object { $_.Substring(0, [Math]::Min(200, $_.Length)) }
	"tools: $($t | Select-Object -Last 1)"; if (($t | Select-Object -Last 1) -notmatch '^PASS') { $bad += 'tools' }
	if (-not $NoTour) {
		$m = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\menu_check.ps1" -Out "$out\tour" -Configs r,d -GameDir $Fs1Dir
		$m | Where-Object { $_ -match "Screen '[^']+': (FAIL|SKIP)" } | ForEach-Object { "tour $_" }
		$m | Where-Object { $_ -match '^(FAIL )?(r|d) exit=' } | ForEach-Object { "tour $_" }
		$sf = @($m | Where-Object { $_ -match "Screen '[^']+': FAIL" }).Count
		$ends = @($m | Where-Object { $_ -match '^(r|d) exit=0 Menu tour: \d+ screens, \d+ passed, 0 failed, \d+ skipped' }).Count
		"tour: $($m | Select-Object -Last 1), screen FAIL lines $sf, builds finished clean $ends of 2"
		if ($sf -or $ends -ne 2 -or (($m | Select-Object -Last 1) -ne 'DONE pass=2 fail=0')) { $bad += 'fs1 tour' }
	} else { $bad += 'tour not run' }
}
if ($bad) { "FAIL $($bad -join ', ')" } else { 'PASS' }
