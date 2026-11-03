<#
Builds the FS2_DEMO x64 game exe and fs2game.dll (Release and Debug) into the demo test folder, as build_demo.ps1 does for x86
(docs/FS2_DEMO.md): the demo recordings replay there through dll_check.ps1 / godot_check.ps1 -Set demo.

  tools\build_demo64.ps1 [-GameDir <dir>] [-Configs Release,Debug]

-GameDir defaults to $env:PATH_FS2DEMO, the demo dev copy (tools\make_demo_test.ps1 refreshes it). Names:
FS2_<Layer>_r64.exe / d64 and FS2game_<Layer>_64r.dll / d (tools\layer.ps1). Last line PASS, or FAIL with the reason.
#>
param(
	[string]$GameDir = $env:PATH_FS2DEMO,
	[string[]]$Configs = @('Release', 'Debug')
)
$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"	# exe and DLL names (D-20261101-064106)
if (-not $GameDir -or -not (Test-Path $GameDir)) { "FAIL no demo dev copy '$GameDir' (-GameDir or PATH_FS2DEMO)"; exit 1 }
$Configs = @($Configs | ForEach-Object { $_ -split ',' })
$env:PATH_FS2 = $GameDir
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
$logs = Join-Path $env:LOCALAPPDATA 'fs2_demo_build'
New-Item -ItemType Directory -Force $logs | Out-Null
function Clear-Shared {
	Get-ChildItem $repo -Recurse -Filter code.lib -ErrorAction SilentlyContinue | Remove-Item
	Get-ChildItem $repo -Recurse -Directory -Filter *.tlog -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
}
$failed = @()
foreach ($c in $Configs) {
	$k = if ($c -eq 'Release') { 'r' } else { 'd' }
	foreach ($proj in @(@('Freespace2.vcxproj', (Get-FS2Exe "${k}64"), 'exe'), @('fs2game\fs2game.vcxproj', (Get-FS2Dll $k), 'dll'))) {
		$out = Join-Path $GameDir $proj[1]
		$int = Join-Path $env:LOCALAPPDATA ("fs2_demo_obj64\{0}_{1}_{2}\" -f (Split-Path $repo -Leaf), $c, $proj[2])
		Clear-Shared
		Remove-Item $out -ErrorAction SilentlyContinue
		& $msb (Join-Path $repo $proj[0]) /t:Rebuild /nodeReuse:false "/p:Configuration=$c" /p:Platform=x64 "/p:SolutionDir=$repo\" `
			"/p:ForceImportBeforeCppTargets=$PSScriptRoot\fs2demo.props" "/p:IntDir=$int" /m /v:q /nologo > (Join-Path $logs "x64_${c}_$($proj[2]).txt") 2>&1
		$code = $LASTEXITCODE
		$demo = (Test-Path $out) -and (Select-String $out -Pattern 'Freespace 2 Demo' -SimpleMatch -Quiet)
		"$c $($proj[1]) exit $code, demo build: $demo"
		if ($code -or -not $demo) { $failed += "$c $($proj[1])" }
	}
}
Clear-Shared
if ($failed) { "FAIL $($failed -join ', ') (logs in $logs)"; exit 1 }
'PASS'
