# Builds every tool project in Release and checks each one does its basic job (the VS2022 standard: every project
# compiles and runs). Part of the quick suite.
#   Command-line tools: Cryptstring encrypts, Scramble round-trips a file, Cfilearchiver packs a VP, Fonttool
#   copies font kerning, Ac starts. GUI tools, from the game folder: FRED opens a mission (File > Open) and saves it, Pofview opens
#   a model given on its command line, Nebedit opens. HelpEd is built only.
# GUI checks send keys to the windows they open, so run this on an interactive desktop and leave the keyboard alone.
# Usage: powershell -File tools\tools_check.ps1 [-Game <FreeSpace 2 folder>] [-NoBuild] [-RepoDir <checkout>]
#   -RepoDir checks another checkout (a lower layer such as VS2022, which has no copy of this script).
# Exit code 0 when every check passes, 1 otherwise.
param([string]$Game = $env:PATH_FS2, [switch]$NoBuild, [string]$RepoDir)
$repo = if ($RepoDir) { $RepoDir } else { Split-Path $PSScriptRoot -Parent }
if (-not $Game -or -not (Test-Path "$Game\Root_fs2.vp")) { Write-Output "FAIL   no FreeSpace 2 folder: set PATH_FS2 or pass -Game"; exit 1 }
$w = Join-Path $env:TEMP "fs2_tools_check"
Remove-Item $w -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$w\bin", "$w\pack\data" | Out-Null
$failed = 0

# Known failures, by layer (D-20261018-120739): original-code bugs that are fixed on a higher layer, keyed by the tool
# name a run check starts with. A listed failure is reported but not counted; a listed check that passes is counted,
# so the list cannot go stale. Builds are never listed.
# Empty: Tom moved the tool heap fixes (#11, #22) onto VS2022 on 2026-10-18, so every layer passes outright.
# Example entry: 'VS2022' = @{ 'Fonttool' = '#22, fixed on OriginalSourceBugFixes (review 49)' }
$knownByLayer = @{}
$layer = git -C $repo rev-parse --abbrev-ref HEAD 2>$null
$known = if ($layer -and $knownByLayer.ContainsKey($layer)) { $knownByLayer[$layer] } else { @{} }
$knownSeen = @()
function Out-Result($ok, $msg) {
	$tool = ($msg -split ' ')[0]
	if ($known.ContainsKey($tool)) {
		if ($ok) { Write-Output "STALE  $msg (listed as a known failure on ${layer}: $($known[$tool]); remove it from the list)"; $script:failed++ }
		else { Write-Output "known  $msg ($($known[$tool]))"; $script:knownSeen += $tool }
		return
	}
	if ($ok) { Write-Output "ok     $msg" } else { Write-Output "FAIL   $msg"; $script:failed++ }
}

# FRED links into PATH_FS2; send it to the scratch folder so the game folder is not touched by the build
if (-not $NoBuild) {
	$saved = $env:PATH_FS2; $env:PATH_FS2 = "$w\bin"
	$msb = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
	foreach ($p in 'Ac', 'Cfilearchiver', 'Cryptstring', 'Fonttool', 'HelpEd', 'Nebedit', 'Pofview', 'Scramble', 'Fred2') {
		$log = "$w\build_$p.txt"
		& $msb "$repo\$p.vcxproj" /p:Configuration=Release /p:Platform=Win32 "/p:SolutionDir=$repo\" /m /v:q /nologo 2>&1 | Out-File $log
		$err = @(Select-String $log -Pattern ': (fatal )?error ')
		Out-Result ($LASTEXITCODE -eq 0 -and $err.Count -eq 0) "build $p$(if ($err) { ': ' + ($err[0].Line -replace ' \[.*$', '') })"
	}
	$env:PATH_FS2 = $saved
}
$fred = if (Test-Path "$w\bin\Fred2.exe") { "$w\bin\Fred2.exe" } else { "$Game\Fred2.exe" }

