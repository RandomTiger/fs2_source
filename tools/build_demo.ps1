<#
Builds the FS2_DEMO exe (docs/FS2_DEMO.md) in Release and Debug from clean, into the demo test folder.

  tools\build_demo.ps1 [-GameDir <dir>] [-Configs Release,Debug]

-GameDir defaults to $env:FS2DEMO_TEST, the scratch folder demo tests run in (a copy of the demo data, never the
install itself). The exes keep the retail names, FS2vs2022r.exe and FS2vs2022d.exe, so they must not go to the
retail game folder. Objects go to their own folder per worktree in %LOCALAPPDATA%, and code.lib and the .tlog lists
are cleared before and after, so a retail build in the same checkout and this one never share objects (a stale
FS2_DEMO object desyncs retail replays). The last line is PASS, or FAIL with the reason.
#>
param(
	[string]$GameDir = $env:FS2DEMO_TEST,
	[string[]]$Configs = @('Release', 'Debug')
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir -or -not (Test-Path $GameDir)) { 'FAIL no demo test folder (-GameDir or FS2DEMO_TEST)'; exit 1 }
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
	$exe = Join-Path $GameDir ("FS2vs2022{0}.exe" -f $(if ($c -eq 'Release') { 'r' } else { 'd' }))
	$int = Join-Path $env:LOCALAPPDATA ("fs2_demo_obj\{0}_{1}\" -f (Split-Path $repo -Leaf), $c)
	Clear-Shared
	Remove-Item $exe -ErrorAction SilentlyContinue
	& $msb (Join-Path $repo 'Freespace2.vcxproj') /t:Rebuild /nodeReuse:false "/p:Configuration=$c" /p:Platform=Win32 "/p:SolutionDir=$repo\" `
		"/p:ForceImportBeforeCppTargets=$PSScriptRoot\fs2demo.props" "/p:IntDir=$int" /m /v:q /nologo > (Join-Path $logs "$c.txt") 2>&1
	$code = $LASTEXITCODE
	$demo = (Test-Path $exe) -and (Select-String $exe -Pattern 'Freespace 2 Demo' -SimpleMatch -Quiet)
	"$c exit $code, demo exe: $demo"
	if ($code -or -not $demo) { $failed += $c }
}
Clear-Shared
if ($failed) { "FAIL $($failed -join ', ') (logs in $logs)"; exit 1 }
'PASS'
