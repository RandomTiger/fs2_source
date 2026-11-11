<#
Builds the normal game exe into the FS2 demo test folder, in Release and Debug (and x64 with -Platforms x64).

  tools\build_demo.ps1 [-GameDir <dir>] [-Configs Release,Debug] [-Platforms Win32,x64]

Since #287 there is no separate FS2_DEMO build: the one exe finds the demo data at run time (GlobalIncs\DataSetProfile)
and plays the demo recordings with traces identical to the old demo exe. This script builds that same exe into
PATH_FS2 as usual and copies it (with its pdb) into the demo folder, so the gates that call it keep working unchanged.
-GameDir defaults to $env:PATH_FS2DEMO, the FS2 demo dev copy demo tests run in (never the clean install,
PATH_FS2DEMO_CLEAN). The exes keep the normal names, FS2_<Layer>_r.exe and _d.exe (_r64/_d64 for x64, tools\layer.ps1).
The objects are the retail build's own, so it is quick after a normal build. The last line is PASS, or FAIL with the reason.
#>
param(
	[string]$GameDir = $env:PATH_FS2DEMO,
	[string[]]$Configs = @('Release', 'Debug'),
	[string[]]$Platforms = @('Win32')
)

$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"	# exe names (D-20261101-064106)
if (-not $GameDir -or -not (Test-Path $GameDir)) { 'FAIL no demo test folder (-GameDir or PATH_FS2DEMO)'; exit 1 }
$Configs = @($Configs | ForEach-Object { $_ -split ',' })
$Platforms = @($Platforms | ForEach-Object { $_ -split ',' })
if (-not $env:PATH_FS2) { 'FAIL PATH_FS2 is not set (the exe is built there, then copied)'; exit 1 }
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
$logs = Join-Path $env:LOCALAPPDATA 'fs2_demo_build'
New-Item -ItemType Directory -Force $logs | Out-Null

$failed = @()
foreach ($p in $Platforms) {
	foreach ($c in $Configs) {
		$cfg = ConvertTo-FS2Config $c
		if ($p -eq 'x64') { $cfg += '64' }
		$name = Get-FS2Exe $cfg
		$exe = Join-Path $GameDir $name
		Remove-Item $exe -ErrorAction SilentlyContinue
		# Built into the retail folder as usual, then copied: an exe linked straight into the demo folder is listed in
		# the build's own outputs, and a later retail Rebuild in this checkout deletes it (demo_gate, #287).
		# Paths end in / not \: a quoted argument ending in \" breaks on a path with a space (MSB1008, #281).
		& $msb (Join-Path $repo 'Freespace2.vcxproj') /nodeReuse:false "/p:Configuration=$c" "/p:Platform=$p" "/p:SolutionDir=$repo/" /m /v:q /nologo > (Join-Path $logs "$c$p.txt") 2>&1
		$code = $LASTEXITCODE
		if (-not $code) {
			foreach ($f in $name, [IO.Path]::ChangeExtension($name, '.pdb')) {
				$src = Join-Path $env:PATH_FS2 $f
				if (Test-Path $src) { Copy-Item $src $GameDir -Force }
			}
		}
		"$c $p exit $code, exe: $(Test-Path $exe)"
		if ($code -or -not (Test-Path $exe)) { $failed += "$c $p" }
	}
}
if ($failed) { "FAIL $($failed -join ', ') (logs in $logs)"; exit 1 }
'PASS'
