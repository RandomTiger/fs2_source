# Shared by l1_gate.ps1 (native) and godot_check.ps1 -Level L1 (Godot headless): the L1 smoke set (GATING_PLAN_v2,
# D-20261027-102606). Dot-source it. No game is started here.
#
#   Get-L1Plan          reads l1_set.txt and picks the random missions from the pool with a logged seed
#   Get-L1Divergence    TraceDiff of a recorded and a replayed trace: the first diverging frame and the lines after it
#   Invoke-GodotL1      the Godot side of L1 (called from godot_check.ps1 -Level L1)

$script:L1Folder = @{ fs2 = 'autotest'; fs1 = 'fs1'; fs2demo = 'demo' }	# set -> recordings\<folder>
$script:L1Fs1DemoMissions = @('btmdemo', 'demo01', 'demo02a', 'demo02b')

function Get-L1Names([string]$Repo, [string]$Set) {
	@(Get-ChildItem (Join-Path $Repo "recordings\$($script:L1Folder[$Set])") -Filter *.fs2rec -ErrorAction SilentlyContinue |
		ForEach-Object { $_.BaseName } | Sort-Object { $_ })
}

# Returns Seed, Features, Fs2, Fs1, Fs2Demo, Fs1Demo (fixed lists), Random (objects with Set and Name) and Sets (name -> set).
# -Only: run just these missions (name, or set/name when the name is in two sets); no features, no fixed list, no random.
function Get-L1Plan([string]$Repo, [string]$SetFile, [int]$Seed, [string[]]$Only, [int]$RandomCount = 3) {
	if (-not $Seed) { $Seed = (Get-Random -Minimum 1 -Maximum 2147483647) }
	$p = @{ Seed = $Seed; Features = @(); Fs2 = @(); Fs1 = @(); Fs2Demo = @(); Fs1Demo = @(); Random = @(); Only = $false }
	$names = @{}
	foreach ($s in 'fs2', 'fs1', 'fs2demo') { $names[$s] = Get-L1Names $Repo $s }
	if ($Only) {
		$p.Only = $true
		foreach ($o in @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })) {
			$set = $null; $n = $o
			if ($o -match '^(fs2|fs1|fs2demo|fs1demo)/(.+)$') { $set = $Matches[1]; $n = $Matches[2] }
			$hits = @()
			foreach ($s in 'fs2', 'fs1', 'fs2demo') { if ((-not $set -or $set -eq $s) -and ($names[$s] -contains $n)) { $hits += $s } }
			if ((-not $set -or $set -eq 'fs1demo') -and ($script:L1Fs1DemoMissions -contains $n)) { $hits += 'fs1demo' }
			if ($hits.Count -ne 1) { throw "-Only '$o': $(if ($hits.Count) { 'in several sets (' + ($hits -join ', ') + '), write set/name' } else { 'no such recording or demo mission' })" }
			switch ($hits[0]) { 'fs2' { $p.Fs2 += $n } 'fs1' { $p.Fs1 += $n } 'fs2demo' { $p.Fs2Demo += $n } 'fs1demo' { $p.Fs1Demo += $n } }
		}
		return $p
	}
	foreach ($line in Get-Content $SetFile) {
		$t = ($line -replace '#.*', '').Trim()
		if (-not $t) { continue }
		$f = $t -split '\s+'
		if ($f.Count -ne 2) { throw "l1_set.txt: bad line '$line'" }
		switch ($f[0]) {
			'features' { $p.Features += @($names['fs2'] | Where-Object { $_ -like $f[1] }) }
			'fs2' { $p.Fs2 += $f[1] }
			'fs1' { $p.Fs1 += $f[1] }
			'fs2demo' { $p.Fs2Demo += $f[1] }
			'fs1demo' { $p.Fs1Demo += $f[1] }
			default { throw "l1_set.txt: unknown kind '$($f[0])'" }
		}
	}
	foreach ($s in 'fs2', 'fs1', 'fs2demo') {
		foreach ($n in $p[@{ fs2 = 'Fs2'; fs1 = 'Fs1'; fs2demo = 'Fs2Demo' }[$s]]) {
			if ($names[$s] -notcontains $n) { throw "l1_set.txt: no recording '$n' in recordings\$($script:L1Folder[$s])" }
		}
	}
	foreach ($n in $p.Fs1Demo) { if ($script:L1Fs1DemoMissions -notcontains $n) { throw "l1_set.txt: unknown FS1 demo mission '$n'" } }
	# the pool: every recording not already in the run; a name that exists in two sets is left out (replay_check matches by name only)
	$used = @($p.Features + $p.Fs2 + $p.Fs1 + $p.Fs2Demo)
	$count = @{}
	foreach ($s in 'fs2', 'fs1', 'fs2demo') { foreach ($n in $names[$s]) { $count[$n] = 1 + [int]$count[$n] } }
	$pool = @()
	foreach ($s in 'fs2', 'fs1', 'fs2demo') {
		foreach ($n in $names[$s]) { if (($used -notcontains $n) -and ($count[$n] -eq 1)) { $pool += [pscustomobject]@{ Set = $s; Name = $n } } }
	}
	$pool = @($pool | Sort-Object Set, Name)
	$rng = New-Object System.Random $Seed
	$pick = @()
	while (($pick.Count -lt $RandomCount) -and $pool.Count) {
		$i = $rng.Next($pool.Count)
		$pick += $pool[$i]
		$pool = @($pool | Where-Object { $_ -ne $pool[$i] })
	}
	$p.Random = $pick
	foreach ($r in $pick) {
		switch ($r.Set) { 'fs2' { $p.Fs2 += $r.Name } 'fs1' { $p.Fs1 += $r.Name } 'fs2demo' { $p.Fs2Demo += $r.Name } }
	}
	$p.PoolSize = $pool.Count + $pick.Count
	return $p
}

