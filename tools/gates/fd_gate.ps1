# FS1Demo merge-down gate (D-20261020-211001 item 1 + D-20261022-150213 d): Release and Debug Rebuild (links into
# PATH_FS2), exes copied into the FS1 demo folder, the four demo missions r/d (fs1demo_run.ps1), then the menu tour on
# the FS1 demo set r/d (menu_tour_all.ps1 -Sets fs1demo -NoBuild). Last line PASS or FAIL.
. "$PSScriptRoot\common.ps1"
Require-Env PATH_FS2, PATH_FS1
Initialize-Fs1DemoScratch	# FS1DEMO_PATH = the scratch copy of the FS1 demo, never the install
$Repo = $RepoRoot; $dd = $env:FS1DEMO_PATH; $fs2 = $env:PATH_FS2
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
$out = Join-Path $env:TEMP 'fd_gate'; New-Item -ItemType Directory -Force $out | Out-Null
$bad = @()
if (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { 'FAIL the game is running'; exit 1 }
"repo at $(git -C $Repo log --oneline -1)"
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
foreach ($d in $fs2, $dd) { Remove-Item "$d\FS2vs2022r.exe", "$d\FS2vs2022d.exe" -ErrorAction SilentlyContinue }
foreach ($c in 'Release', 'Debug') {
	Get-ChildItem $Repo -Recurse -Filter code.lib | Remove-Item
	& $msb "$Repo\Freespace2.vcxproj" /t:Rebuild /nodeReuse:false "/p:Configuration=$c" /p:Platform=Win32 "/p:SolutionDir=$Repo\" /m /v:q /nologo > "$out\build_$c.txt" 2>&1
	"build $c exit $LASTEXITCODE"; if ($LASTEXITCODE) { $bad += "build $c" }
}
foreach ($e in 'r', 'd') { if (Test-Path "$fs2\FS2vs2022$e.exe") { Copy-Item "$fs2\FS2vs2022$e.exe" $dd -Force } else { $bad += "exe $e" } }
if (-not $bad) {
	$m = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\fs1demo_run.ps1" -Configs r,d -GameDir $dd -Out "$out\missions"
	$m
	if (($m | Select-Object -Last 1) -ne 'PASS') { $bad += 'demo missions' }
	$t = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\menu_tour_all.ps1" -Sets fs1demo -Configs r,d -NoBuild -Fs1DemoDir $dd -Out "$out\tour"
	$t | Where-Object { $_ -match "(FAIL|SKIP)" -or $_ -match ' exit=' -or $_ -match '^DONE' }
	$sf = @($t | Where-Object { $_ -match "Screen '[^']+': FAIL" }).Count
	$ends = @($t | Where-Object { $_ -match '^fs1demo (r|d) exit=0 Menu tour: \d+ screens, \d+ passed, 0 failed, \d+ skipped' }).Count
	"tour: screen FAIL lines $sf, builds finished clean $ends of 2"
	if ($sf -or $ends -ne 2 -or (($t | Select-Object -Last 1) -ne 'DONE pass=2 fail=0')) { $bad += 'fs1demo tour' }
}
if ($bad) { "FAIL $($bad -join ', ')" } else { 'PASS' }
