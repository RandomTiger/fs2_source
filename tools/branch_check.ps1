# Checks the branches against the rules in docs/BRANCHES.md.  Read-only: it fetches, then reports.
# Usage: powershell -File tools\branch_check.ps1 [-NoFetch]
# Exit code 1 if a rule is broken (ERROR), 0 otherwise.  WARN lines are things to tidy, not breakages.
# KNOWN lines are written exceptions: shown, not counted.
# Parent map and exceptions per D-20261107-092836: each comes only from docs/BRANCHES.md or a decision and cites it;
# nothing is inferred from git history, which holds the breaches this check exists to catch.  A branch on origin that
# is not in the map is an ERROR "parent unknown", never given a default.  No breach becomes an exception.
param([switch]$NoFetch)

# Parent map.  Kind: layer, feature, pointer.  Target: the branch it merges into ($null = not written anywhere).
$map = [ordered]@{
	'master'                 = @{ Kind = 'layer'; Parent = $null; Src = 'BRANCHES.md line 6' }
	'VS2022'                 = @{ Kind = 'layer'; Parent = 'master'; Src = 'BRANCHES.md line 7' }
	'OriginalSourceBugFixes' = @{ Kind = 'layer'; Parent = 'VS2022'; Src = 'BRANCHES.md line 8' }
	'RecordPlayback'         = @{ Kind = 'layer'; Parent = 'OriginalSourceBugFixes'; Src = 'BRANCHES.md line 9' }
	'Enhanced'               = @{ Kind = 'layer'; Parent = 'RecordPlayback'; Src = 'BRANCHES.md line 11' }
	'PrePort32'              = @{ Kind = 'layer'; Parent = 'Enhanced'; Src = 'BRANCHES.md lines 12, 18' }
	'PrePort64'              = @{ Kind = 'layer'; Parent = 'PrePort32'; Src = 'BRANCHES.md lines 13, 18' }
	'Godot'                  = @{ Kind = 'layer'; Parent = 'PrePort64'; Src = 'BRANCHES.md line 14' }
	'GodotPlus'              = @{ Kind = 'layer'; Parent = 'Godot'; Src = 'BRANCHES.md line 15 (as GodotEnhanced); D-20261105-171620 (renamed GodotPlus, meeting branch)' }
	'Latest'                 = @{ Kind = 'pointer'; Parent = 'GodotPlus'; Src = 'BRANCHES.md line 20' }
	'FS2Demo'                = @{ Kind = 'feature'; Parent = 'RecordPlayback'; Target = 'Enhanced'; Src = 'BRANCHES.md lines 10, 33' }
	'FS1Compat'              = @{ Kind = 'feature'; Parent = 'RecordPlayback'; Target = 'Enhanced'; Src = 'BRANCHES.md lines 10, 33' }
	'FS1Demo'                = @{ Kind = 'feature'; Parent = 'FS1Compat'; Target = 'Enhanced'; Src = 'BRANCHES.md lines 10, 34' }
	'VoiceControl'           = @{ Kind = 'feature'; Parent = 'RecordPlayback'; Target = 'Enhanced'; Src = 'BRANCHES.md line 10; D-20261021-105201' }
	'PadControl'             = @{ Kind = 'feature'; Parent = 'VoiceControl'; Target = 'Enhanced'; Src = 'BRANCHES.md lines 10, 35' }
	'MissionJump'            = @{ Kind = 'feature'; Parent = 'RecordPlayback'; Target = 'Enhanced'; Src = 'D-20261105-171620 (feature off RecordPlayback, done); BRANCHES.md lines 29-30' }
	'NoMultiplayer'          = @{ Kind = 'feature'; Parent = 'RecordPlayback'; Target = 'Enhanced'; Src = 'D-20261105-171620 (feature off RecordPlayback, done); BRANCHES.md lines 29-30' }
	'FrameShots'             = @{ Kind = 'feature'; Parent = 'RecordPlayback'; Target = 'Enhanced'; Src = 'D-20261105-171620 (feature off RecordPlayback, done); BRANCHES.md lines 29-30' }
	'AutoPlay'               = @{ Kind = 'feature'; Parent = 'RecordPlayback'; Target = 'RecordPlayback'; Src = 'D-20261107-124841 (feature off RecordPlayback); D-20261108-161952 (Tom: merges back into RecordPlayback, done as 99ca4fd)' }
	'GodotRender3D'         = @{ Kind = 'feature'; Parent = 'Godot'; Target = 'GodotPlus'; Src = 'D-20261031-111802 (off Godot); D-20261105-171620 (merges into GodotPlus)' }
	'GodotResolution'        = @{ Kind = 'feature'; Parent = 'Godot'; Target = 'GodotPlus'; Src = 'D-20261102-200801 (off Godot); D-20261105-171620 (merges into GodotPlus)' }
	'GodotOptions'           = @{ Kind = 'feature'; Parent = 'GodotResolution'; Target = 'GodotPlus'; Src = 'D-20261102-200808 (child of GodotResolution); D-20261105-171620 (merges into GodotPlus)' }
	'SteamIntegration'       = @{ Kind = 'feature'; Parent = 'Godot'; Target = 'GodotPlus'; Src = 'D-20261031-090335 (direct child of Godot); D-20261105-171620 (merges into GodotPlus)' }
	'GodotRender3D-tex'      = @{ Kind = 'feature'; Parent = 'GodotRender3D'; Target = $null; Src = 'D-20261107-071934 d (sub-branch of GodotRender3D; its merge is Tom''s, no target written)' }
}

