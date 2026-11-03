<#
Refreshes the FS2 demo dev copy (PATH_FS2DEMO), the folder tools\build_demo.ps1 builds into and the demo tests run
in, from this PC's clean demo install (D-20261019-213450).

  tools\make_demo_test.ps1 [-From <clean demo install>] [-To <dev copy>]

-From defaults to $env:PATH_FS2DEMO_CLEAN (its fs2demo\ folder when the VPs are there) and is only read: nothing is
ever written to it. -To defaults to $env:PATH_FS2DEMO and is made if missing. The demo's VPs are copied over the
ones in -To; the exes, pilots, fs2.ini and logs already there are left alone (the game writes those on its first
run). Nothing is downloaded: on a PC without the demo, install it from its own installer (or tools\demo_install.py)
and name the folder in PATH_FS2DEMO_CLEAN.
#>
param(
	[string]$From = $env:PATH_FS2DEMO_CLEAN,
	[string]$To = $env:PATH_FS2DEMO
)

if (-not $From) { throw 'Set PATH_FS2DEMO_CLEAN (the clean demo install) or pass -From' }
if (-not $To) { throw 'Set PATH_FS2DEMO (the demo dev copy) or pass -To' }
if (-not (Test-Path (Join-Path $From 'root_fs2.vp')) -and (Test-Path (Join-Path $From 'fs2demo\root_fs2.vp'))) { $From = Join-Path $From 'fs2demo' }
if (-not (Test-Path (Join-Path $From 'root_fs2.vp'))) { throw "No demo install with root_fs2.vp at '$From' (-From or PATH_FS2DEMO_CLEAN)" }
$f = (Resolve-Path $From).Path.TrimEnd('\') + '\'
$t = [IO.Path]::GetFullPath($To).TrimEnd('\') + '\'
if ($t.StartsWith($f, [StringComparison]::OrdinalIgnoreCase) -or $f.StartsWith($t, [StringComparison]::OrdinalIgnoreCase)) {
	throw "The dev copy '$To' must not be, or hold, or be inside the clean install '$From'"
}
New-Item -ItemType Directory -Force $To | Out-Null
Get-ChildItem $From -Filter *.vp | Copy-Item -Destination $To -Force
Get-ChildItem $To -Filter *.vp | Select-Object Name, Length | Format-Table -AutoSize | Out-String