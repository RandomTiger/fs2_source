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