# Written exceptions: one named branch and one named merge commit each; the commits that merge brought in are excused
# on that branch only.
$exceptions = @(
	@{ Branch = 'FS1Compat'; Merge = '95ee575'; What = 'Enhanced merged down into FS1Compat (batch 4)'; Src = 'D-20261022-152101 option 3' }
	@{ Branch = 'FS1Compat'; Merge = '24cd57f'; What = 'Enhanced merged down into FS1Compat'; Src = 'D-20261022-152101 option 3; D-20261022-161947' }
	@{ Branch = 'FS1Compat'; Merge = 'f2363f5'; What = 'FS1Demo merged into FS1Compat, reverted on FS1Compat'; Src = 'D-20261021-173031 rule 4; D-20261023-202744' }
	# only these four commits, not all a pull merge brings: a later VoiceControl commit in RecordPlayback stays an ERROR
	@{ Branch = 'RecordPlayback'; Merge = '26fc888'; Commits = 'c403875', 'c3e1e0a', 'b26050f', 'be8fd99'; What = 'pull merge of origin/RecordPlayback; its own four commits sit on its second parent, where VoiceControl was cut'; Src = 'D-20261107-095649' }
	# Tom ordered AutoPlay merged back into RecordPlayback; only its two commits, a later AutoPlay commit stays an ERROR
	@{ Branch = 'RecordPlayback'; Merge = '99ca4fd'; Commits = '31df01a', 'f8a408f'; What = 'AutoPlay merged into RecordPlayback (Tom, 2026-11-07)'; Src = 'D-20261108-161952' }
)

$errors = 0
$unknown = @()
function Out-Error($msg) { Write-Output "ERROR  $msg"; $script:errors++ }
function Out-Warn($msg) { Write-Output "WARN   $msg" }
function Out-Ok($msg) { Write-Output "ok     $msg" }
function Out-Known($msg) { Write-Output "KNOWN  $msg" }
function Count($range) { [int](git rev-list --count $range) }
function Set-Of($revs) { $h = @{}; foreach ($c in $revs) { $h[$c] = 1 }; $h }

if (-not $NoFetch) { git fetch -q --prune origin }

$remote = @(git for-each-ref --format='%(refname:short)' refs/remotes/origin | Where-Object { $_ -ne 'origin/HEAD' -and $_ -ne 'origin' } | ForEach-Object { $_.Substring(7) })
$present = @($map.Keys | Where-Object { $map[$_].Kind -eq 'layer' -and $remote -contains $_ })
$features = @($map.Keys | Where-Object { $map[$_].Kind -eq 'feature' -and $remote -contains $_ })

