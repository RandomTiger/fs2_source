# Gate Enhanced (already merged), then merge each layer into the next, gate it, and push it; stops at the first
# conflict or red gate.
param([string]$Label = 'up', [string]$What = 'see the merged commits', [switch]$FirstIsDone,[string[]]$Layers = @('Enhanced', 'PrePort32', 'PrePort64'))
$Layers = @($Layers | ForEach-Object { $_ -split ',' })
. "$PSScriptRoot\common.ps1"
$root = Split-Path $RepoRoot -Parent	# the worktrees fs2_source_<Layer> sit next to this checkout
$msg = "`n`nCo-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
for ($i = 0; $i -lt $Layers.Count; $i++) {
	$l = $Layers[$i]; $r = "$root\fs2_source_$l"
	# GitHub first: another agent may have pushed to this layer since the local copy was last synced.
	git -C $r fetch -q origin $l 2>&1 | Out-Null
	$behind = [int](git -C $r rev-list --count "$l..origin/$l")
	if ($behind -gt 0) {
		git -C $r merge -q --no-ff "origin/$l" -m "Merge origin/$l into ${l}: $behind commits pushed by another agent$msg" 2>&1 | Out-Null
		if ($LASTEXITCODE) { "$l`: MERGE CONFLICT (origin/$l), stopped"; git -C $r diff --name-only --diff-filter=U; exit 1 }
		"$l`: merged $behind commits from GitHub"
	}
	if ($i -gt 0) {
		$prev = $Layers[$i - 1]
		git -C $r merge -q --no-ff $prev -m "Merge $prev into ${l}: $What$msg" 2>&1 | Out-Null
		if ($LASTEXITCODE) { "$l`: MERGE CONFLICT, stopped"; git -C $r diff --name-only --diff-filter=U; exit 1 }
	}
	if ($i -eq 0 -and $FirstIsDone) { "$l`: already green and pushed"; continue }
	if ($l -eq 'Enhanced') {
		$out = & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\e_gate.ps1" -Repo $r -Label "${Label}_$l"
		$out | Set-Content "$env:TEMP\${Label}_$l.txt"
		$fails = @($out | Where-Object { $_ -match '^FAIL ' })
		$ok = ($out -match '^warning_check: PASS') -and ($out -match '^tools: PASS') -and ($fails.Count -eq 0 -or ($fails.Count -eq 1 -and $fails[0] -match '^FAIL AutotestAI \|'))
	} else {
		$out = & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\p_gate.ps1" -Repo $r -Label "${Label}_$l"
		$out | Set-Content "$env:TEMP\${Label}_$l.txt"
		$ok = ($out | Where-Object { $_ -and $_.Trim() } | Select-Object -Last 1) -match '^PASS'
	}
	if (-not $ok) { "$l`: GATE RED, stopped (see $env:TEMP\${Label}_$l.txt)"; $out; exit 1 }
	$p = git -C $r push -q origin $l 2>&1
	if ($LASTEXITCODE) { "$l`: PUSH FAILED, stopped (local is green but not on GitHub)"; $p; exit 1 }
	"$l`: green, pushed ($(Get-Date -Format HH:mm))"
}
'ALL GREEN'
