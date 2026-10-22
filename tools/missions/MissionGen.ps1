# Builds autotest missions (.fs2) from short descriptions. Dot-source this from a mission script:
#   . "$PSScriptRoot\MissionGen.ps1"
#   $m = New-Mission 'AutotestAI' 'AI orders' 'notes'
#   Add-Ship $m @{ Name = 'Beta 1'; Class = 'GTF Myrmidon'; ... }
#   Save-Mission $m
# The file is written to data/missions as CRLF Latin-1, the format FRED writes.

$ErrorActionPreference = 'Stop'

function New-Mission([string]$File, [string]$Title, [string]$Notes) {
	@{
		File = $File; Title = $Title; Notes = $Notes
		Nebula = $false; Storm = ''; Support = $false; MissionFlags = 0
		Ships = New-Object Collections.ArrayList
		Wings = New-Object Collections.ArrayList
		Events = New-Object Collections.ArrayList
		Goals = New-Object Collections.ArrayList
		Waypoints = New-Object Collections.ArrayList
		Messages = New-Object Collections.ArrayList
		Asteroids = $null
		PlayerClass = 'GTF Ulysses'
		Variables = New-Object Collections.ArrayList
		JumpNodes = New-Object Collections.ArrayList
	}
}

function Format-Orient([string]$Face) {
	switch ($Face) {
		'back'  { "`t-1.000000, 0.000000, 0.000000,`n`t0.000000, 1.000000, 0.000000,`n`t0.000000, 0.000000, -1.000000" }
		'right' { "`t0.000000, 0.000000, -1.000000,`n`t0.000000, 1.000000, 0.000000,`n`t1.000000, 0.000000, 0.000000" }
		'left'  { "`t0.000000, 0.000000, 1.000000,`n`t0.000000, 1.000000, 0.000000,`n`t-1.000000, 0.000000, 0.000000" }
		default { "`t1.000000, 0.000000, 0.000000,`n`t0.000000, 1.000000, 0.000000,`n`t0.000000, 0.000000, 1.000000" }
	}
}

function Format-Vec($v) { '{0:F6}, {1:F6}, {2:F6}' -f [double]$v[0], [double]$v[1], [double]$v[2] }

