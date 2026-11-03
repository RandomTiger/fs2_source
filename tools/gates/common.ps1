# Shared by the gate scripts in this folder. No machine path lives in a gate script (D-20261027-185212):
# checkouts are found from this script's location, installs from the PATH_* environment variables,
# and a missing variable is a stated FAIL, never a C:\ default.
$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
. "$PSScriptRoot\..\layer.ps1"	# exe names: Get-FS2Exe r|d [-Repo] (D-20261101-064106)
function Require-Env([string[]]$Names) {
	foreach ($n in $Names) {
		if (-not [Environment]::GetEnvironmentVariable($n)) { "FAIL environment variable $n is not set (see the machine profile)"; exit 1 }
	}
}
function Get-MSBuild {
	& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
}
# The demo gates build and run in the dev copies PATH_FS2DEMO / PATH_FS1DEMO, never in the clean installs
# (D-20261019-213450). A dev copy is refreshed from its clean install by hand (tools\make_demo_test.ps1 for the FS2
# demo). A missing variable or folder is a stated FAIL.
function Initialize-Fs2DemoScratch {
	Require-Env PATH_FS2DEMO
	if (-not (Test-Path $env:PATH_FS2DEMO)) { "FAIL the FS2 demo dev copy PATH_FS2DEMO = $env:PATH_FS2DEMO does not exist (refresh it with tools\make_demo_test.ps1)"; exit 1 }
}
function Initialize-Fs1DemoScratch {
	Require-Env PATH_FS1DEMO
	if (-not (Test-Path $env:PATH_FS1DEMO)) { "FAIL the FS1 demo dev copy PATH_FS1DEMO = $env:PATH_FS1DEMO does not exist (see the header of tools\fs1demo_run.ps1)"; exit 1 }
}