# cached commit sets
$allCache = @{}; $fpCache = @{}
function All-Of($b) { if (-not $allCache.ContainsKey($b)) { $allCache[$b] = Set-Of (git rev-list "origin/$b") }; $allCache[$b] }
function Fp-Of($b) { if (-not $fpCache.ContainsKey($b)) { $fpCache[$b] = @(git rev-list --first-parent "origin/$b") }; $fpCache[$b] }

# commits made directly on a branch (its first-parent line) that are not on the first-parent line of another
function Own-Commits($branch, $below) {
	$fpBelow = Set-Of (Fp-Of $below)
	@(Fp-Of $branch | Where-Object { -not $fpBelow.ContainsKey($_) })
}

# commits made directly on a branch that are not in another branch at all
function Own-Above($branch, $base) {
	$all = All-Of $base
	@(Fp-Of $branch | Where-Object { -not $all.ContainsKey($_) })
}

# ancestors in the map (parent, its parent, ...)
function Chain($b) { $c = @(); $p = $map[$b].Parent; while ($p) { $c += $p; $p = $map[$p].Parent }; $c }

# like Own-Commits, but also not on the first-parent line of any ancestor of $below in the map (D-20261108-164648):
# a child cut from an older line of its parent carries the grandparent's own commits, which are not the child's.
# The chain stops at the first ancestor that is not in the map or not on origin; reachability is never used.
function Own-Commits-Chain($branch, $below) {
	$drop = Set-Of (Fp-Of $below)
	foreach ($a in Chain $below) {
		if (-not $map.Contains($a) -or $remote -notcontains $a) { break }
		foreach ($c in Fp-Of $a) { $drop[$c] = 1 }
	}
	@(Fp-Of $branch | Where-Object { -not $drop.ContainsKey($_) })
}

# exceptions: excused commit set per exception, keyed by branch
$excused = @{}
foreach ($x in $exceptions) {
	$x.Set = if ($x.Commits) { Set-Of @($x.Commits | ForEach-Object { git rev-parse "$_^{commit}" }) } else { Set-Of (git rev-list "$($x.Merge)^2" --not "$($x.Merge)^1" 2>$null) }
	$x.Hits = 0
	if (-not $excused.ContainsKey($x.Branch)) { $excused[$x.Branch] = @() }
	$excused[$x.Branch] += $x
}
# drop excused commits from a list found on branch $b, counting what each exception covered
function Drop-Excused($b, $list) {
	if (-not $excused.ContainsKey($b)) { return @($list) }
	$keep = @()
	foreach ($c in $list) {
		$hit = $false
		foreach ($x in $excused[$b]) { if ($x.Set.ContainsKey($c)) { $x.Hits++; $hit = $true; break } }
		if (-not $hit) { $keep += $c }
	}
	@($keep)
}

Write-Output "== Parents"
foreach ($b in $remote) {
	if (-not $map.Contains($b)) { Out-Error "$b parent unknown: written in neither docs/BRANCHES.md nor a decision; Tom's to give"; $unknown += $b; continue }
	$p = $map[$b].Parent
	if ($p -and $remote -notcontains $p) { Out-Warn "$b's parent $p is not on origin" }
}

Write-Output "== Layers: $($present -join ' -> ')"
for ($i = 1; $i -lt $present.Count; $i++) {
	$lo = $present[$i - 1]; $hi = $present[$i]
	$behind = Count "origin/$hi..origin/$lo"
	if ($behind -gt 0) { Out-Warn "$hi is missing $behind commit(s) of ${lo}: merge $lo into $hi" } else { Out-Ok "$hi has everything from $lo" }
	# a downward merge puts commits made on the higher branch into the lower one
	$loAll = All-Of $lo
	$down = @(Own-Commits $hi $lo | Where-Object { $loAll.ContainsKey($_) })
	if ($down.Count -gt 0) { Out-Error "$lo contains $($down.Count) commit(s) made on ${hi}: a merge went downward" }
}