function Show-L1Plan($p) {
	if ($p.Only) { "only: fs2 [$($p.Fs2 -join ', ')]  fs1 [$($p.Fs1 -join ', ')]  fs2demo [$($p.Fs2Demo -join ', ')]  fs1demo [$($p.Fs1Demo -join ', ')]"; return }
	$rnd = @($p.Random | ForEach-Object { "$($_.Set)/$($_.Name)" })
	"seed $($p.Seed): random picks (from a pool of $($p.PoolSize)): $($rnd -join ', ')"
	"  rerun with the same picks: -Seed $($p.Seed)"
	"feature tests ($($p.Features.Count)): $($p.Features -join ', ')"
	"fixed + random FS2 retail: $(@($p.Fs2 | Where-Object { $p.Features -notcontains $_ }) -join ', ')"
	"fixed + random FS1 retail: $($p.Fs1 -join ', ')"
	"FS2 demo: $($p.Fs2Demo -join ', ')    FS1 demo (clean-run rule): $($p.Fs1Demo -join ', ')"
}

# First diverging frame between a recorded and a replayed trace: the DIVERGED/OUT OF TOLERANCE line plus up to 8 lines after it.
function Get-L1Divergence([string]$TraceDiff, [string]$Recorded, [string]$Replay) {
	if (-not (Test-Path $Replay)) { return [pscustomobject]@{ Frame = ''; Text = "no replay trace written ($Replay)" } }
	if (-not (Test-Path $Recorded)) { return [pscustomobject]@{ Frame = ''; Text = "no baseline trace ($Recorded)" } }
	$o = @(& $TraceDiff $Recorded $Replay 2>&1 | ForEach-Object { "$_" })
	for ($i = 0; $i -lt $o.Count; $i++) {
		if ($o[$i] -match '^(DIVERGED|OUT OF TOLERANCE)') {
			$frame = if ($o[$i] -match 'frame (\d+)') { $Matches[1] } else { 'length differs' }
			return [pscustomobject]@{ Frame = $frame; Text = (($o[$i..([Math]::Min($i + 8, $o.Count - 1))]) -join "`n      ") }
		}
	}
	return [pscustomobject]@{ Frame = ''; Text = "TraceDiff: $(($o | Select-Object -Last 2) -join ' / ')" }
}

# replay_check's summary.txt: one record per failing run. Returns objects with Name, Folder, Config, Why.
function Read-ReplayFailures([string]$Summary) {
	$out = @()
	if (-not (Test-Path $Summary)) { return $out }
	foreach ($l in Get-Content $Summary) {
		if ($l -match '^SKIP ') { $out += [pscustomobject]@{ Name = ($l -split '\s+')[1]; Folder = ''; Config = '-'; Why = "$($l.Substring(0, [Math]::Min(160, $l.Length))) (a skip is not green)" }; continue }
		if ($l -notmatch '^FAIL ') { continue }
		$seg = $l.Substring(5) -split ' \| '
		$head = $seg[0]; $folder = 'autotest'; $name = $head
		if ($head -match '^(\S+) \((\w+)\)$') { $name = $Matches[1]; $folder = $Matches[2] }
		$found = $false
		foreach ($s in $seg[1..($seg.Count - 1)]) {
			if ($s -match '^(r|d) ') {
				$c = $Matches[1]
				$good = ($s -match '^(r|d) exit=0 ') -and ($s -match 'REPLAY OK') -and ($s -match ' / MATCH') -and ($s -notmatch 'NOT HEADLESS|GUARDS NOT ASSERTING|CRASH DUMP')
				if (-not $good) { $found = $true; $out += [pscustomobject]@{ Name = $name; Folder = $folder; Config = $c; Why = $s.Substring(0, [Math]::Min(220, $s.Length)) } }
			} elseif ($s -match 'DIFFER') { $found = $true; $out += [pscustomobject]@{ Name = $name; Folder = $folder; Config = 'r vs d'; Why = $s } }
		}
		if (-not $found) { $out += [pscustomobject]@{ Name = $name; Folder = $folder; Config = '?'; Why = $l.Substring(0, [Math]::Min(220, $l.Length)) } }
	}
	return $out
}

