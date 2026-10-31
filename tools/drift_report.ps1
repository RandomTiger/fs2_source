<#
Drift report: where and how two runs of the same mission drift apart (#96, D-20261031-065902).

DIAGNOSTIC, NOT A GATE RESULT. Strict replay (REPLAY OK) and TraceDiff MATCH (exit 0) stay the only pass condition
everywhere. This report is never quoted as evidence that a mission passes. It reads existing recordings and traces
only, runs no game and changes no file it reads.

Usage (PowerShell, from anywhere):
  tools\drift_report.ps1 -A <run A> -B <run B> [-Out <report.txt>]
  tools\drift_report.ps1 -SelfTest <trace> [-Out <dir>]

A run is a recording (NAME.fs2rec, its trace NAME.trace beside it) or a trace on its own. With two recordings the
report also compares their recorded streams. Paths may be outside the repo, for example files taken with git show from
another layer's commit.

The report has three parts (docs/REPLAY.md, "Drift report"):
  1. Strict verdict: TraceDiff A B, quoted unchanged with its exit code.
  2. Stream: entries per tag in each recording, the stated ignore list taken out, and the first entry where the
     remaining tag sequences differ.
  3. Trace drift: TraceDiff -curve over the frames both traces wrote in full; per kind the first frame that drifts,
     the largest drift and the drift at the last common full frame.

-SelfTest checks the tool: the trace against itself must show zero drift, and a copy with one injected drift
(pos.x of the first object +0.5 m at one full frame) must be reported at that frame and kind.
#>
param(
	[string]$A,
	[string]$B,
	[string]$Out,
	[string]$SelfTest
)

$repo = Split-Path $PSScriptRoot
$td = Join-Path $repo 'tools\bin\TraceDiff.exe'
if (-not (Test-Path $td)) { throw "TraceDiff not found: $td (build it first)" }

# ---------------------------------------------------------------------------------------------------------------
# The ignore list (D-20261031-065902 term 3). Each item is noise by a logged decision; nothing else is ignored.
#   RT_SND_* (18-24)        sound-device answers: whether a channel was free, is playing, time remaining, pitch,
#                           channels playing. D-035 item 2: device-dependent, no logic reads them; left the stream in
#                           recording format 4.
#   RT_STREAM_* (25-27, 30) music-stream answers: is playing, is paused, done reading, bytes committed. D-035 item 2,
#                           same reason.
#   flip cursor             an RT_MOUSE_POS read straight after another RT_MOUSE_POS (ignored entries between do
#                           not count): gr_flip's cursor read after mouse_eval_deltas' own. D-035 item 3: it only
#                           draws the cursor; left the stream in format 4.
# Not ignored, and named in the report instead: the clock read that times a music or voice stream starting
# (D-035 status (a)) is an RT_TIMER_MS entry like any other and cannot be told apart in the stream.
# ---------------------------------------------------------------------------------------------------------------

$tagNames = @('?', 'TIMER_FIX', 'TIMER_MS', 'TIMER_US', 'OSPOLL', 'FOREGROUND', 'EV_KEY', 'EV_MOUSE', 'EV_LOSTFOCUS',
	'EV_GOTFOCUS', 'MOUSE_POS', 'JOY_DOWN', 'JOY_DOWN_COUNT', 'JOY_UP_COUNT', 'JOY_DOWN_TIME', 'JOY_RAW_AXIS', 'JOY_POS',
	'AXES', 'SND_PLAY', 'SND_PLAY_3D', 'SND_PLAY_LOOPING', 'SND_IS_PLAYING', 'SND_TIME_REMAINING', 'SND_PITCH',
	'SND_NUM_PLAYING', 'STREAM_IS_PLAYING', 'STREAM_IS_PAUSED', 'STREAM_DONE_READING', 'ENTER_GAME', 'END',
	'STREAM_BYTES', 'INPUT_DEVICE', 'JOY_PRESENT')

if (-not ('Fs2DriftStream' -as [type])) {
	Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
public class Fs2DriftStream {
	public int Version; public string Build = ""; public string Mission = ""; public string Error = "";
	public List<byte> Tags = new List<byte>();
	public List<string> Vals = new List<string>();
	public bool Ignored(int i) {
		int t = Tags[i];
		if (t >= 18 && t <= 27) return true;
		if (t == 30) return true;
		if (t == 10) {
			for (int j = i - 1; j >= 0; j--) {
				int u = Tags[j];
				if ((u >= 18 && u <= 27) || u == 30) continue;
				return u == 10 && !FlipLike(j);
			}
		}
		return false;
	}
	// the earlier read of a pair is mouse_eval_deltas' own, never the flip's
	bool FlipLike(int j) {
		if (j == 0) return false;
		for (int k = j - 1; k >= 0; k--) {
			int u = Tags[k];
			if ((u >= 18 && u <= 27) || u == 30) continue;
			return u == 10 && !FlipLike(k);
		}
		return false;
	}
	static string Str(byte[] d, int off, int len) {
		int n = 0; while (n < len && d[off + n] != 0) n++;
		return System.Text.Encoding.GetEncoding(28591).GetString(d, off, n);
	}
	public static Fs2DriftStream Load(string path) {
		var s = new Fs2DriftStream();
		byte[] d = File.ReadAllBytes(path);
		if (d.Length < 804 || Str(d, 0, 6) != "FS2REC") { s.Error = "not a recording"; return s; }
		s.Version = BitConverter.ToInt32(d, 8);
		s.Build = Str(d, 12, 64); s.Mission = Str(d, 76, 64);
		int hdr = s.Version == 1 ? 804 : s.Version == 2 ? 816 : 828;
		int pos = hdr + BitConverter.ToInt32(d, 772) + BitConverter.ToInt32(d, 796) + BitConverter.ToInt32(d, 800);
		try {
			while (pos < d.Length) {
				int tag = d[pos++];
				string v = "";
				switch (tag) {
					case 1: case 2: case 3: case 5: case 11: case 12: case 13:
					case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 30: case 32:
						v = Var(d, ref pos).ToString(); break;
					case 14: v = BitConverter.ToSingle(d, pos).ToString("R"); pos += 4; break;
					case 10: case 15: case 16: case 17: case 31: {
						int n = Var(d, ref pos); var a = new string[n];
						for (int i = 0; i < n; i++) a[i] = Var(d, ref pos).ToString();
						v = string.Join(" ", a); break; }
					case 6: v = Var(d, ref pos) + " " + Var(d, ref pos) + " " + Var(d, ref pos); break;
					case 7: v = Var(d, ref pos) + " " + Var(d, ref pos); break;
					case 4: case 8: case 9: case 28: case 29: break;
					default: s.Error = "unknown tag " + tag + " at byte " + (pos - 1); return s;
				}
				s.Tags.Add((byte)tag); s.Vals.Add(v);
			}
		} catch (IndexOutOfRangeException) { s.Error = "recording ends inside an entry"; }
		catch (ArgumentException) { s.Error = "recording ends inside an entry"; }
		if (s.Error == "" && (s.Tags.Count == 0 || s.Tags[s.Tags.Count - 1] != 29)) s.Error = "stream does not end with RT_END";
		return s;
	}
	static int Var(byte[] d, ref int pos) {
		uint u = 0; int shift = 0; byte b;
		do { b = d[pos++]; u |= (uint)(b & 0x7f) << shift; shift += 7; } while ((b & 0x80) != 0);
		return (int)(u >> 1) ^ -(int)(u & 1);
	}
}
'@
}

function Get-Run([string]$path) {
	$p = (Resolve-Path $path).Path
	if ($p -match '\.fs2rec$') { return @{ Rec = $p; Trace = ($p -replace '\.fs2rec$', '.trace') } }
	return @{ Rec = $null; Trace = $p }
}

function Stream-Section($ra, $rb) {
	$o = @()
	$sa = [Fs2DriftStream]::Load($ra); $sb = [Fs2DriftStream]::Load($rb)
	foreach ($x in @(@('A', $sa), @('B', $sb))) {
		$s = $x[1]
		if ($s.Error) { return @("  $($x[0]): cannot read the stream: $($s.Error)") }
		$o += "  $($x[0]): format $($s.Version), build '$($s.Build)', mission '$($s.Mission)', $($s.Tags.Count) entries"
	}
	$fa = New-Object 'System.Collections.Generic.List[int]'; $fb = New-Object 'System.Collections.Generic.List[int]'
	$ca = New-Object int[] 33; $cb = New-Object int[] 33; $ia = New-Object int[] 33; $ib = New-Object int[] 33
	for ($i = 0; $i -lt $sa.Tags.Count; $i++) { $t = $sa.Tags[$i]; $ca[$t]++; if ($sa.Ignored($i)) { $ia[$t]++ } else { $fa.Add($i) } }
	for ($i = 0; $i -lt $sb.Tags.Count; $i++) { $t = $sb.Tags[$i]; $cb[$t]++; if ($sb.Ignored($i)) { $ib[$t]++ } else { $fb.Add($i) } }
	$o += ''
	$o += '  entries per tag             A        B   (ignored A / B)   class'
	for ($t = 1; $t -lt 33; $t++) {
		if (-not ($ca[$t] -or $cb[$t])) { continue }
		$class = if (($t -ge 18 -and $t -le 24)) { 'sound-device answer, ignored (D-035 item 2)' }
			elseif (($t -ge 25 -and $t -le 27) -or $t -eq 30) { 'music-stream answer, ignored (D-035 item 2)' }
			elseif ($t -eq 10) { 'flip cursor pairs ignored (D-035 item 3)' }
			elseif ($t -eq 2) { 'includes stream-start clock reads (D-035 (a)), not ignored' }
			elseif ($t -eq 31 -or $t -eq 32) { 'input-device answers, not ignored' }
			else { '' }
		$ign = if ($ia[$t] -or $ib[$t]) { '({0} / {1})' -f $ia[$t], $ib[$t] } else { '' }
		$o += '  {0,-22} {1,8} {2,8}   {3,-17} {4}' -f $tagNames[$t], $ca[$t], $cb[$t], $ign, $class
	}
	# the classes whose entry counts differ between the runs, A minus B
	$classes = [ordered]@{ 'sound-device answers (ignored)' = @(18..24); 'music-stream answers (ignored)' = @(25, 26, 27, 30);
		'RT_TIMER_MS, includes stream-start clock reads (not ignored)' = @(2); 'input-device answers (not ignored)' = @(31, 32) }
	$o += ''
	$o += '  classes with different entry counts (A minus B):'
	$named = @()
	foreach ($c in $classes.Keys) {
		$dd = 0; foreach ($t in $classes[$c]) { $dd += $ca[$t] - $cb[$t]; $named += $t }
		if ($dd) { $o += "    $c`: $dd" }
	}
	$fl = $ia[10] - $ib[10]
	if ($fl) { $o += "    flip cursor reads (ignored): $fl" }
	$dd = 0; for ($t = 1; $t -lt 33; $t++) { if ($named -notcontains $t) { $dd += [Math]::Abs(($ca[$t] - $ia[$t]) - ($cb[$t] - $ib[$t])) } }
	if ($dd) { $o += "    other kept entries (frames run, polls, keys, joystick): $dd in all, by tag in the table" }

	$n = [Math]::Min($fa.Count, $fb.Count); $k = 0
	while ($k -lt $n -and $sa.Tags[$fa[$k]] -eq $sb.Tags[$fb[$k]]) { $k++ }
	$o += ''
	$o += "  after the ignore list: A $($fa.Count) entries, B $($fb.Count)"
	if ($k -eq $n -and $fa.Count -eq $fb.Count) {
		$o += '  tag sequences identical after the ignore list'
	} else {
		$pa = 0; for ($i = 0; $i -lt $k; $i++) { if ($sa.Tags[$fa[$i]] -eq 4) { $pa++ } }
		$o += "  tag sequences first differ at kept entry $k (os_poll $pa); A entry $(if ($k -lt $fa.Count) { $fa[$k] } else { 'end' }), B entry $(if ($k -lt $fb.Count) { $fb[$k] } else { 'end' })"
		$o += '  kept entries around it (tag value):'
		for ($i = [Math]::Max(0, $k - 4); $i -le [Math]::Min($n - 1, $k + 3); $i++) {
			$x = $sa.Tags[$fa[$i]]; $y = $sb.Tags[$fb[$i]]
			$o += '    {0}{1,8}  A {2,-20} {3,-14}  B {4,-20} {5}' -f $(if ($i -eq $k) { '>' } else { ' ' }), $i, $tagNames[$x], $sa.Vals[$fa[$i]], $tagNames[$y], $sb.Vals[$fb[$i]]
		}
	}
	# values of the kept entries that are inputs (keys, mouse buttons, focus) up to the first tag difference
	$diffv = -1
	for ($i = 0; $i -lt $k; $i++) {
		$t = $sa.Tags[$fa[$i]]
		if (($t -ge 6 -and $t -le 9) -and ($sa.Vals[$fa[$i]] -ne $sb.Vals[$fb[$i]])) { $diffv = $i; break }
	}
	if ($diffv -ge 0) { $o += "  first input event with different values before that: kept entry $diffv" }
	return $o
}

function Trace-Section($ta, $tb) {
	$o = @()
	$strict = & $td $ta $tb 2>&1
	$code = $LASTEXITCODE
	$first = 'none'
	$m = $strict | Select-String 'DIVERGED at frame (\d+) \(mission time ([\d.]+) s\): (.*)' | Select-Object -First 1
	if ($m) { $first = "frame $($m.Matches[0].Groups[1].Value) (mission time $($m.Matches[0].Groups[2].Value) s), $($m.Matches[0].Groups[3].Value)" }
	elseif ($strict | Select-String 'different lengths') { $first = ($strict | Select-String 'different lengths').Line.Trim() }
	$o += "  First diverging frame: $first"
	$o += ''
	$o += "  1. Strict verdict (TraceDiff A B, unchanged, exit $code):"
	$o += $strict | ForEach-Object { "    | $_" }
	if ($code -ge 2) {
		# exit 2: TraceDiff could not compare the traces (for example a trace version this layer does not read)
		$o[0] = '  First diverging frame: unknown, the traces are not comparable on this layer (TraceDiff exit 2)'
		$o += ''
		$o += '  3. Trace drift: not measured, the traces are not comparable on this layer (TraceDiff exit 2)'
		return $o
	}

	$curve = & $td $ta $tb -curve 2>&1 | Where-Object { $_ -match '^\d+,' }
	$kinds = @('pos', 'orient', 'vel', 'rotvel', 'health', 'energy', 'control', 'hard')
	$units = @('m', '', 'm/s', 'rad/s', '', '', '', 'fields')
	$firstF = @{}; $max = @{}; $maxF = @{}; $last = @{}
	foreach ($k in $kinds) { $max[$k] = 0.0; $last[$k] = 0.0 }
	$lastFrame = $null
	foreach ($line in $curve) {
		$v = $line.Split(',')
		$lastFrame = "$($v[0]) ($($v[1]) s)"
		for ($i = 0; $i -lt 8; $i++) {
			$k = $kinds[$i]; $x = [double]$v[$i + 2]
			$last[$k] = $x
			if ($x -ne 0 -and -not $firstF.ContainsKey($k)) { $firstF[$k] = "$($v[0]) ($($v[1]) s)" }
			if ([Math]::Abs($x) -gt $max[$k]) { $max[$k] = [Math]::Abs($x); $maxF[$k] = $v[0] }
		}
	}
	$o += ''
	$o += "  3. Trace drift over $(@($curve).Count) full frames both traces wrote (aligned by frame number; last common full frame $lastFrame):"
	$o += '    kind      first drift at          largest (frame)            at last common frame'
	$any = $false
	for ($i = 0; $i -lt 8; $i++) {
		$k = $kinds[$i]
		if ($firstF.ContainsKey($k)) {
			$any = $true
			$o += '    {0,-9} {1,-23} {2,-26} {3}' -f $k, $firstF[$k], ('{0:G4} {1} ({2})' -f $max[$k], $units[$i], $maxF[$k]), ('{0:G4}' -f $last[$k])
		} else {
			$o += '    {0,-9} none' -f $k
		}
	}
	$o += if ($any) { '  Drift: yes' } else { '  Drift: zero on every kind' }
	return $o
}

function Report($pa, $pb) {
	$ra = Get-Run $pa; $rb = Get-Run $pb
	$o = @('DRIFT REPORT -- diagnostic, not a gate result. Never quote it as a pass; strict replay and TraceDiff 0 are the only pass.')
	$o += "  Run A: $(if ($ra.Rec) { $ra.Rec } else { $ra.Trace })"
	$o += "  Run B: $(if ($rb.Rec) { $rb.Rec } else { $rb.Trace })"
	$o += Trace-Section $ra.Trace $rb.Trace
	if ($ra.Rec -and $rb.Rec) {
		$o += ''
		$o += '  2. Stream (ignore list: sound-device answers, music-stream answers, flip cursor; tools\drift_report.ps1):'
		$o += Stream-Section $ra.Rec $rb.Rec
	} else {
		$o += ''
		$o += '  2. Stream: not compared (needs two .fs2rec files)'
	}
	return $o
}

if ($SelfTest) {
	if (-not $Out) { $Out = Join-Path $repo ("replays\drift_selftest_" + (Get-Date -Format 'yyyyMMdd_HHmmss')) }
	New-Item -ItemType Directory -Force $Out | Out-Null
	$src = (Resolve-Path $SelfTest).Path
	$d = [IO.File]::ReadAllBytes($src)
	# the full frames, then the middle one: its first object's pos.x gets +0.5 m and its hash a flipped bit
	$pos = 12; $full = @()
	while ($pos + 32 -le $d.Length) {
		$size = [BitConverter]::ToInt32($d, $pos + 28)
		if ($size) { $full += $pos }
		$pos += 32 + $size
	}
	$f = $full[[int]($full.Count / 2)]
	$p = $f + 32
	$p += 4 + 4 * [BitConverter]::ToInt32($d, $p)		# control_info
	$p += 4 + 4 * [BitConverter]::ToInt32($d, $p)		# button_info
	$p += 4 + 4 * 6 * [BitConverter]::ToInt32($d, $p)	# events, TRE_COUNT values each
	$p += 4 + 4 * 2 * [BitConverter]::ToInt32($d, $p)	# goals, TRG_COUNT values each
	if ([BitConverter]::ToInt32($d, $p) -lt 1) { throw 'the chosen frame has no objects' }
	$p += 4 + 12 + 4										# object count; signature, type, count; flags
	$x = [BitConverter]::ToSingle($d, $p) + 0.5
	[BitConverter]::GetBytes([single]$x).CopyTo($d, $p)
	$d[$f + 20] = $d[$f + 20] -bxor 1
	$inj = Join-Path $Out 'injected.trace'
	[IO.File]::WriteAllBytes($inj, $d)
	$frame = [BitConverter]::ToInt32($d, $f)
	$r1 = Report $src $src
	$r2 = Report $src $inj
	$r1 | Set-Content (Join-Path $Out 'identical.txt'); $r2 | Set-Content (Join-Path $Out 'injected.txt')
	$ok1 = [bool]($r1 -match 'Drift: zero on every kind') -and [bool]($r1 -match 'First diverging frame: none')
	$okPos = $r2 | Where-Object { $_ -match "^\s+pos\s+$frame " }
	$ok2 = [bool]$okPos -and [bool]($r2 -match "First diverging frame: frame $frame ")
	'DRIFT SELFTEST (diagnostic, not a gate result)'
	"  identical pair: $(if ($ok1) { 'zero drift, as expected' } else { 'UNEXPECTED drift' })"
	"  injected +0.5 m pos.x at frame ${frame}: $(if ($ok2) { 'reported at that frame as pos' } else { 'NOT reported as expected' })"
	"  reports: $Out"
	if (-not ($ok1 -and $ok2)) { exit 1 }
	exit 0
}

if (-not ($A -and $B)) { throw 'usage: drift_report.ps1 -A <run> -B <run> [-Out <file>] | -SelfTest <trace>' }
$r = Report $A $B
if ($Out) { $r | Set-Content $Out }
$r
exit 0
