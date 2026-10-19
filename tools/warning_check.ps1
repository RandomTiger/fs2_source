<#
Builds every project (the game, its tools and code.lib) in Release and Debug from clean intermediates and fails on
any compiler, linker or MSBuild warning (D-20261019-024006: zero warnings from Enhanced up).

  tools\warning_check.ps1 [-RepoDir <checkout>] [-Out <dir>]

The exes go to a scratch folder (PATH_FS2 is pointed there), so the game folder and its exes are not touched. Each
project's build log is kept in <Out> (default %LOCALAPPDATA%\fs2_warning_check). A kept warning is silenced at its site with a
#pragma and a reason; project-wide disables are only the two CRT defines and the linker's /ignore for third-party
libraries (D-20261019-024004). The last line is PASS or FAIL.
About 25 minutes.
#>
param(
	[string]$RepoDir = (Split-Path $PSScriptRoot),
	# not under %TEMP%: MSBuild warns (MSB8029) when an output folder is there
	[string]$Out = (Join-Path $env:LOCALAPPDATA 'fs2_warning_check')
)

$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
$env:PATH_FS2 = Join-Path $Out 'bin'
New-Item -ItemType Directory -Force $Out, $env:PATH_FS2 | Out-Null
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1

$projects = 'Freespace2', 'Fred2', 'Pofview', 'Nebedit', 'Fonttool', 'Ac', 'Cfilearchiver', 'Cryptstring', 'Scramble', 'HelpEd'
$all = @()
$failed = 0
foreach ($p in $projects) {
	foreach ($c in 'Release', 'Debug') {
		# code.lib is shared by most projects; a stale one from another branch or variant would hide its warnings
		Get-ChildItem $RepoDir -Recurse -Filter code.lib -ErrorAction SilentlyContinue | Remove-Item
		# Rebuild first cleans what the last build wrote, as listed in the .tlog folders, and that includes the exes a
		# normal build put in the game folder; without the lists only this check's own outputs are replaced
		Get-ChildItem $RepoDir -Recurse -Directory -Filter *.tlog -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
		$log = Join-Path $Out "${p}_$c.txt"
		& $msb (Join-Path $RepoDir "$p.vcxproj") /t:Rebuild /nodeReuse:false "/p:Configuration=$c" /p:Platform=Win32 "/p:SolutionDir=$RepoDir\" /m /v:m /nologo > $log 2>&1
		$code = $LASTEXITCODE
		$w = @(Select-String $log -Pattern 'warning [A-Z]+\d+' | ForEach-Object { ($_.Line -replace '\s*\[[^\]]*\]\s*$', '').Trim() } | Sort-Object -Unique)
		$state = if ($code) { 'BUILD FAILED' } elseif ($w.Count) { "$($w.Count) warnings" } else { 'ok' }
		if ($code -or $w.Count) { $failed++ }
		'{0,-6} {1} {2}' -f $(if ($code -or $w.Count) { 'FAIL' } else { 'ok' }), "$p $c", $(if ($state -ne 'ok') { $state } else { '' })
		$all += $w
	}
}
Get-ChildItem $RepoDir -Recurse -Filter code.lib -ErrorAction SilentlyContinue | Remove-Item

$all = @($all | Sort-Object -Unique)
foreach ($w in $all) { '       ' + ($w -replace [regex]::Escape("$RepoDir\"), '') }
if ($failed) { "FAIL   $($all.Count) distinct warnings in $failed project builds" } else { 'PASS' }