# --- Godot side -------------------------------------------------------------------------------------------------
# Same set through godot_check.ps1 (stock godot --headless + fs2godot + fs2game64 DLL), Release and Debug, one call per
# set and config. Prerequisite is the L0 build as for godot_check itself (x64 exes, DLLs, fs2godot.dll, demo x64 build);
# nothing is built here. The menu tours are native-only (no Godot tour exists), so they run in l1_gate.ps1.
function Invoke-GodotL1([string]$Repo, [string]$GodotCheck, [string]$SetFile, [int]$Seed, [string[]]$Only, [string]$Out) {
	$start = Get-Date
	$plan = Get-L1Plan $Repo $SetFile $Seed $Only
	if (-not $Out) { $Out = Join-Path $Repo ('replays\godot_L1_' + $start.ToString('yyyyMMdd_HHmm')) }
	New-Item -ItemType Directory -Force $Out | Out-Null
	$Out = (Resolve-Path $Out).Path
	$td = Join-Path $Repo 'tools\bin\TraceDiff.exe'
	"L1 (Godot headless) start $($start.ToString('HH:mm')), repo $Repo, $(git -C $Repo log --oneline -1)"
	Show-L1Plan $plan
	"tours: not run here (native menu tour only; l1_gate.ps1 runs them)"
	$groups = @(
		@{ Set = 'autotest'; Rec = 'autotest'; Names = @($plan.Fs2) },
		@{ Set = 'fs1'; Rec = 'fs1'; Names = @($plan.Fs1) },
		@{ Set = 'demo'; Rec = 'demo'; Names = @($plan.Fs2Demo) },
		@{ Set = 'fs1demo'; Rec = $null; Names = @($plan.Fs1Demo) }
	)
	$fails = @(); $lines = @(); $first = $true
	foreach ($c in 'r', 'd') {
		$first = $true
		foreach ($g in $groups) {
			if (-not $g.Names.Count) { continue }
			$dir = Join-Path $Out "$c\$($g.Set)"
			$a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $GodotCheck, '-Config', $c, '-Set', $g.Set, '-Missions', ($g.Names -join ','), '-Out', $dir)
			if (-not $first) { $a += '-NoHostChecks' }	# restart and forced-Error checks once per config, with the first set
			$first = $false
			$t0 = Get-Date
			$o = @(& powershell @a 2>&1 | ForEach-Object { "$_" })
			$code = $LASTEXITCODE
			$lines += "godot $c $($g.Set): $($g.Names.Count) missions, $([int]((Get-Date) - $t0).TotalMinutes) min, $($o | Select-Object -Last 1)"
			$sum = Join-Path $dir 'summary.txt'
			$bad = @(if (Test-Path $sum) { Get-Content $sum | Where-Object { $_ -match '^FAIL ' } })
			foreach ($b in $bad) {
				$m = ($b.Substring(5) -split ' \| ')[0]
				$f = [pscustomobject]@{ Mission = $m; Set = $g.Set; Config = "$c (godot)"; Frame = ''; Text = $b.Substring(0, [Math]::Min(300, $b.Length)) }
				if ($g.Rec -and ($b -match 'DIVERGED|NO TRACE|NO VERDICT')) {
					$base = Join-Path $Repo "recordings\$($g.Rec)\$m.trace"
					$d = Get-L1Divergence $td $base (Join-Path $dir "${m}_godot.trace")
					$f.Frame = $d.Frame; $f.Text += "`n      " + $d.Text
				}
				$fails += $f
			}
			if (($code -ne 0) -and -not $bad) {
				$fails += [pscustomobject]@{ Mission = '(whole call)'; Set = $g.Set; Config = "$c (godot)"; Frame = ''; Text = "godot_check exit ${code}:$(($o | Select-Object -Last 3) -join ' / ')" }
			}
			if (($code -eq 0) -and (Test-Path $sum) -and -not (Select-String $sum -Pattern '^DONE' -Quiet)) {
				$fails += [pscustomobject]@{ Mission = '(whole call)'; Set = $g.Set; Config = "$c (godot)"; Frame = ''; Text = 'no DONE line in summary' }
			}
		}
	}
	$end = Get-Date
	$res = @("L1 (Godot headless) summary, start $($start.ToString('HH:mm')), end $($end.ToString('HH:mm')), seed $($plan.Seed)") + $lines
	if ($fails) {
		$res += "L1 FAIL ($($fails.Count) failures) - close gates on this branch, walk back with -Only <mission>"
		foreach ($f in $fails) { $res += "  FAIL mission $($f.Mission) | set $($f.Set) | config $($f.Config) | first diverging frame $(if ($f.Frame) { $f.Frame } else { 'n/a' })`n      $($f.Text)" }
	} else { $res += 'L1 green (Godot headless)' }
	$res | Set-Content (Join-Path $Out 'l1_summary.txt')
	Add-Content (Join-Path $(if ($env:FS2_GATE_OUT) { $env:FS2_GATE_OUT } else { Join-Path $env:TEMP 'fs2_gates' }) 'l1_runs.log') "$($start.ToString('yyyy-MM-dd HH:mm')) godot $Repo seed $($plan.Seed) random $(@($plan.Random | ForEach-Object { "$($_.Set)/$($_.Name)" }) -join ',') -> $(if ($fails) { 'FAIL' } else { 'green' })"
	$res
	if ($fails) { exit 1 }
	exit 0
}
