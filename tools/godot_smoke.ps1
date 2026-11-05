<#
Windowed Godot smoke test (G3): a visible Godot window runs a live autotest mission on the game fiber, one game resume per Godot
_process. Passes when the game ends with exit 0 and Godot's window kept processing events meanwhile: runner.gd (FS2_SMOKE=1) logs
the gap between Godot frames and every window event; this script resizes the window and takes a screenshot during the mission and
logs what Windows reports for the window (the hung reading is logged only, it does not fail the run). Runs on the scratch pilot AutotestPilot, not a real pilot. Exits non-zero when the game does or autotest.log says FAIL. Proof goes to <Out>: smoke.out.txt, smoke_during.png, smoke.txt.
Usage: tools\godot_smoke.ps1 [-Config r|d] [-Mission sm1-01] [-Seconds 90] [-Out dir] [-HostRenderer]
The default form draws through the null renderer, so it tests the fiber and the window only. -HostRenderer draws through the host renderer. With FS2_RENDER3D=1 it
fails unless the game reports billboards on the sink and 0 faults; FS2_RENDER3D=1 without the switch is refused, as nothing would read it (#188).
#>
param([string]$Config = 'r', [string]$Mission = 'sm1-01', [int]$Seconds = 90, [string]$Out,
	[string]$GameDir = $env:PATH_FS2, [string]$Godot, [switch]$HostRenderer)
# the game's render3d line in its stderr: empty when it is good, else why not (#188)
function Get-Render3DFault([string]$ErrFile) {
	$m = Select-String $ErrFile -Pattern 'render3d: billboards: (\d+) on the sink, (\d+) faults' -ErrorAction SilentlyContinue | Select-Object -Last 1
	if (-not $m) { return 'no render3d line in the game''s stderr' }
	$n = [int]$m.Matches[0].Groups[1].Value; $f = [int]$m.Matches[0].Groups[2].Value
	if ($n -le 0) { return "no billboards reached the sink ($n)" }
	if ($f -ne 0) { return "$f render3d faults" }
	return ''
}
$want3D = $env:FS2_RENDER3D -eq '1'
if ($want3D -and -not $HostRenderer) { Write-Output 'FAIL: FS2_RENDER3D=1 does nothing without -HostRenderer: the null renderer never reads it (#188)'; exit 2 }
if (-not $Godot) { if (-not $env:GODOT_ENGINE_4_7) { throw 'GODOT_ENGINE_4_7 is not set (folder holding the Godot 4.7.2 exes), and no -Godot given' }; $Godot = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64.exe' }
$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"	# exe and DLL names (D-20261101-064106)
if (-not $Out) { $Out = Join-Path $repo ('replays\godot_smoke_' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W32 {
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hgt, bool repaint);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr res);
 delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
 // Godot's window by its class: in Debug the process's main window is the game's Debug Spew console (#226)
 public static IntPtr GodotWindow(int id) {
  IntPtr found = IntPtr.Zero;
  EnumWindows(delegate(IntPtr h, IntPtr l) {
   uint pid; GetWindowThreadProcessId(h, out pid);
   if (pid != (uint)id || !IsWindowVisible(h)) return true;
   var s = new System.Text.StringBuilder(64); GetClassName(h, s, 64);
   if (s.ToString() != "Engine") return true;
   found = h; return false;
  }, IntPtr.Zero);
  return found;
 }
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
[void][W32]::SetProcessDPIAware()	# real pixels, or a scaled desktop gives the window's rectangle and the resize in other units (#228)
$env:FS2_DLL = Join-Path $GameDir (Get-FS2Dll $Config); $env:FS2_ROOT = $GameDir; $env:FS2_FIBER = '1'; $env:FS2_FRAMES = '1'; $env:FS2_SMOKE = '1'
$oldHost = $env:FS2_GR_HOST
if ($HostRenderer) { $env:FS2_GR_HOST = '1' }
$env:FS2_CMD = "-window -autotest $(if (-not $HostRenderer) { '-headless ' })-at_mission $Mission -at_pilot AutotestPilot -at_time $Seconds"
Remove-Item (Join-Path $GameDir 'autotest.log') -ErrorAction SilentlyContinue
Remove-Item "$Out\smoke.err.txt" -ErrorAction SilentlyContinue	# an old render3d line must not pass this run
Set-Location $GameDir
$p = Start-Process $Godot -ArgumentList "--path `"$(Join-Path $repo 'godot')`" --windowed --resolution 800x450" -PassThru -RedirectStandardOutput "$Out\smoke.out.txt" -RedirectStandardError "$Out\smoke.err.txt"
$null = $p.Handle
$res = @(); $t0 = Get-Date
Start-Sleep -Seconds 8
$p.Refresh(); $h = [W32]::GodotWindow($p.Id)
$res += "window handle: $h, process main window: $($p.MainWindowTitle)"
if ($h -eq [IntPtr]::Zero) { $res += 'no Godot window found: window checks not run' }
if ($h -ne [IntPtr]::Zero) {
	$res += "hung at +8s: $([W32]::IsHungAppWindow($h))"
	[W32+RECT]$r = New-Object W32+RECT; [void][W32]::GetWindowRect($h, [ref]$r)
	$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
	[System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
	$bmp.Save("$Out\smoke_during.png"); $res += "screenshot saved ($($r.R-$r.L)x$($r.B-$r.T))"
	[void][W32]::MoveWindow($h, $r.L + 40, $r.T + 40, 640, 400, $true); $res += "window moved and resized at +$([int]((Get-Date)-$t0).TotalSeconds)s"
	Start-Sleep -Seconds 3
	$o = [IntPtr]::Zero
	$ok = [W32]::SendMessageTimeout($h, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 2000, [ref]$o)
	$res += "WM_NULL answered within 2 s: $($ok -ne [IntPtr]::Zero)"
	$res += "hung at +11s: $([W32]::IsHungAppWindow($h))"
}
$done = $p.WaitForExit(($Seconds + 600) * 1000)
if (-not $done) { Stop-Process $p -Force; $res += 'TIMEOUT' }
$code = if ($done) { $p.ExitCode } else { 'TIMEOUT' }
$res += "godot exit=$code"
$o = Get-Content "$Out\smoke.out.txt"
$gaps = @($o | Select-String 'max gap between Godot frames=(\d+)' | % { [int]$_.Matches[0].Groups[1].Value })
$res += "smoke log lines: $($gaps.Count), worst gap between Godot frames: $(($gaps | Measure-Object -Maximum).Maximum) ms"
$res += ($o | Select-String 'size_changed' | Select-Object -First 3 | % { $_.Line })
$res += ($o | Select-String 'game finished' | % { $_.Line })
$res += ($o | Select-String 'smoke: t=' | Select-Object -First 1 | % { $_.Line })
$res += ($o | Select-String 'smoke: t=' | Select-Object -Last 1 | % { $_.Line })
$res += "autotest.log: " + ((Select-String (Join-Path $GameDir 'autotest.log') -Pattern 'PASS|FAIL|Asserts' | Select-Object -Last 2 | % { $_.Line }) -join ' | ')
$res += (Select-String "$Out\smoke.err.txt" -Pattern 'render3d: billboards' -ErrorAction SilentlyContinue | Select-Object -Last 1 | % { $_.Line })
$fault3D = if ($want3D) { Get-Render3DFault "$Out\smoke.err.txt" } else { '' }
if ($fault3D) { $res += "FAIL: $fault3D" }
Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD, Env:FS2_FIBER, Env:FS2_FRAMES, Env:FS2_SMOKE -ErrorAction SilentlyContinue
$env:FS2_GR_HOST = $oldHost	# empty removes it
Get-ChildItem (Join-Path $GameDir 'data\players') -Recurse -File -Filter 'AutotestPilot.*' -ErrorAction SilentlyContinue | Remove-Item -Force	# the game makes this scratch pilot when it is missing: leave none in the install (#164)
$res | Set-Content "$Out\smoke.txt"; $res
if ($h -eq [IntPtr]::Zero -or $fault3D -or $code -ne 0 -or (Select-String (Join-Path $GameDir 'autotest.log') -Pattern '\bFAIL\b' -CaseSensitive -Quiet)) { exit 1 }