function Run($exe, $argList) {
	Remove-Item "$w\out.txt", "$w\err.txt" -ErrorAction SilentlyContinue
	$a = @{ FilePath = $exe; WorkingDirectory = $w; RedirectStandardOutput = "$w\out.txt"; RedirectStandardError = "$w\err.txt"; NoNewWindow = $true; PassThru = $true }
	if ($argList) { $a.ArgumentList = $argList }
	$p = Start-Process @a
	$null = $p.Handle		# keeps the handle open, or ExitCode reads empty after exit
	if (-not $p.WaitForExit(60000)) { Stop-Process $p -Force; return 'TIMEOUT' }
	return $p.ExitCode
}
# one file out of the game's VP archives (header: "VPVP", version, index offset, count; entries: offset, size, name[32], time)
function Get-VpFile($name, $dest) {
	foreach ($vp in Get-ChildItem "$Game\*.vp") {
		$b = [IO.File]::ReadAllBytes($vp.FullName)
		$dir = [BitConverter]::ToInt32($b, 8); $n = [BitConverter]::ToInt32($b, 12)
		for ($i = 0; $i -lt $n; $i++) {
			$e = $dir + 44 * $i
			$en = [Text.Encoding]::ASCII.GetString($b, $e + 8, 32).Split([char]0)[0]
			if ($en -ieq $name) {
				$off = [BitConverter]::ToInt32($b, $e); $size = [BitConverter]::ToInt32($b, $e + 4)
				$out = New-Object byte[] $size; [Array]::Copy($b, $off, $out, 0, $size); [IO.File]::WriteAllBytes($dest, $out); return $true
			}
		}
	}
	return $false
}

