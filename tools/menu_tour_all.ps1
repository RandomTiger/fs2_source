<#
Runs the autotest menu tour (tools\menu_check.ps1, docs/AUTOTEST.md) on the four data sets, Release and Debug:
FS2 retail, the FS2 demo, FS1 retail and the FS1 demo (docs/FS1_DEMO.md, "Menu tour on the four data sets").

  tools\menu_tour_all.ps1 [-Sets fs2,fs2demo,fs1,fs1demo] [-Configs r,d] [-Shots <dir>] [-Out <dir>]
                          [-Fs2Dir <dir>] [-Fs2DemoDir <dir>] [-Fs1Dir <dir>] [-Fs1DemoDir <dir>] [-NoBuild]

The folders default to $env:PATH_FS2 (FS2 retail), $env:FS2DEMO_TEST (the FS2 demo test folder,
tools\make_demo_test.ps1), $env:PATH_FS1 (FS1 retail) and $env:FS1DEMO_PATH (the FS1 demo folder, fsdemo10.exe
beside the exes). The FS2 demo gets an FS2_DEMO build (tools\build_demo.ps1); the other three get the same build,
which picks its data set from the folder. The exes already in the folders are saved first and put back at the end
(other branches build into them too). A set whose folder is missing is SKIP.
Prints one line per screen per build, "<set> <config> Screen '<category>/<screen>': PASS|FAIL <why>|SKIP <why>",
one line per build, and DONE pass=N fail=N (builds). Results and each autotest.log go to <Out>\<set> (default
%LOCALAPPDATA%\menu_tour_all). -Shots saves each screen as <Shots>\<set>\<config>\<category>\<screen>.bmp; keep it
outside the repo, the screenshots show game data. Build or run nothing else meanwhile.
#>
param(
	[string[]]$Sets = @('fs2', 'fs2demo', 'fs1', 'fs1demo'),
	[string[]]$Configs = @('r', 'd'),
	[string]$Shots,
	[string]$Out = (Join-Path $env:LOCALAPPDATA 'menu_tour_all'),
	[string]$Fs2Dir = $env:PATH_FS2,
	[string]$Fs2DemoDir = $env:FS2DEMO_TEST,
	[string]$Fs1Dir = $env:PATH_FS1,
	[string]$Fs1DemoDir = $env:FS1DEMO_PATH,
	[switch]$NoBuild
)

$repo = Split-Path $PSScriptRoot
# powershell -File passes "a,b" as one string
$Sets = @($Sets | ForEach-Object { $_ -split ',' })
$Configs = @($Configs | ForEach-Object { $_ -split ',' })
$dirs = @{ fs2 = $Fs2Dir; fs2demo = $Fs2DemoDir; fs1 = $Fs1Dir; fs1demo = $Fs1DemoDir }
if (Get-Process FS2vs2022* -ErrorAction SilentlyContinue) { 'FAIL the game is running'; exit 1 }
New-Item -ItemType Directory -Force $Out | Out-Null
$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
$env:CL = $null; $env:_CL_ = $null; $env:_LINK_ = $null

$run = @($Sets | Where-Object { $dirs[$_] -and (Test-Path $dirs[$_]) })
foreach ($s in $Sets) { if ($run -notcontains $s) { "SKIP $s : no folder (see the parameters)" } }
$bak = Join-Path $Out 'exe_backup'
# the build links into PATH_FS2 (the -Fs2Dir folder), so that folder is saved and put back even when fs2 is not toured
$keep = @($run)
if (-not $NoBuild -and $keep -notcontains 'fs2' -and $Fs2Dir -and (Test-Path $Fs2Dir)) { $keep += 'fs2' }
foreach ($s in $keep) {
	$b = Join-Path $bak $s
	New-Item -ItemType Directory -Force $b | Out-Null
	Copy-Item (Join-Path $dirs[$s] 'FS2vs2022r.exe'), (Join-Path $dirs[$s] 'FS2vs2022d.exe') $b -ErrorAction SilentlyContinue
}

$pass = 0; $fail = 0
try {
	$built = $null
	foreach ($s in $run) {
		$dir = $dirs[$s]
		if (-not $NoBuild) {
			if ($s -eq 'fs2demo') {
				# FS1 branches link into PATH_FS1
				$env:PATH_FS1 = $dir
				$r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build_demo.ps1') -GameDir $dir | Select-Object -Last 1
				if ($r -notmatch '^PASS') { "FAIL $s build: $r"; $fail++; continue }
			} elseif ($built) {
				Copy-Item (Join-Path $built 'FS2vs2022r.exe'), (Join-Path $built 'FS2vs2022d.exe') $dir -Force
			} else {
				$env:PATH_FS1 = $dir
				$env:PATH_FS2 = $Fs2Dir
				# every worktree links into the same game folders: delete first so no other branch's exe survives a no-op link
				$targets = @($run | Where-Object { $_ -ne 'fs2demo' } | ForEach-Object { $dirs[$_] })
				foreach ($d in @($Fs2Dir) + $targets) { Remove-Item (Join-Path $d 'FS2vs2022r.exe'), (Join-Path $d 'FS2vs2022d.exe') -ErrorAction SilentlyContinue }
				$ok = $true
				foreach ($c in 'Release', 'Debug') {
					& $msb (Join-Path $repo 'Freespace2.vcxproj') "/p:Configuration=$c" /p:Platform=Win32 /m /v:m /nologo > (Join-Path $Out "build_$c.log") 2>&1
					if ($LASTEXITCODE) { "FAIL $s build $c ($Out\build_$c.log)"; $ok = $false; break }
				}
				if (-not $ok) { $fail++; break }
				# take the exes from wherever the build linked them: the set's folder, else PATH_FS2
				$built = @($dir, $Fs2Dir) | Where-Object { (Test-Path (Join-Path $_ 'FS2vs2022r.exe')) -and (Test-Path (Join-Path $_ 'FS2vs2022d.exe')) } | Select-Object -First 1
				if (-not $built) { "FAIL $s build: no fresh FS2vs2022r/d.exe in $dir or $Fs2Dir"; $fail++; break }
				foreach ($d in $targets) { if ($d -ne $built) { Copy-Item (Join-Path $built 'FS2vs2022r.exe'), (Join-Path $built 'FS2vs2022d.exe') $d -Force } }
			}
		}
		foreach ($c in $Configs) {
			$e = Join-Path $dir "FS2vs2022$c.exe"
			if (Test-Path $e) { "$s $c exe built $((Get-Item $e).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))" } else { "$s $c exe missing: $e" }
		}
		$a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'menu_check.ps1'), '-Out', (Join-Path $Out $s), '-Configs', ($Configs -join ','), '-GameDir', $dir)
		if ($Shots) { $a += '-Shots', (Join-Path $Shots $s) }
		Remove-Item (Join-Path (Join-Path $Out $s) 'summary.txt') -ErrorAction SilentlyContinue
		& powershell @a | Where-Object { $_ -notmatch '^DONE' } | ForEach-Object {
			if ($_ -match '^(FAIL )?(r|d) exit=') { if ($_ -match '^FAIL') { $fail++ } else { $pass++ } }
			if ($_ -match '^FAIL ') { "FAIL $s $($_.Substring(5))" } else { "$s $_" }
		}
	}
} finally {
	foreach ($s in $keep) { Copy-Item (Join-Path (Join-Path $bak $s) '*.exe') $dirs[$s] -Force -ErrorAction SilentlyContinue }
	'exes put back'
}
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
