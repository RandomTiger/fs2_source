<#
Runs the autotest menu tour (-autotest -at_menus, docs/AUTOTEST.md) through the Godot host, in a shown window with the host
renderer, on the data sets: every single-player screen, with each tab, page and help overlay, opens and closes with no Assert,
Int3, Warning or crash (#166). tools\menu_check.ps1 is the same tour on the native exes.

Usage: tools\godot_tour.ps1 [-Sets fs2,fs2demo,fs1,fs1demo] [-Configs r,d] [-Shots <dir>] [-Out <dir>]
                            [-Fs2Dir <dir>] [-Fs2DemoDir <dir>] [-Fs1Dir <dir>] [-Fs1DemoDir <dir>] [-Godot <exe>]

The folders default to PATH_FS2, PATH_FS2DEMO, PATH_FS1 and PATH_FS1DEMO, each holding this branch's game DLLs: build first (the
check runs what is there and names each DLL's time). A set whose folder or DLL is missing fails. The install's fs2.ini,
autotest.log and pilots are put back after each run; a pilot that still differs fails the run.
Prints one line per screen per build, "<set> <config> Screen '<category>/<screen>': PASS|FAIL <why>|SKIP <why>", one line per
build, and DONE pass=N fail=N (builds). Everything also goes to <Out>\summary.txt, each autotest.log beside it, and each run's
OS foreground window changes to <Out>\<set>_<config>.foreground.txt, with a count, and beside a FAIL the ones in the 4 s before that
screen's shot (#190): they only report, except that a run whose observer fails to start or dies fails. -Shots saves each
screen as <Shots>\<set>\<config>\<category>\<screen>.bmp: keep it outside the repo, the shots show game data. A Godot process
already running stops the check, as only one game can run: build or run nothing else meanwhile.
#>
param(
	[string[]]$Sets = @('fs2', 'fs2demo', 'fs1', 'fs1demo'),
	[string[]]$Configs = @('r', 'd'),
	[string]$Shots,
	[string]$Out,
	[string]$Fs2Dir = $env:PATH_FS2,
	[string]$Fs2DemoDir = $env:PATH_FS2DEMO,
	[string]$Fs1Dir = $env:PATH_FS1,
	[string]$Fs1DemoDir = $env:PATH_FS1DEMO,
	[string]$Godot
)
if (-not $Godot) { if (-not $env:GODOT_ENGINE_4_7) { throw 'GODOT_ENGINE_4_7 is not set (folder holding the Godot 4.7.2 exes), and no -Godot given' }; $Godot = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64.exe' }
$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"	# DLL names (D-20261101-064106)
if (-not $Out) { $Out = Join-Path $repo ('replays\godot_tour_' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$summary = Join-Path $Out 'summary.txt'
# powershell -File passes "a,b" as one string
$Sets = @($Sets | ForEach-Object { $_ -split ',' })
$Configs = @($Configs | ForEach-Object { $_ -split ',' })
$dirs = @{ fs2 = $Fs2Dir; fs2demo = $Fs2DemoDir; fs1 = $Fs1Dir; fs1demo = $Fs1DemoDir }

function Say([string]$Line) { $Line | Add-Content $summary; $Line }
# the FS1 demo keeps its data under fs1demo\ in its install
function PlayersDir([string]$Set, [string]$Dir) { if ($Set -eq 'fs1demo') { Join-Path $Dir 'fs1demo\data\players' } else { Join-Path $Dir 'data\players' } }
function Pilots([string]$Dir) { Get-ChildItem $Dir -Recurse -File -ErrorAction SilentlyContinue | Sort-Object FullName | ForEach-Object { "$($_.FullName) $((Get-FileHash $_.FullName).Hash)" } }

# Every change of the OS foreground window during a run (#190): a change empties the game's key queue, so a red can say whether
# one happened.  It only reports: no verdict depends on it.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
public static class FS2TourForeground {
	[DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
	static Thread Worker;
	static volatile bool Stopping;
	static readonly List<KeyValuePair<DateTime, string>> Changes = new List<KeyValuePair<DateTime, string>>();
	public static volatile bool Failed;
	static string Line(DateTime at, IntPtr h) {
		StringBuilder s = new StringBuilder(256);
		GetWindowText(h, s, 256);
		return at.ToString("HH:mm:ss.fff") + " 0x" + h.ToString("x") + " '" + s + "'";
	}
	public static void Start(string path) {
		ManualResetEvent ready = new ManualResetEvent(false);
		lock (Changes) { Changes.Clear(); }
		Stopping = false;
		Failed = true;
		Worker = new Thread(delegate() {
			try {
				using (StreamWriter w = new StreamWriter(path)) {
					w.AutoFlush = true;
					IntPtr last = GetForegroundWindow();
					w.WriteLine("start " + Line(DateTime.Now, last));
					Failed = false;
					ready.Set();
					while (!Stopping) {
						Thread.Sleep(10);
						IntPtr h = GetForegroundWindow();
						if (h != last) {
							DateTime at = DateTime.Now;
							string t = Line(at, h);
							last = h;
							w.WriteLine(t);
							lock (Changes) { Changes.Add(new KeyValuePair<DateTime, string>(at, t)); }
						}
					}
				}
			} catch (Exception) {
				Failed = true;
			}
		});
		Worker.IsBackground = true;
		Worker.Start();
		if (!ready.WaitOne(10000) || Failed) { throw new Exception("no first sample"); }
	}
	public static int Stop() {
		Stopping = true;
		Worker.Join(5000);
		lock (Changes) { return Changes.Count; }
	}
	public static string[] Between(DateTime from, DateTime to) {
		List<string> r = new List<string>();
		lock (Changes) { foreach (KeyValuePair<DateTime, string> c in Changes) { if (c.Key >= from && c.Key <= to) { r.Add(c.Value); } } }
		return r.ToArray();
	}
}
'@

if (Get-Process (@([IO.Path]::GetFileNameWithoutExtension($Godot)) + $FS2ProcessNames) -ErrorAction SilentlyContinue) { Say 'FAIL the game or Godot is already running'; exit 1 }

$pass = 0; $fail = 0
foreach ($s in $Sets) {
	foreach ($e in $Configs) {
		$dir = $dirs[$s]
		$dll = if ($dir) { Join-Path $dir (Get-FS2Dll $e) }
		if (-not $dll -or -not (Test-Path $dll)) { Say "FAIL $s $e no DLL: $dll"; $fail++; continue }
		Say "$s $e DLL built $((Get-Item $dll).LastWriteTime.ToString('yyyy-MM-dd HH:mm'))"

		# the scratch pilot, as in tools\menu_check.ps1, so the result never depends on a real pilot in this folder (#141)
		$pdir = PlayersDir $s $dir
		Get-ChildItem $pdir -Recurse -File -Filter 'AutotestPilot.*' -ErrorAction SilentlyContinue | Remove-Item -Force
		$bak = Join-Path $Out "bak_${s}_$e"
		New-Item -ItemType Directory -Force "$bak\players" | Out-Null
		foreach ($n in 'fs2.ini', 'autotest.log') { if (Test-Path (Join-Path $dir $n)) { Copy-Item (Join-Path $dir $n) $bak } }
		Copy-Item (Join-Path $pdir '*') "$bak\players" -Recurse -Force -ErrorAction SilentlyContinue
		$before = Pilots $pdir

		$log = Join-Path $dir 'autotest.log'
		Remove-Item $log -ErrorAction SilentlyContinue
		$cmd = '-window -autotest -at_menus -at_pilot AutotestPilot'
		if ($Shots) {
			$shotDir = Join-Path $Shots "$s\$e"
			New-Item -ItemType Directory -Force $shotDir | Out-Null
			$cmd += " -at_shots `"$shotDir`""
		}
		$saved = @{}
		foreach ($k in 'FS2_DLL', 'FS2_ROOT', 'FS2_FIBER', 'FS2_GR_HOST', 'FS2_CMD') { $saved[$k] = [Environment]::GetEnvironmentVariable($k) }
		$env:FS2_DLL = $dll; $env:FS2_ROOT = $dir; $env:FS2_FIBER = '1'; $env:FS2_GR_HOST = '1'; $env:FS2_CMD = $cmd
		$watching = $true
		try { [FS2TourForeground]::Start((Join-Path $Out "${s}_$e.foreground.txt")) } catch { $watching = $false }
		$code = 'NO FOREGROUND OBSERVER'
		if ($watching) {
			$p = Start-Process $Godot -ArgumentList "--path `"$(Join-Path $repo 'godot')`" --windowed --resolution 800x450" -WorkingDirectory $dir -PassThru -RedirectStandardOutput "$Out\${s}_$e.out.txt" -RedirectStandardError "$Out\${s}_$e.err.txt"
			$null = $p.Handle
			$done = $p.WaitForExit(900000)
			if (-not $done) { Stop-Process $p -Force }
			$code = if ($done) { $p.ExitCode } else { 'TIMEOUT' }
			$fgCount = [FS2TourForeground]::Stop()
			if ([FS2TourForeground]::Failed) { $watching = $false }
			Say "$s $e foreground changes: $fgCount$(if (-not $watching) { ' (OBSERVER FAILED, count unproven)' })"
		}
		foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }

		$tour = 'NO LOG'; $bad = @()
		if (Test-Path $log) {
			Copy-Item $log (Join-Path $Out "${s}_$e.autotest.log")
			$logEnd = (Get-Item $log).LastWriteTime
			$endTime = 0.0
			foreach ($l in Get-Content $log) { if ($l -match '^\[\s*([\d.]+)\]') { $endTime = [double]$Matches[1] } }
			foreach ($m in (Select-String $log -Pattern "Screen '[^']+': (PASS|FAIL|SKIP).*")) {
				$v = $m.Matches[0].Value
				Say "$s $e $v"
				if ($v -match "': FAIL") {
					$bad += $v
					# the foreground changes in the 4 s up to this stop's shot; with no shot, the time from the log's own end
					$shot = if ($Shots -and ($v -match "Screen '([^']+)'")) { Join-Path $shotDir ($Matches[1].Replace('/', '\') + '.bmp') }
					if ($shot -and (Test-Path $shot)) {
						$at = (Get-Item $shot).LastWriteTime
					} else {
						$t = if ($m.Line -match '^\[\s*([\d.]+)\]') { [double]$Matches[1] } else { $endTime }
						$at = $logEnd.AddSeconds($t - $endTime)
					}
					$near = if ($watching) { @([FS2TourForeground]::Between($at.AddSeconds(-4), $at)) } else { @('foreground not known: observer failed') }
					if (-not $near) { $near = @('no foreground change') }
					foreach ($n in $near) { Say "$s $e   4 s before $($at.ToString('HH:mm:ss.fff')): $n" }
				}
			}
			$m = Select-String $log -Pattern 'Menu tour: \d+ screens, .*' | Select-Object -Last 1
			$tour = if ($m) { $m.Matches[0].Value } else { 'tour did not finish' }
		}

		foreach ($n in 'fs2.ini', 'autotest.log') {
			$b = Join-Path $bak $n
			if (Test-Path $b) { Copy-Item $b (Join-Path $dir $n) -Force } else { Remove-Item (Join-Path $dir $n) -ErrorAction SilentlyContinue }
		}
		Copy-Item "$bak\players\*" $pdir -Recurse -Force -ErrorAction SilentlyContinue
		$pdirFull = if (Test-Path $pdir) { (Resolve-Path $pdir).Path } else { $pdir }
		foreach ($f in Get-ChildItem $pdirFull -Recurse -File -ErrorAction SilentlyContinue) { if (-not (Test-Path (Join-Path "$bak\players" $f.FullName.Substring($pdirFull.Length).TrimStart('\')))) { Remove-Item $f.FullName } }
		$pilots = (($before -join "`n") -eq ((Pilots $pdir) -join "`n"))

		$ok = ($code -eq 0) -and $watching -and ($tour -match 'Menu tour: \d+ screens') -and ($tour -notmatch '\bfailed [1-9]') -and (-not $bad) -and $pilots
		$line = "$s $e exit=$code $tour" + $(if (-not $pilots) { ' PILOTS CHANGED' })
		if ($ok) { $pass++ } else { $fail++; $line = "FAIL $line" }
		Say $line
		Remove-Item $bak -Recurse -Force
	}
}
Say "DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
