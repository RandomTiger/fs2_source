# Gate for FS2Demo and Enhanced (no suite_check there): demo build into FS2DEMO_TEST, retail Rebuild, quick replays
# with the demo set, tools check, and warning_check with -Warn. Last line PASS or FAIL.
param([string]$Repo = '', [string]$Label = 'demo_gate', [switch]$Warn)
$sp = $PSScriptRoot
. "$PSScriptRoot\common.ps1"
Require-Env PATH_FS2
Initialize-Fs2DemoScratch	# FS2_PROCDUMP (optional) = procdump.exe, for crash dumps
if (-not $Repo) { $Repo = $RepoRoot }
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
$bad = @()
if ($Warn) {
	$w = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\warning_check.ps1" -RepoDir $Repo | Select-Object -Last 1
	"warning_check: $w"; if ($w -notmatch '^PASS') { $bad += 'warnings' }
}
$d = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\build_demo.ps1" | Select-Object -Last 3
"demo build: $($d -join ' / ')"; if ($d[-1] -notmatch '^PASS') { $bad += 'demo build' }
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msb "$Repo\tools\TraceDiff\TraceDiff.vcxproj" /p:Configuration=Release /p:Platform=Win32 "/p:SolutionDir=$Repo\" /m /v:q /nologo > "$env:TEMP\${Label}_td.txt" 2>&1
foreach ($c in 'Release', 'Debug') {
	Get-ChildItem $Repo -Recurse -Filter code.lib | Remove-Item
	& $msb "$Repo\Freespace2.vcxproj" /t:Rebuild /nodeReuse:false "/p:Configuration=$c" /p:Platform=Win32 "/p:SolutionDir=$Repo\" /m /v:q /nologo > "$env:TEMP\${Label}_$c.txt" 2>&1
	"retail $c exit $LASTEXITCODE"; if ($LASTEXITCODE) { $bad += "retail $c" }
}
$m = @(Get-ChildItem "$Repo\recordings\autotest" -Filter 'Autotest*.fs2rec' | ForEach-Object { $_.BaseName }) + @('sm1-01', 'sm3-06') + @(Get-ChildItem "$Repo\recordings\demo" -Filter *.fs2rec | ForEach-Object { $_.BaseName })
$r = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\replay_check.ps1" -Configs r,d -CrossCheck 'sm1-01,sm3-06' -Missions ($m -join ',') -Out "$env:TEMP\$Label"
$r | Where-Object { $_ -match '^(FAIL|SKIP)' } | ForEach-Object { $_.Substring(0, [Math]::Min(170, $_.Length)) }
$last = $r | Select-Object -Last 1; "replays: $last"
$fails = @($r | Where-Object { $_ -match '^FAIL ' })
if ($fails.Count -or ($r -match '^SKIP')) { $bad += 'replays' }
$t = & powershell -NoProfile -ExecutionPolicy Bypass -File "$Repo\tools\tools_check.ps1" -Game $env:PATH_FS2 | Select-Object -Last 1
"tools: $t"; if ($t -notmatch '^PASS') { $bad += 'tools' }
if ($bad) { "FAIL $($bad -join ', ')" } else { 'PASS' }
