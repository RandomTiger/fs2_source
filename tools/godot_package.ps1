<#
Packaged Godot port (batch P, D-20261028-160512): builds the template_release wrapper, exports the project with the stock
4.7.2-stable Windows release export template (unmodified, installed per user, never in git) and leaves
godot\build\pkg\ holding FS2Godot.exe + FS2Godot.pck + fs2godot_rel.dll. -Stage <dir> also copies the package and the game DLL
(fs2game64r.dll) into a data folder, which is the whole install: the exe then runs from there with no arguments.

Usage: tools\godot_package.ps1 [-Godot exe] [-GodotCpp dir] [-Stage dir] [-GameDir dir] [-NoBuild]
Locations come from the machine profile: -Godot / FS2_GODOT (the 4.7.2 editor exe, used only to export), -GodotCpp / FS2_GODOT_CPP
(godot-cpp checkout, pinned in godot\README.md), -GameDir / PATH_FS2 (folder holding fs2game64r.dll, for -Stage).
The export template must be in %APPDATA%\Godot\export_templates\4.7.2.stable (official release, SHA512-checked, not rebuilt).
#>
param(
	[string]$Godot = $env:FS2_GODOT,
	[string]$GodotCpp = $env:FS2_GODOT_CPP,
	[string]$Stage,
	[string]$GameDir = $env:PATH_FS2,
	[switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$proj = Join-Path $repo 'godot'
$pkg = Join-Path $proj 'build\pkg'
if (-not $Godot -or -not (Test-Path $Godot)) { throw 'Godot 4.7.2 editor exe not found: pass -Godot or set FS2_GODOT (machine profile)' }
if (-not $NoBuild -and (-not $GodotCpp -or -not (Test-Path $GodotCpp))) { throw 'godot-cpp not found: pass -GodotCpp or set FS2_GODOT_CPP (machine profile)' }
$tpl = Join-Path $env:APPDATA 'Godot\export_templates\4.7.2.stable\windows_release_x86_64.exe'
if (-not (Test-Path $tpl)) { throw "Export template missing: $tpl (official 4.7.2-stable templates, installed unmodified)" }

if (-not $NoBuild) {
	$vsw = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	$vs = & $vsw -latest -products * -property installationPath
	$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
	$b = Join-Path $proj 'build\release'
	& $cmake -S $proj -B $b -G 'Visual Studio 17 2022' -A x64 "-DGODOT_CPP_DIR=$GodotCpp" -DGODOTCPP_TARGET=template_release
	if ($LASTEXITCODE) { throw 'cmake configure failed' }
	& $cmake --build $b --config Release --parallel
	if ($LASTEXITCODE) { throw 'wrapper build failed' }
}
$rel = Join-Path $proj 'bin\fs2godot_rel.dll'
if (-not (Test-Path $rel)) { throw "Missing $rel (wrapper not built)" }

if (-not (Test-Path (Join-Path $proj '.godot\extension_list.cfg'))) { & $Godot --headless --path $proj --import | Out-Null }
if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
New-Item -ItemType Directory $pkg | Out-Null
& $Godot --headless --path $proj --export-release 'Windows Desktop' (Join-Path $pkg 'FS2Godot.exe')
if ($LASTEXITCODE) { throw "export failed ($LASTEXITCODE)" }
foreach ($f in 'FS2Godot.exe', 'FS2Godot.pck') { if (-not (Test-Path (Join-Path $pkg $f))) { throw "export produced no $f" } }
# the exporter puts the GDExtension library beside the exe; make sure of it, whatever the layout
if (-not (Test-Path (Join-Path $pkg 'fs2godot_rel.dll'))) { Copy-Item $rel $pkg -Force }

if ($Stage) {
	if (-not $GameDir) { throw '-Stage needs -GameDir or PATH_FS2 (folder holding fs2game64r.dll)' }
	New-Item -ItemType Directory $Stage -Force | Out-Null
	foreach ($f in 'FS2Godot.exe', 'FS2Godot.pck', 'fs2godot_rel.dll') { Copy-Item (Join-Path $pkg $f) $Stage -Force }
	if ((Resolve-Path $GameDir).Path -ne (Resolve-Path $Stage).Path) { Copy-Item (Join-Path $GameDir 'FS2game64r.dll') $Stage -Force }
}
Write-Host "Package: $pkg"
