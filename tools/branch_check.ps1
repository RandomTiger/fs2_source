# Checks the branches against the rules in docs/BRANCHES.md.  Read-only: it fetches, then reports.
# Usage: powershell -File tools\branch_check.ps1 [-NoFetch]
# Exit code 1 if a rule is broken (ERROR), 0 otherwise.  WARN lines are things to tidy, not breakages.
param([switch]$NoFetch)

$layers = 'master', 'VS2015', 'VS2022', 'OriginalSourceBugFixes', 'RecordPlayback', 'Latest', 'GodotPrep', 'Godot'
$errors = 0

function Out-Error($msg) { Write-Output "ERROR  $msg"; $script:errors++ }
function Out-Warn($msg) { Write-Output "WARN   $msg" }
function Out-Ok($msg) { Write-Output "ok     $msg" }
function Count($range) { [int](git rev-list --count $range) }
function Set-Of($revs) { $h = @{}; foreach ($c in $revs) { $h[$c] = 1 }; $h }

if (-not $NoFetch) { git fetch -q --prune origin }

$remote = @(git for-each-ref --format='%(refname:short)' refs/remotes/origin | Where-Object { $_ -ne 'origin/HEAD' -and $_ -ne 'origin' } | ForEach-Object { $_.Substring(7) })
$present = @($layers | Where-Object { $remote -contains $_ })
$features = @($remote | Where-Object { $layers -notcontains $_ })

# commits made directly on a branch (its first-parent line) that are not on the first-parent line of another
function Own-Commits($branch, $below) {
	$fpBelow = Set-Of (git rev-list --first-parent "origin/$below")
	@(git rev-list --first-parent "origin/$branch" | Where-Object { -not $fpBelow.ContainsKey($_) })
}

# commits made directly on a branch that are not in another branch at all
function Own-Above($branch, $base) {
	$all = Set-Of (git rev-list "origin/$base")
	@(git rev-list --first-parent "origin/$branch" | Where-Object { -not $all.ContainsKey($_) })
}

Write-Output "== Layers: $($present -join ' -> ')"
for ($i = 1; $i -lt $present.Count; $i++) {
	$lo = $present[$i - 1]; $hi = $present[$i]
	$behind = Count "origin/$hi..origin/$lo"
	if ($behind -gt 0) { Out-Warn "$hi is missing $behind commit(s) of ${lo}: merge $lo into $hi" } else { Out-Ok "$hi has everything from $lo" }
	# a downward merge puts commits made on the higher branch into the lower one
	$loAll = Set-Of (git rev-list "origin/$lo")
	$down = @(Own-Commits $hi $lo | Where-Object { $loAll.ContainsKey($_) })
	if ($down.Count -gt 0) { Out-Error "$lo contains $($down.Count) commit(s) made on ${hi}: a merge went downward" }
}

if ($present -contains 'RecordPlayback') {
	# the first commit made on RecordPlayback itself; every feature must contain it
	$rpRoot = (Own-Commits 'RecordPlayback' 'VS2022' | Select-Object -Last 1)
	$latestFp = if ($present -contains 'Latest') { Set-Of (git rev-list --first-parent origin/Latest) } else { @{} }
	$latestAll = if ($present -contains 'Latest') { Set-Of (git rev-list origin/Latest) } else { @{} }
	$latestOwn = if ($present -contains 'Latest') { @(Own-Above 'Latest' 'RecordPlayback') } else { @() }
	Write-Output "== Features: $($features -join ', ')"
	foreach ($f in $features) {
		$tip = git rev-parse "origin/$f"
		git merge-base --is-ancestor $rpRoot "origin/$f"
		if ($LASTEXITCODE -ne 0) {
			Out-Error "$f is not based on RecordPlayback: merge RecordPlayback into it"
		}
		$behind = Count "origin/$f..origin/RecordPlayback"
		$ahead = Count "origin/RecordPlayback..origin/$f"
		if ($behind -gt 0) { Out-Warn "$f is $behind commit(s) behind RecordPlayback ($ahead of its own)" } else { Out-Ok "$f is up to date with RecordPlayback ($ahead of its own)" }
		$fAll = Set-Of (git rev-list "origin/$f")
		$fromLatest = @($latestOwn | Where-Object { $fAll.ContainsKey($_) })
		if ($fromLatest.Count -gt 0) { Out-Error "$f contains $($fromLatest.Count) commit(s) made on Latest: Latest was merged into a feature" }
		foreach ($g in $features) {
			if ($g -eq $f) { continue }
			$gOwn = @(Own-Above $g 'RecordPlayback')
			$mixed = @($gOwn | Where-Object { $fAll.ContainsKey($_) })
			if ($mixed.Count -gt 0) { Out-Error "$f contains $($mixed.Count) commit(s) made on ${g}: features must not merge into each other" }
		}
		if ($latestFp.ContainsKey($tip) -and $ahead -gt 0) { Out-Error "$f was fast-forwarded into Latest: use git merge --no-ff" }
		elseif ($latestAll.ContainsKey($tip) -and $ahead -gt 0) { Out-Ok "$f is fully merged into Latest" }
		elseif ($ahead -gt 0 -and $present -contains 'Latest') {
			$merged = @(git rev-list "origin/$f" --not origin/RecordPlayback | Where-Object { $latestAll.ContainsKey($_) }).Count
			if ($merged -gt 0) { Out-Warn "$f has $($ahead - $merged) commit(s) not yet merged into Latest" }
		}
	}
}

Write-Output "== Local branches"
foreach ($b in @(git for-each-ref --format='%(refname:short)' refs/heads)) {
	if ($remote -notcontains $b) { Out-Warn "$b exists only on this PC: push it or delete it"; continue }
	$ahead = Count "origin/$b..$b"; $behind = Count "$b..origin/$b"
	if ($ahead -gt 0 -and $behind -gt 0) { Out-Error "$b and origin/$b have diverged: merge origin/$b, never force-push" }
	elseif ($ahead -gt 0) { Out-Warn "$b has $ahead unpushed commit(s)" }
	elseif ($behind -gt 0) { Out-Warn "$b is $behind commit(s) behind origin: pull" }
}

Write-Output "== $errors error(s)"
if ($errors -gt 0) { exit 1 }
exit 0