# FS1Compat builds the game into PATH_FS1 (docs/FS1_COMPAT.md); that build target must never reach a layer
$fs1Target = 0
foreach ($l in $present) {
	foreach ($ref in @("origin/$l") + @(git for-each-ref --format='%(refname:short)' "refs/heads/$l")) {
		git grep -q 'PATH_FS1' $ref -- '*.vcxproj' '*.vcxproj.user'
		if ($LASTEXITCODE -eq 0) { Out-Error "$ref builds into PATH_FS1: revert FS1Compat's build-target commit (docs/FS1_COMPAT.md, Before merging)"; $fs1Target++ }
	}
}
if ($fs1Target -eq 0) { Out-Ok "no layer builds into PATH_FS1" }

if (($remote -contains 'Latest') -and ($present -contains 'GodotPlus')) {	# Latest follows GodotPlus (2026-11-07)
	git merge-base --is-ancestor origin/Latest origin/GodotPlus
	if ($LASTEXITCODE -ne 0) { Out-Error "Latest is not part of GodotPlus: Latest must only be moved forward to GodotPlus" }
	else {
		$behind = Count "origin/Latest..origin/GodotPlus"
		if ($behind -gt 0) { Out-Warn "Latest is $behind commit(s) behind GodotPlus: move it up once GodotPlus passes its autotests" } else { Out-Ok "Latest is at GodotPlus" }
	}
}

# commits made on each feature itself: on its first-parent line and not in its parent
$own = @{}
foreach ($f in $features) { if ($remote -contains $map[$f].Parent) { $own[$f] = @(Own-Above $f $map[$f].Parent) } }

Write-Output "== Features: $($features -join ', ')"
foreach ($f in $features) {
	$p = $map[$f].Parent; $t = $map[$f].Target
	if ($remote -notcontains $p) { continue }
	$fAll = All-Of $f; $pAll = All-Of $p
	$tip = git rev-parse "origin/$f"
	$behind = Count "origin/$f..origin/$p"
	$ahead = Count "origin/$p..origin/$f"
	if ($behind -gt 0) { Out-Warn "$f is $behind commit(s) behind its parent $p ($ahead of its own)" } else { Out-Ok "$f is up to date with its parent $p ($ahead of its own)" }

	# a child merged into its parent: commits on the child's first-parent line, off the parent's, that are in the parent
	if ($map[$p].Kind -ne 'feature' -or $t -ne $p) {
		$up = Drop-Excused $p @(Own-Commits-Chain $f $p | Where-Object { $pAll.ContainsKey($_) })
		if ($up.Count -gt 0) { Out-Error "$p contains $($up.Count) commit(s) made on its child ${f}: a child was merged into its parent" }
	}

	# target commits: a feature holds no commit made on its target that is not in its parent
	$fromTarget = @()
	if (-not $t) { Out-Warn "$f has no merge target written: Tom's to give" }
	elseif ($remote -contains $t) {
		$tParent = $map[$t].Parent
		$tOwn = if ($tParent -and $remote -contains $tParent) { @(Own-Above $t $tParent) } else { @(Fp-Of $t) }
		$fromTarget = Drop-Excused $f @($tOwn | Where-Object { $fAll.ContainsKey($_) -and -not $pAll.ContainsKey($_) })
		if ($fromTarget.Count -gt 0) { Out-Error "$f contains $($fromTarget.Count) commit(s) made on its target ${t}: $t was merged or fast-forwarded into it" }
	}

	# siblings: a feature holds no commit made on another feature, other than its own parents or children.
	# Commits in the layer the feature stands on came up through the layers; they are not a sibling's.
	# Commits that also sit in the target are left out when the target check above fired: one cause, one line.
	$tAll = if ($t -and $remote -contains $t) { All-Of $t } else { @{} }
	$layer = @(Chain $f | Where-Object { $map[$_].Kind -eq 'layer' })[0]
	$lAll = if ($remote -contains $layer) { All-Of $layer } else { @{} }
	$related = @(Chain $f) + @($features | Where-Object { (Chain $_) -contains $f })
	$hits = @()
	foreach ($g in $features) {
		if ($g -eq $f -or $related -contains $g -or -not $own.ContainsKey($g)) { continue }
		$mixed = @($own[$g] | Where-Object { $fAll.ContainsKey($_) -and -not $pAll.ContainsKey($_) -and -not $lAll.ContainsKey($_) -and -not ($fromTarget.Count -gt 0 -and $tAll.ContainsKey($_)) })
		$mixed = Drop-Excused $f $mixed
		if ($mixed.Count -gt 0) { $hits += "$g ($($mixed.Count))" }
	}
	if ($hits.Count -gt 0) { Out-Error "$f contains commits made on $($hits -join ', '): features must not merge into each other" }

	if ($t -and $remote -contains $t) {
		$tFp = Set-Of (Fp-Of $t)
		if ($tFp.ContainsKey($tip) -and $ahead -gt 0) { Out-Error "$f was fast-forwarded into ${t}: use git merge --no-ff" }
		elseif ($tAll.ContainsKey($tip) -and $ahead -gt 0) { Out-Ok "$f is fully merged into $t" }
		elseif ($ahead -gt 0) {
			$merged = @(git rev-list "origin/$f" --not "origin/$p" | Where-Object { $tAll.ContainsKey($_) }).Count
			if ($merged -gt 0) { Out-Warn "$f has $($ahead - $merged) commit(s) not yet merged into $t" }
		}
	}
}

