# The one reader of build\layer.props (D-20261101-064106). Dot-source it: . "<repo>\tools\layer.ps1"
# Every exe and DLL name comes from the branch's layer name:
#   Get-FS2Exe r|d|r64|d64 (or Release/Debug)  -> FS2_<Layer>_r.exe ...
#   Get-FS2Dll r|d                              -> FS2game_<Layer>_64r.dll / _64d.dll
#   Get-FS2Tool Fred2|Pofview|Nebedit r|d       -> Fred2_<Layer>r.exe / Fred2_<Layer>d.exe (no underscore before r/d)
#   $FS2ProcessNames                          -> process-name patterns for "is any game running" checks
#                                                  (every layer's exe, plus the old FS2vs2022* names still in the installs)
$FS2LayerRepo = (Resolve-Path "$PSScriptRoot\..").Path
function Get-FS2Layer([string]$Repo = $FS2LayerRepo) {
	$props = Join-Path $Repo 'build\layer.props'
	if (-not (Test-Path $props)) { throw "no ${props}: this checkout has no layer name" }
	$name = ([xml](Get-Content -Raw $props)).Project.PropertyGroup.FS2Layer
	if (-not $name) { throw "no FS2Layer in $props" }
	$name.Trim()
}
function ConvertTo-FS2Config([string]$Config) {
	switch -regex ($Config) { '^(r|Release)$' { 'r' } '^(d|Debug)$' { 'd' } '^(r64|d64)$' { $Config } default { throw "unknown config '$Config'" } }
}
function Get-FS2Exe([string]$Config = 'r', [string]$Repo = $FS2LayerRepo) { "FS2_$(Get-FS2Layer $Repo)_$(ConvertTo-FS2Config $Config).exe" }
function Get-FS2Dll([string]$Config = 'r', [string]$Repo = $FS2LayerRepo) { "FS2game_$(Get-FS2Layer $Repo)_64$(ConvertTo-FS2Config $Config).dll" }
function Get-FS2Tool([string]$Tool, [string]$Config = 'r', [string]$Repo = $FS2LayerRepo) {
	$c = ConvertTo-FS2Config $Config
	if ($c -notin 'r', 'd') { throw "tools have no '$Config' build" }
	"${Tool}_$(Get-FS2Layer $Repo)$c.exe"
}
$FS2ProcessNames = @('FS2_*', 'FS2vs2022*')
