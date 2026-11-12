# Writes build\version_build.h with the port's own version (D-20261112, #311): <milestone>.<commits since it> <layer> <hash>[-dirty].
# The milestone is the newest v<major>.<minor> tag the checkout has; tags go on VS2022, so every branch sees them after its
# merge-up. Called by build\version.props before the resource compiler runs. Rewrites the file only when the text changes.
param([Parameter(Mandatory)][string]$Layer)
$out = Join-Path $PSScriptRoot 'version_build.h'
$v = $null
if (Get-Command git -ErrorAction SilentlyContinue) {
	$d = git -C $PSScriptRoot describe --tags --match 'v[0-9]*' --long --dirty --abbrev=7 2>$null
	if ($LASTEXITCODE -eq 0 -and "$d" -match '^v(\d+\.\d+)-(\d+)-g([0-9a-f]+)(-dirty)?$') {
		$v = "$($Matches[1]).$($Matches[2]) $Layer $($Matches[3])$($Matches[4])"
	} else {
		$n = git -C $PSScriptRoot rev-list --count HEAD 2>$null
		$h = git -C $PSScriptRoot rev-parse --short=7 HEAD 2>$null
		if ($n -and $h) { $v = "0.0.$n $Layer $h" }
	}
}
if (-not $v) { $v = "unknown $Layer" }
$text = "// Written by build\version_stamp.ps1 at build time; not in git.`r`n#define FS2_PORT_VERSION `"$v\0`"`r`n#define FS2_PORT_VERSION_STR `"$v`"`r`n"
$old = if (Test-Path $out) { [IO.File]::ReadAllText($out) } else { '' }
if ($old -ne $text) { [IO.File]::WriteAllText($out, $text) }
"FS2 port version: $v"
