# Shared by the gate scripts in this folder. No machine path lives in a gate script (D-20261027-185212):
# checkouts are found from this script's location, installs from the PATH_* environment variables,
# and a missing variable is a stated FAIL, never a C:\ default.
$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
function Require-Env([string[]]$Names) {
	foreach ($n in $Names) {
		if (-not [Environment]::GetEnvironmentVariable($n)) { "FAIL environment variable $n is not set (see the machine profile)"; exit 1 }
	}
}
function Get-MSBuild {
	& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
}
# The demo gates build and run in scratch copies, never in the demo installs (D-20261019-213450).
# FS2DEMO_TEST / FS1DEMO_PATH name them; unset, they default to <PATH_FS2DEMO>_scratch / <PATH_FS1DEMO>_scratch.
function Initialize-Fs2DemoScratch {
	if (-not $env:FS2DEMO_TEST) { Require-Env PATH_FS2DEMO; $env:FS2DEMO_TEST = $env:PATH_FS2DEMO.TrimEnd('\') + '_scratch' }
	if (Test-Path $env:FS2DEMO_TEST) { return }
	$mk = "$RepoRoot\tools\make_demo_test.ps1"
	if (-not (Test-Path $mk)) { "FAIL no demo scratch folder $env:FS2DEMO_TEST and no tools\make_demo_test.ps1 on this branch to make it"; exit 1 }
	"creating FS2 demo scratch $env:FS2DEMO_TEST from $env:PATH_FS2DEMO"
	& powershell -NoProfile -ExecutionPolicy Bypass -File $mk -From $env:PATH_FS2DEMO -To $env:FS2DEMO_TEST | Out-Null
	if (-not (Test-Path $env:FS2DEMO_TEST)) { 'FAIL the FS2 demo scratch folder was not created'; exit 1 }
}
# The FS1 demo scratch cannot be made by a script: it holds fsdemo10.exe, the game extracts the demo on its first
# start, and a pilot is made by hand (tools\fs1demo_run.ps1). A missing folder is a stated FAIL.
function Initialize-Fs1DemoScratch {
	if (-not $env:FS1DEMO_PATH) { Require-Env PATH_FS1DEMO; $env:FS1DEMO_PATH = $env:PATH_FS1DEMO.TrimEnd('\') + '_scratch' }
	if (-not (Test-Path $env:FS1DEMO_PATH)) { "FAIL no FS1 demo scratch folder $env:FS1DEMO_PATH (set FS1DEMO_PATH, or set it up as in docs/FS1_DEMO.md)"; exit 1 }
}