# ---- command-line tools
$code = Run "$repo\Cryptstring.exe" 'hello'
Out-Result ($code -eq 0 -and (Get-Content "$w\out.txt" -Raw) -match '\S') "Cryptstring encrypts a string"
Set-Content "$w\plain.txt" ('FreeSpace 2 scramble test ' * 20) -Encoding ascii
$c1 = Run "$repo\Scramble.exe" "`"$w\plain.txt`" `"$w\enc.bin`""
$c2 = Run "$repo\Scramble.exe" "-u `"$w\enc.bin`" `"$w\dec.txt`""
$same = (Test-Path "$w\dec.txt") -and ((Get-FileHash "$w\plain.txt").Hash -eq (Get-FileHash "$w\dec.txt").Hash)
$diff = (Test-Path "$w\enc.bin") -and ((Get-FileHash "$w\plain.txt").Hash -ne (Get-FileHash "$w\enc.bin").Hash)
Out-Result ($c1 -eq 0 -and $c2 -eq 0 -and $same -and $diff) "Scramble encrypts and decrypts a file"
Set-Content "$w\pack\data\a.txt" 'alpha' -Encoding ascii; Set-Content "$w\pack\data\b.txt" 'bravo bravo' -Encoding ascii
$code = Run "$repo\Cfilearchiver.exe" "test `"$w\pack`""
$vp = Get-ChildItem $w -Filter *.vp -Recurse | Select-Object -First 1
$names = @()
if ($vp) {
	$b = [IO.File]::ReadAllBytes($vp.FullName); $dir = [BitConverter]::ToInt32($b, 8)
	$names = for ($i = 0; $i -lt [BitConverter]::ToInt32($b, 12); $i++) { [Text.Encoding]::ASCII.GetString($b, $dir + 44 * $i + 8, 32).Split([char]0)[0] }
}
Out-Result (($names -contains 'a.txt') -and ($names -contains 'b.txt')) "Cfilearchiver packs a VP (exit $code, entries: $($names -join ' '))"
# Fonttool: copy the kerning of one game font into another (the template FontTool.pcx is a layout, not a font sheet)
if ((Get-VpFile 'font01.vf' "$w\a.vf") -and (Get-VpFile 'font02.vf' "$w\b.vf")) {
	$h = (Get-FileHash "$w\b.vf").Hash
	$code = Run "$repo\Fonttool.exe" 'a.vf b.vf'
	Out-Result ($code -eq 0 -and (Get-FileHash "$w\b.vf").Hash -ne $h) "Fonttool copies kerning between fonts (exit $code)"
} else { Out-Result $false 'Fonttool: font01.vf or font02.vf not found in the VPs' }
$code = Run "$repo\Ac.exe" ''
Out-Result ($code -ne 'TIMEOUT' -and (Get-Content "$w\out.txt", "$w\err.txt" -Raw -ErrorAction SilentlyContinue) -match '\S') "Ac starts and prints its usage (exit $code)"

# ---- GUI tools, run from the game folder (they read its data); copies get their own names and are removed after
$ws = New-Object -ComObject WScript.Shell
# Window texts read with plain Win32 calls, which never wait on the tool (UI Automation hung on Nebedit's busy loop)
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class ToolWindows {
	delegate bool EnumProc(IntPtr h, IntPtr l);
	[DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
	[DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
	[DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
	[DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
	[DllImport("user32.dll", CharSet = CharSet.Auto)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
	static string Text(IntPtr h) { var s = new StringBuilder(512); GetWindowText(h, s, 512); return s.ToString(); }
	// visible top-level windows of a process, each followed by its controls' texts
	[DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();

	public static bool IsForeground(int pid) { uint p; GetWindowThreadProcessId(GetForegroundWindow(), out p); return p == pid; }
	[DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
	[DllImport("user32.dll", CharSet = CharSet.Auto)] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
	[DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);
	[DllImport("user32.dll", CharSet = CharSet.Auto)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
	static string Class(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
	// menu commands go straight to the window, so they need neither focus nor the keyboard
	public static bool Command(IntPtr h, int id) { return PostMessage(h, 0x0111, (IntPtr)id, IntPtr.Zero); }
	// fill the file name of a process's open file dialog and press OK; false while no such dialog is up
	public static bool FileDialog(int pid, string name) {
		IntPtr dlg = IntPtr.Zero, edit = IntPtr.Zero;
		EnumWindows((h, l) => {
			uint p; GetWindowThreadProcessId(h, out p);
			if (p != pid || !IsWindowVisible(h) || Class(h) != "#32770") return true;
			IntPtr box = IntPtr.Zero;
			EnumChildWindows(h, (c, l2) => { if (GetDlgCtrlID(c) == 0x47C) { box = c; return false; } return true; }, IntPtr.Zero);
			if (box == IntPtr.Zero) return true;
			EnumChildWindows(box, (c, l2) => { if (Class(c) == "Edit") { edit = c; return false; } return true; }, IntPtr.Zero);
			if (edit != IntPtr.Zero) { dlg = h; return false; }
			return true;
		}, IntPtr.Zero);
		if (dlg == IntPtr.Zero) return false;
		SendMessage(edit, 0x000C, IntPtr.Zero, name);	// WM_SETTEXT
		return Command(dlg, 1);						// IDOK
	}
	public static List<string> Of(int pid) {
		var r = new List<string>();
		EnumWindows((h, l) => {
			uint p; GetWindowThreadProcessId(h, out p);
			if (p == pid && IsWindowVisible(h)) { r.Add(Text(h)); EnumChildWindows(h, (c, l2) => { r.Add(Text(c)); return true; }, IntPtr.Zero); }
			return true;
		}, IntPtr.Zero);
		return r;
	}
}
"@
function Texts($p) { ((([ToolWindows]::Of($p.Id)) | Where-Object { $_ } | Select-Object -Unique) -join ' / ') }
function Wait-Text($p, $pattern, $secs) {
	for ($i = 0; $i -lt $secs; $i++) { if ($p.HasExited) { return $false }; if ((Texts $p) -match $pattern) { return $true }; Start-Sleep 1 }
	return $false
}
function Check-Gui($name, $p, $want, $extraOk, $extra) {
	$p.Refresh()
	if ($p.HasExited) { Out-Result $false "$name exited (code $('0x{0:X}' -f $p.ExitCode)) $extra"; return }
	$t = Texts $p
	$bad = $t -match 'Error|Assert|Runtime|exception'
	Out-Result ((-not $bad) -and ($t -match $want) -and $extraOk) "$name $extra$(if ($bad -or $t -notmatch $want) { " windows: '$($t.Substring(0, [Math]::Min(200, $t.Length)))'" })"
	Stop-Process $p -Force
}
$copies = @{ "$Game\Fred2_check.exe" = $fred; "$Game\Pofview_check.exe" = "$repo\Pofview.exe"; "$Game\Nebedit_check.exe" = "$repo\Nebedit.exe" }
foreach ($k in $copies.Keys) { Copy-Item $copies[$k] $k -Force }

# FRED: File > Open, the mission name, OK; then File > Save, and the file must still be a whole mission
$m = "$Game\data\missions\ToolsCheck.fs2"
if (Get-VpFile 'sm1-01.fs2' $m) {
	(Get-Item $m).LastWriteTime = (Get-Date).AddMinutes(-5)
	$before = (Get-Item $m).LastWriteTime
	$p = Start-Process "$Game\Fred2_check.exe" -WorkingDirectory $Game -PassThru
	# File > Open and File > Save go to FRED as window commands, and the name into the dialog's own edit box: no
	# keystrokes, so nothing depends on which window has the focus (keystrokes were lost about one run in three)
	$null = Wait-Text $p 'FRED2' 60; Start-Sleep 2
	$p.Refresh(); $main = $p.MainWindowHandle
	$null = [ToolWindows]::Command($main, 0xE101)		# ID_FILE_OPEN
	$open = $false
	for ($i = 0; $i -lt 15 -and -not $open; $i++) { Start-Sleep 1; $open = [ToolWindows]::FileDialog($p.Id, $m) }	# full path: a new PC's dialog starts outside data\missions
	$null = Wait-Text $p '^ToolsCheck' 30; Start-Sleep 3
	$title = ((Texts $p) -split ' / ')[0]
	$null = [ToolWindows]::Command($main, 0xE103)		# ID_FILE_SAVE
	$saved = $false
	for ($i = 0; $i -lt 15 -and -not $saved; $i++) { Start-Sleep 1; $saved = (Get-Item $m).LastWriteTime -gt $before }
	$whole = (Select-String $m -Pattern '#Mission Info' -Quiet) -and (Select-String $m -Pattern '#End' -Quiet)
	Check-Gui 'FRED opens and saves a mission' $p 'ToolsCheck' ($saved -and $whole) "(title '$title', saved $saved, whole $whole)"
	Remove-Item $m -ErrorAction SilentlyContinue
} else { Out-Result $false 'FRED: sm1-01.fs2 not found in the VPs' }

# Pofview: a model given on the command line, placed in the game folder root (no data folder, so the game never reads it)
if (Get-VpFile 'fighter01.pof' "$Game\fighter01.pof") {
	$p = Start-Process "$Game\Pofview_check.exe" -ArgumentList 'fighter01.pof' -WorkingDirectory $Game -PassThru
	Start-Sleep 15
	Check-Gui 'Pofview opens a model' $p 'fighter01' $true ''
	Remove-Item "$Game\fighter01.pof" -ErrorAction SilentlyContinue
} else { Out-Result $false 'Pofview: fighter01.pof not found in the VPs' }

$p = Start-Process "$Game\Nebedit_check.exe" -WorkingDirectory $Game -PassThru
Start-Sleep 15
Check-Gui 'Nebedit opens' $p '.' $true ''

Start-Sleep 2
foreach ($k in $copies.Keys) { Remove-Item $k -ErrorAction SilentlyContinue }
Write-Output $(if ($failed) { "FAIL   $failed check(s) failed" } elseif ($knownSeen) { "PASS (known failures on ${layer}: $($knownSeen -join ', '))" } else { 'PASS' })
exit [int]($failed -gt 0)