function Format-List($items) { '( ' + (($items | % { "`"$_`"" }) -join ' ') + ' )' }

# Ship fields: Name, Class, Team (Friendly), Loc (x,y,z), Face (fwd/back/left/right), Goals (sexp list
# without the outer "goals"), Cargo, Vel, Hull, Shields, Primary, Secondary (weapon name lists),
# Subs (@{ name = damage }), Arr (Hyperspace / Near Ship / In front of ship / Docking Bay), ArrDist,
# ArrAnchor, ArrCue, Dep (Hyperspace / Docking Bay), DepAnchor, DepCue, Flags, Escort, Kamikaze, AIClass,
# Docked (@(ship, docker point, dockee point)), Score, Wing (set by Add-Wing).
function Add-Ship($m, [hashtable]$s) {
	$d = @{ Team = 'Friendly'; Loc = @(0, 0, 0); Face = 'fwd'; Cargo = 'Nothing'; Vel = 0; Hull = 100; Shields = 100
		Arr = 'Hyperspace'; ArrCue = '( true )'; Dep = 'Hyperspace'; DepCue = '( false )'; Flags = @(); Score = 10 }
	foreach ($k in $d.Keys) { if (-not $s.ContainsKey($k)) { $s[$k] = $d[$k] } }
	[void]$m.Ships.Add($s)
	$s
}

# Wing fields: Name, Ships (names, already added with Add-Ship), Waves, Threshold, Arr, ArrDist,
# ArrAnchor, ArrCue, Dep, DepAnchor, DepCue, Flags, Hotkey.
function Add-Wing($m, [hashtable]$w) {
	$d = @{ Waves = 1; Threshold = 0; Arr = 'Hyperspace'; ArrCue = '( true )'; Dep = 'Hyperspace'; DepCue = '( false )'; Flags = @() }
	foreach ($k in $d.Keys) { if (-not $w.ContainsKey($k)) { $w[$k] = $d[$k] } }
	foreach ($n in $w.Ships) { ($m.Ships | ? { $_.Name -eq $n }).ArrCue = '( false )' }
	[void]$m.Wings.Add($w)
}

# Event fields: Name, Formula, Repeat (1), Interval (1), Chained (seconds, or absent), Objective.
function Add-Event($m, [hashtable]$e) { [void]$m.Events.Add($e) }
function Add-Goal($m, [string]$Type, [string]$Name, [string]$Text, [string]$Formula) {
	[void]$m.Goals.Add(@{ Type = $Type; Name = $Name; Text = $Text; Formula = $Formula })
}
function Add-Waypoints($m, [string]$Name, $Points) { [void]$m.Waypoints.Add(@{ Name = $Name; Points = $Points }) }
# Message: from Command (Head-CM1, Terran Command) unless Head is given.
function Add-Message($m, [string]$Name, [string]$Text, [string]$Head = 'Head-CM1', [string]$Persona = 'Terran Command') {
	[void]$m.Messages.Add(@{ Name = $Name; Text = $Text; Head = $Head; Persona = $Persona })
}

function Save-Mission($m) {
	$L = New-Object Collections.Generic.List[string]
	function W([string]$s = '') { foreach ($line in $s -split "`n") { $L.Add($line) } }

	W '#Mission Info'; W
	W '$Version: 0.10'
	W "`$Name:  XSTR(`"$($m.Title)`", -1)"
	W '$Author: Claude'
	W '$Created: 10/14/26 at 12:00:00'
	W '$Modified: 10/14/26 at 12:00:00'
	W '$Notes:'; W $m.Notes.Trim(); W '$End Notes:'; W
	W '$Mission Desc:'; W " XSTR(`"$($m.Title)`", -1)"; W '$end_multi_text'; W
	W '+Game Type Flags: 1'; W
	if ($m.Nebula) {
		W "+Flags: $($m.MissionFlags -bor 4)"; W '+NebAwacs: 3000.000000'; W
		if ($m.Storm) { W "+Storm: $($m.Storm)"; W }
	} else {
		W "+Flags: $($m.MissionFlags)"; W
	}
	W '+Red Alert: 0'; W; W '+Scramble: 0'; W
	W "+Disallow Support: $(if ($m.Support) { 0 } else { 1 })"; W
	W '+Viewer pos: 0.000000, 100.000000, -200.000000'
	W "+Viewer orient:`n$(Format-Orient 'fwd')"; W

	W '#Plot Info'; W
	W '$Tour:  XSTR("Blah", -1)'
	foreach ($k in 'Pre-Briefing Cutscene', 'Pre-Mission Cutscene', 'Next Mission Success', 'Next Mission Partial', 'Next Mission Failure') { W "`$${k}: Blah" }
	W

	if ($m.Variables.Count) {
		W '#Sexp_variables'; W; W '$Variables:'; W '('
		$i = 0
		foreach ($v in $m.Variables) { W "`t`t$i`t`t`"$($v.Name)`"`t`t`"$($v.Value)`"`t`t`"$($v.Type)`""; $i++ }
		W ')'; W
	}

	W '#Command Briefing'; W
	W '#Briefing'; W '$start_briefing'; W '$num_stages: 0'; W '$end_briefing'; W
	W '#Debriefing_info'; W; W '$Num stages: 0'; W

	W '#Players		;! 1 total'; W
	W '$Starting Shipname: Alpha 1'
	W '$Ship Choices: ('; W "`t`"$($m.PlayerClass)`"`t5"; W ')'; W
	W '+Weaponry Pool: ('; W "`t`"Subach HL-7`"`t17"; W "`t`"Harpoon`"`t500"; W ')'; W

	W "#Objects`t`t;! $($m.Ships.Count) total"; W
	$i = 0
	foreach ($s in $m.Ships) {
		W "`$Name: $($s.Name)`t`t;! Object #$i"
		W "`$Class: $($s.Class)"
		W "`$Team: $($s.Team)"
		W "`$Location: $(Format-Vec $s.Loc)"
		W "`$Orientation:`n$(Format-Orient $s.Face)"
		W '$IFF: IFF 1'
		W '$AI Behavior: None'
		if ($s.AIClass) { W "+AI Class: $($s.AIClass)" }
		if ($s.Goals) { W "`$AI Goals: ( goals $($s.Goals) )" }
		W "`$Cargo 1:  XSTR(`"$($s.Cargo)`", -1)"
		if ($s.Vel) { W "+Initial Velocity: $($s.Vel)" }
		W "+Initial Hull: $($s.Hull)"
		W "+Initial Shields: $($s.Shields)"
		W '+Subsystem: Pilot'
		if ($s.Primary) { W "+Primary Banks: $(Format-List $s.Primary)" }
		if ($s.Secondary) { W "+Secondary Banks: $(Format-List $s.Secondary)" }
		if ($s.Subs) { foreach ($k in $s.Subs.Keys) { W "+Subsystem: $k"; W "`$Damage: $($s.Subs[$k])" } }
		W "`$Arrival Location: $($s.Arr)"
		if ($s.Arr -ne 'Hyperspace') { W "+Arrival Distance: $([int]$s.ArrDist)"; W "`$Arrival Anchor: $($s.ArrAnchor)" }
		if ($s.ArrDelay) { W "+Arrival Delay: $($s.ArrDelay)" }
		W "`$Arrival Cue: $($s.ArrCue)"
		W "`$Departure Location: $($s.Dep)"
		if ($s.Dep -ne 'Hyperspace') { W "`$Departure Anchor: $($s.DepAnchor)" }
		if ($s.DepDelay) { W "+Departure Delay: $($s.DepDelay)" }
		W "`$Departure Cue: $($s.DepCue)"
		W '$Determination: 10'
		$flags = @($s.Flags)
		if ($s.Name -eq 'Alpha 1') { $flags = @('player-start') + $flags }
		W "+Flags: $(if ($flags.Count) { Format-List $flags } else { '( )' })"
		if ($s.Escort) { W "+Escort Priority: $($s.Escort)" }
		if ($s.Kamikaze) { W "+Kamikaze Damage: $($s.Kamikaze)" }
		if ($s.Docked) { W "+Docked With: $($s.Docked[0])"; W "`$Docker Point: $($s.Docked[1])"; W "`$Dockee Point: $($s.Docked[2])" }
		W '+Group: 0'
		W "+Score: $($s.Score)"
		W
		$i++
	}

	W "#Wings`t`t;! $($m.Wings.Count) total"; W
	foreach ($w in $m.Wings) {
		W "`$Name: $($w.Name)"
		W "`$Waves: $($w.Waves)"
		W "`$Wave Threshold: $($w.Threshold)"
		W "`$Special Ship: 0`t`t;! $($w.Ships[0])"; W
		W "`$Arrival Location: $($w.Arr)"
		if ($w.Arr -ne 'Hyperspace') { W "+Arrival Distance: $([int]$w.ArrDist)"; W "`$Arrival Anchor: $($w.ArrAnchor)" }
		if ($w.ArrDelay) { W "+Arrival delay: $($w.ArrDelay)" }
		W "`$Arrival Cue: $($w.ArrCue)"
		W "`$Departure Location: $($w.Dep)"
		if ($w.Dep -ne 'Hyperspace') { W "`$Departure Anchor: $($w.DepAnchor)" }
		if ($w.DepDelay) { W "+Departure delay: $($w.DepDelay)" }
		W "`$Departure Cue: $($w.DepCue)"
		W "`$Ships: (`t`t;! $($w.Ships.Count) total"
		foreach ($n in $w.Ships) { W "`t`"$n`"" }
		W ')'
		if ($null -ne $w.Hotkey) { W "+Hotkey: $($w.Hotkey)" }
		W "+Flags:$(if ($w.Flags.Count) { Format-List $w.Flags } else { '( )' })"
		W
	}

	W "#Events`t`t;! $($m.Events.Count) total"; W
	foreach ($e in $m.Events) {
		W "`$Formula: $($e.Formula)"
		W "+Name: $($e.Name)"
		W "+Repeat Count: $(if ($e.Repeat) { $e.Repeat } else { 1 })"
		W "+Interval: $(if ($e.Interval) { $e.Interval } else { 1 })"
		if ($null -ne $e.Chained) { W "+Chained: $($e.Chained)" }
		if ($e.Objective) { W "+Objective:  XSTR(`"$($e.Objective)`", -1)" }
		W
	}

	W "#Goals`t`t;! $($m.Goals.Count) total"; W
	foreach ($g in $m.Goals) {
		W "`$Type: $($g.Type)"
		W "+Name: $($g.Name)"
		W "`$MessageNew:  XSTR(`"$($g.Text)`", -1)"
		W '$end_multi_text'
		W "`$Formula: $($g.Formula)"
		W
	}

	W "#Waypoints`t`t;! $($m.Waypoints.Count) lists total"; W
	foreach ($j in $m.JumpNodes) { W "`$Jump Node: $(Format-Vec $j.Loc)"; W "`$Jump Node Name: $($j.Name)" }
	foreach ($p in $m.Waypoints) {
		W "`$Name: $($p.Name)"
		W "`$List: (`t`t;! $($p.Points.Count) points in list"
		foreach ($pt in $p.Points) { W "`t( $(Format-Vec $pt) )" }
		W ')'; W
	}

	W "#Messages`t`t;! $($m.Messages.Count) total"; W
	foreach ($g in $m.Messages) {
		W "`$Name: $($g.Name)"
		W '$Team: -1'
		W "`$MessageNew:  XSTR(`"$($g.Text)`", -1)"
		W '$end_multi_text'
		if ($g.Persona) { W "+Persona: $($g.Persona)" }
		if ($g.Head) { W "+AVI Name: $($g.Head)" }
		W
	}

	W '#Reinforcements		;! 0 total'; W; W
	W '#Background bitmaps		;! 1 total'; W
	W '$Num stars: 500'
	W '$Ambient light level: 0'
	if ($m.Nebula) { W "+Neb2: $(if ($m.NebBack) { $m.NebBack } else { 'nbackpurp2' })"; W; W '+Neb2Flags: 63'; W }
	W '$Sun: SunWhite'; W '+Angles: 0.000000 0.000000 0.000000'; W '+Scale: 1.000000'
	W '$Starbitmap: neb08'; W '+Angles: 0.000000 0.000000 0.000000'
	W '+ScaleX: 10.000000'; W '+ScaleY: 10.000000'; W '+DivX: 1'; W '+DivY: 1'; W

	W '#Asteroid Fields'; W
	if ($m.Asteroids) {
		$a = $m.Asteroids
		W "`$Density: $($a.Density)"
		W "+Field Type: $($a.Type)"
		W '+Debris Genre: 0'
		W '+Field Debris Type: 0'; W '+Field Debris Type: 1'; W '+Field Debris Type: 2'
		W '$Average Speed: 9.999999'
		W "`$Minimum: $(Format-Vec $a.Min)"
		W "`$Maximum: $(Format-Vec $a.Max)"
		W
	}
	W '#Music'; W; W '$Event Music: None'; W '$Briefing Music: None'; W
	W '#End'

	$out = Join-Path (Split-Path (Split-Path $PSScriptRoot)) "data\missions\$($m.File).fs2"
	$text = ($L -join "`r`n") + "`r`n"
	[IO.File]::WriteAllBytes($out, [Text.Encoding]::GetEncoding(28591).GetBytes($text))
	$out
}

# The player, Alpha 1, alone in Alpha wing at the origin.
function Add-Player($m, [hashtable]$s = @{}) {
	$s.Name = 'Alpha 1'; $s.Class = $m.PlayerClass
	if (-not $s.Flags) { $s.Flags = @('cargo-known') }
	[void](Add-Ship $m $s)
	Add-Wing $m @{ Name = 'Alpha'; Ships = @('Alpha 1'); Hotkey = 0 }
}

# Shorthand for the timeline: each event fires Chained seconds after the one before it.
function Add-Step($m, [string]$Name, [double]$After, [string[]]$Actions) {
	Add-Event $m @{ Name = $Name; Chained = $After; Formula = "( when`n   ( true )`n" + (($Actions | % { "   $_" }) -join "`n") + "`n)" }
}
