<#
Makes the demo test folder: a copy of the FreeSpace 2 demo's VPs from this PC's known local copy, for
tools\build_demo.ps1 to build into and the demo suite to run in (D-20261019-213450).

  tools\make_demo_test.ps1 [-From <demo install>] [-To <test folder>]

-From defaults to $env:FS2DEMO_PATH and is only read. -To defaults to $env:FS2DEMO_TEST and must not exist yet or be
empty. Nothing is downloaded: on a PC without the demo, install it from its own installer (or tools\demo_install.py)
and name the folder in FS2DEMO_PATH. The game writes its fs2.ini, pilots and logs into the test folder on its first run.
#>
param(
	[string]$From = $env:FS2DEMO_PATH,
	[string]$To = $env:FS2DEMO_TEST
)

if (-not $From -or -not (Test-Path (Join-Path $From 'root_fs2.vp'))) { throw 'No demo install with root_fs2.vp (-From or FS2DEMO_PATH)' }
if (-not $To) { throw 'No test folder (-To or FS2DEMO_TEST)' }
if ((Resolve-Path $From).Path -eq [IO.Path]::GetFullPath($To)) { throw 'The test folder must not be the demo install' }
if ((Test-Path $To) -and (Get-ChildItem $To -Force | Select-Object -First 1)) { throw "$To is not empty" }
New-Item -ItemType Directory -Force $To | Out-Null
Get-ChildItem $From -Filter *.vp | Copy-Item -Destination $To
Get-ChildItem $To | Select-Object Name, Length | Format-Table -AutoSize | Out-String
