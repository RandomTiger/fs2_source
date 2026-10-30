param([Parameter(Mandatory)][string]$Repo, [string[]]$Configs = @('Release','Debug'), [switch]$TraceDiff)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
Require-Env PATH_FS2
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
$msb = Get-MSBuild
$logs = Join-Path $env:TEMP 'fs2build'; New-Item -ItemType Directory -Force $logs | Out-Null
Set-Location $Repo
$rc = 0
# PowerShell 5.1 '>' writes UTF-16; grep reads that as no matches, so a zero warning count would be a tool fault
# (D-20261022-095601 item 4). Rewrite each log as UTF-8 without a BOM.
function Save-Utf8($p) { $s = Get-Content -Raw -LiteralPath $p; if ($null -eq $s) { $s = '' }; [IO.File]::WriteAllText($p, $s, (New-Object Text.UTF8Encoding $false)) }
if ($TraceDiff) {
	& $msb tools\TraceDiff\TraceDiff.vcxproj /p:Configuration=Release /p:Platform=Win32 "/p:SolutionDir=$Repo\" /m /v:m /nologo > "$logs\TraceDiff.log" 2>&1
	Save-Utf8 "$logs\TraceDiff.log"
	"TraceDiff exit $LASTEXITCODE"; if ($LASTEXITCODE) { $rc = 1 }
}
foreach ($c in $Configs) {
	# every worktree links into the same game folder, and MSBuild will not relink a worktree whose
	# objects are up to date, so a newer exe from another branch would otherwise stay there
	$exe = Join-Path $env:PATH_FS2 ($(if ($c -eq 'Release') { 'FS2vs2022r.exe' } else { 'FS2vs2022d.exe' }))
	Remove-Item $exe -ErrorAction SilentlyContinue
	& $msb Freespace2.vcxproj "/p:Configuration=$c" /p:Platform=Win32 /m /v:m /nologo > "$logs\$c.log" 2>&1
	$code = $LASTEXITCODE; Save-Utf8 "$logs\$c.log"; $global:LASTEXITCODE = $code
	"$c exit $LASTEXITCODE"
	if ($LASTEXITCODE) { $rc = 1; Select-String -Path "$logs\$c.log" -Pattern ' error ' | Select-Object -First 20 | ForEach-Object { $_.Line } }
}
exit $rc
