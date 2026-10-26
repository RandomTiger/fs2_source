<#
The merge-in check for FS1Demo (D-20261020-211001, docs/FS1_DEMO.md section 5), not the per-change gate; per-change
gate is fs1demo_run.ps1 -Configs r,d. In one run:

  1. the FS2 retail recordings and recordings\fs1, Release and Debug (replay_check -Configs r,d);
  2. the FS2 demo recordings, from an FS2_DEMO build (build_demo.ps1, replay_check);
  3. the four FS1 demo missions, Release and Debug (fs1demo_run.ps1).

  tools\fs1demo_gate.ps1 [-Fs2Dir <dir>] [-Fs1Dir <dir>] [-DemoDir <dir>] [-Fs1DemoDir <dir>] [-Out <dir>]

The folders default to $env:PATH_FS2 (FS2 retail), $env:PATH_FS1 (FS1 retail), $env:FS2DEMO_TEST (the FS2 demo
test folder) and $env:FS1DEMO_PATH (the FS1 demo folder, see fs1demo_run.ps1). FS1 branches link into PATH_FS1 and
Enhanced into PATH_FS2, so each build here sets both to the folder it is for; the retail exes are copied into the
FS1 retail folder.
The exes already in the FS2, FS1 and FS2 demo folders are saved first and put back at the end (other branches
build into them too). Results: <Out>\retail\summary.txt, <Out>\demo\summary.txt and the mission lines; default
<Out> is %LOCALAPPDATA%\fs1demo_gate. Takes about an hour; build or run nothing else meanwhile.
Known: AutotestAI fails in Debug on its two warnings (#78), as on Enhanced.
#>
param(
	[string]$Fs2Dir = $env:PATH_FS2,
	[string]$Fs1Dir = $env:PATH_FS1,
	[string]$DemoDir = $env:FS2DEMO_TEST,
	[string]$Fs1DemoDir = $env:FS1DEMO_PATH,
	[string]$Out = (Join-Path $env:LOCALAPPDATA 'fs1demo_gate'),
	[switch]$Headless	# every replay and demo run also with -headless (#101)
)

$repo = Split-Path $PSScriptRoot
$hl = if ($Headless) { @('-Headless') } else { @() }
foreach ($d in $Fs2Dir, $Fs1Dir, $DemoDir, $Fs1DemoDir) { if (-not $d -or -not (Test-Path $d)) { "FAIL missing folder '$d' (see the parameters)"; exit 1 } }
if (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { 'FAIL the game is running'; exit 1 }
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
New-Item -ItemType Directory -Force $Out | Out-Null
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null

$bak = Join-Path $Out 'exe_backup'
$saved = @($Fs2Dir, $Fs1Dir, $DemoDir)
foreach ($d in $saved) {
	$b = Join-Path $bak ($d -replace '[:\\ ]', '_')
	New-Item -ItemType Directory -Force $b | Out-Null
	Copy-Item (Join-Path $d 'FS2vs2022r.exe'), (Join-Path $d 'FS2vs2022d.exe') $b -ErrorAction SilentlyContinue
}

function Build([string]$into) {
	$env:PATH_FS1 = $into
	$env:PATH_FS2 = $into	# Enhanced links into PATH_FS2 (D-20261021-151900)
	foreach ($c in 'Release', 'Debug') {
		& $msb (Join-Path $repo 'Freespace2.vcxproj') "/p:Configuration=$c" /p:Platform=Win32 /m /v:m /nologo > (Join-Path $Out "build_$c.log") 2>&1
		if ($LASTEXITCODE) { Write-Host "FAIL $c build into $into (build_$c.log)"; return $false }
	}
	Write-Host "built into $into"
	return $true
}

$ok = $true
try {
	& $msb (Join-Path $repo 'freespace2.sln') /t:TraceDiff /p:Configuration=Release /p:Platform=x86 /m /v:m /nologo > (Join-Path $Out 'build_tracediff.log') 2>&1
	if ($LASTEXITCODE) { 'FAIL TraceDiff build'; exit 1 }

	# 1. retail and FS1 recordings
	if (-not (Build $Fs2Dir)) { exit 1 }
	Copy-Item (Join-Path $Fs2Dir 'FS2vs2022r.exe'), (Join-Path $Fs2Dir 'FS2vs2022d.exe') $Fs1Dir
	$env:FS2DEMO_TEST = $null	# the demo set runs in step 2, from its own build
	& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'replay_check.ps1') -Out (Join-Path $Out 'retail') -Configs r,d -GameDir $Fs2Dir -Fs1Dir $Fs1Dir @hl | Select-Object -Last 1
	Get-Content (Join-Path $Out 'retail\summary.txt') | Where-Object { $_ -match '^FAIL' }

	# 2. FS2 demo recordings
	$env:PATH_FS1 = $DemoDir
	& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build_demo.ps1') -GameDir $DemoDir | Select-Object -Last 1
	$names = (Get-ChildItem (Join-Path $repo 'recordings\demo\*.fs2rec') | ForEach-Object BaseName) -join ','
	& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'replay_check.ps1') -Out (Join-Path $Out 'demo') -Missions $names -GameDir $Fs2Dir -DemoDir $DemoDir @hl | Select-Object -Last 1

	# 3. FS1 demo missions
	if (-not (Build $Fs1DemoDir)) { exit 1 }
	& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'fs1demo_run.ps1') -GameDir $Fs1DemoDir -Out (Join-Path $Out 'fs1demo') @hl
} finally {
	foreach ($d in $saved) { Copy-Item (Join-Path (Join-Path $bak ($d -replace '[:\\ ]', '_')) '*.exe') $d -Force -ErrorAction SilentlyContinue }
	'exes put back'
}
