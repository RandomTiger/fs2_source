# x64 build and headless replay of every recording, Release and Debug, for the x64 layers.
param([string[]]$Branches = @('PrePort64'))
$Branches = @($Branches | ForEach-Object { $_ -split ',' })
. "$PSScriptRoot\common.ps1"
$gh = Split-Path $RepoRoot -Parent
Require-Env PATH_FS2
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null
$msb = Get-MSBuild
foreach ($b in $Branches) {
	$repo = "$gh\fs2_source_$b"
	foreach ($c in 'Release', 'Debug') {
		Remove-Item "$env:PATH_FS2\FS2vs2022$(if ($c -eq 'Release') {'r'} else {'d'})64.exe" -ErrorAction SilentlyContinue
		$f = "$env:TEMP\x64_$b`_$c.txt"
		& $msb "$repo\Freespace2.vcxproj" /p:Configuration=$c /p:Platform=x64 "/p:SolutionDir=$repo\" /v:q /nologo /m 2>&1 | Out-File $f
		if (Select-String $f -Pattern ': (fatal )?error ') { "$b x64 $c BUILD FAILED"; exit 1 }
	}
	$o = "$env:TEMP\x64_$b"
	foreach ($e in 'r64', 'd64') {
		$l = & powershell -NoProfile -ExecutionPolicy Bypass -File "$repo\tools\nodraw_check.ps1" -Headless -Config $e -Out "$o\headless_$e" | Select-Object -Last 1
		"$b x64 $e`: $l"
		if ($l -notmatch 'fail=0') { "$b x64 FAIL"; exit 1 }
	}
}
'x64 PASS'