Write-Output "== Known exceptions"
foreach ($x in $exceptions) { Out-Known "$($x.Branch) $($x.Merge): $($x.What) ($($x.Src)); excused $($x.Hits) commit(s) today" }

# Each branch names its own exes through FS2Layer in build\layer.props (D-20261101-064106). A merge keeps the target's
# value, but git takes a child's value silently when the target has not changed the file since, so compare it with the
# branch name (D-20261101-105804). Report only: the build never derives the name from the branch.
Write-Output "== Layer names (build\layer.props)"
foreach ($b in $remote) {
	$props = git show "origin/${b}:build/layer.props" 2>$null
	if ($LASTEXITCODE -ne 0) { continue }	# not merged up to the per-layer names yet
	$m = [regex]::Match(($props -join "`n"), '<FS2Layer>\s*([^<]*?)\s*</FS2Layer>')
	$want = if ($b -eq 'Latest') { 'GodotPlus' } else { $b }	# Latest is only ever moved to GodotPlus
	if (-not $m.Success) { Out-Error "$b has build\layer.props with no FS2Layer" }
	elseif ($m.Groups[1].Value -ne $want) { Out-Error "$b names its exes after '$($m.Groups[1].Value)', not '$want': a merge took another branch's layer name; restore $want in a commit on $b" }
	else { Out-Ok "$b layer name $want" }
}

Write-Output "== Local branches"
foreach ($b in @(git for-each-ref --format='%(refname:short)' refs/heads)) {
	if ($remote -notcontains $b) { Out-Warn "$b exists only on this PC: push it or delete it"; continue }
	$ahead = Count "origin/$b..$b"; $behind = Count "$b..origin/$b"
	if ($ahead -gt 0 -and $behind -gt 0) { Out-Error "$b and origin/$b have diverged: merge origin/$b, never force-push" }
	elseif ($ahead -gt 0) { Out-Warn "$b has $ahead unpushed commit(s)" }
	elseif ($behind -gt 0) { Out-Warn "$b is $behind commit(s) behind origin: pull" }
}

if ($unknown.Count -gt 0) { Write-Output "== Parent unknown, for Tom: $($unknown -join ', ')" }
Write-Output "== $errors error(s)"
if ($errors -gt 0) { exit 1 }
exit 0
