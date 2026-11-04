<#
Proves a version 140 pilot with the wrong medal count is refused and never crashes (#156).

  tools\stale_pilot_check.ps1 [-Configs r,d] [-Recording <file.fs2rec>]

Takes the 18-medal version 140 pilot a retail recording carries and writes it as AutotestStale.plr, at run time, so
no pilot file is committed.
  FS2 demo install (PATH_FS2DEMO, reads 16 medals): the guard must refuse it ("Cannot load pilot").
  FS2 retail install (PATH_FS2, reads 18 medals): the same bytes must load, so the refusal comes from the medal count.
Each run must also exit 0 with no Assert or Int3, leave the scratch pilot's bytes unchanged in the refused case, and
leave every real pilot in the install unchanged. A refused pilot makes autotest use its own new AutotestPilot, which
goes again afterwards. autotest.log and fs2.ini are restored. The last line is PASS, or FAIL with the reasons.

Covers only the medal mismatch. A pilot already damaged by an old build passes the guard's layout walk (#156).
#>
param(
	[string[]]$Configs = @('r', 'd'),
	[string]$Recording
)

$repo = Split-Path $PSScriptRoot
. "$PSScriptRoot\layer.ps1"
if (-not $Recording) { $Recording = Join-Path $repo 'recordings\autotest\AutotestChain.fs2rec' }
$Configs = @($Configs | ForEach-Object { $_ -split ',' })
$name = 'AutotestStale'
$bad = @()

function Get-RecordedPilot([string]$File) {
	# the version 3 replay_header (code/Replay/Replay.cpp): version at 8, loadout_size at 772, pilot_size at 796, 828 long
	$d = [IO.File]::ReadAllBytes($File)
	if ([BitConverter]::ToInt32($d, 8) -ne 3) { throw "$File is not a version 3 recording" }
	$start = 828 + [BitConverter]::ToInt32($d, 772)
	$size = [BitConverter]::ToInt32($d, 796)
	if (($size -le 0) -or (($start + $size) -gt $d.Length)) { throw "$File has no pilot at byte $start" }
	if ([BitConverter]::ToInt32($d, $start + 4) -ne 140) { throw "$File does not carry a version 140 pilot" }
	$p = New-Object byte[] $size
	[Array]::Copy($d, $start, $p, 0, $size)
	, $p
}

function Get-PilotHashes([string]$Dir) {
	Get-ChildItem (Join-Path $Dir 'data\players') -Recurse -File -ErrorAction SilentlyContinue |
		Where-Object { ($_.Name -notlike "$name.*") -and ($_.Name -notlike 'AutotestPilot.*') } |
		ForEach-Object { "$($_.FullName) $((Get-FileHash $_.FullName).Hash)" }
}

function Remove-Scratch([string]$Dir) {
	foreach ($n in $name, 'AutotestPilot') {
		Get-ChildItem (Join-Path $Dir 'data\players') -Recurse -File -Filter "$n.*" -ErrorAction SilentlyContinue | Remove-Item -Force
	}
}

$pilot = Get-RecordedPilot $Recording
$cases = @(
	@{ Label = 'demo'; Dir = $env:PATH_FS2DEMO; Mission = 'SPDemo-01.fs2'; Refused = $true },
	@{ Label = 'retail'; Dir = $env:PATH_FS2; Mission = 'SM1-01.fs2'; Refused = $false }
)
$temp = Join-Path $env:TEMP "stale_pilot_$PID"
New-Item -ItemType Directory -Force $temp | Out-Null

foreach ($c in $cases) {
	if (-not $c.Dir -or -not (Test-Path $c.Dir)) { $bad += "$($c.Label) install not found"; continue }
	Set-Location $c.Dir
	$kept = @('autotest.log', 'fs2.ini') | Where-Object { Test-Path $_ }
	foreach ($k in $kept) { Copy-Item $k (Join-Path $temp "$($c.Label)_$k") -Force }
	$before = Get-PilotHashes $c.Dir
	foreach ($e in $Configs) {
		$tag = "$($c.Label) $e"
		$exe = Get-FS2Exe $e
		if (-not (Test-Path $exe)) { $bad += "$tag no $exe"; "FAIL $tag no $exe"; continue }
		while (Get-Process $FS2ProcessNames -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
		Remove-Scratch $c.Dir
		Remove-Item 'autotest.log' -ErrorAction SilentlyContinue
		$single = Join-Path $c.Dir 'data\players\single'
		[IO.File]::WriteAllBytes((Join-Path $single "$name.plr"), $pilot)
		$p = Start-Process ".\$exe" -ArgumentList "-window -autotest -at_pilot $name -at_mission $($c.Mission) -at_time 1" -PassThru
		if (-not $p.WaitForExit(300000)) { Stop-Process $p -Force; $p.WaitForExit() }
		$log = if (Test-Path 'autotest.log') { Get-Content 'autotest.log' } else { @() }
		$left = [IO.File]::ReadAllBytes((Join-Path $single "$name.plr"))
		$same = [BitConverter]::ToString($left) -eq [BitConverter]::ToString($pilot)
		Remove-Scratch $c.Dir
		$why = @()
		if ($p.ExitCode -ne 0) { $why += "exit $($p.ExitCode)" }
		$refused = [bool]($log -match "Cannot load pilot '$name'")
		if ($refused -ne $c.Refused) { $why += $(if ($c.Refused) { 'pilot was not refused' } else { 'pilot was refused' }) }
		$used = if ($c.Refused) { 'AutotestPilot' } else { $name }
		if (-not ($log -match "Pilot '$used', skill level")) { $why += "log does not show pilot $used" }
		if ($c.Refused -and -not $same) { $why += 'refused pilot was changed' }
		if (-not ($log -match 'Asserts 0, Int3 0')) { $why += 'asserts, or no summary line' }
		if ($why) { $bad += "$tag $($why -join ', ')"; "FAIL $tag $($why -join ', ')" }
		else { "PASS $tag $(if ($refused) { 'refused' } else { 'loaded' })" }
	}
	$after = Get-PilotHashes $c.Dir
	if (Compare-Object @($before) @($after)) { $bad += "$($c.Label) real pilots changed"; "FAIL $($c.Label) real pilots changed" }
	foreach ($k in $kept) { Copy-Item (Join-Path $temp "$($c.Label)_$k") $k -Force }
	if ($kept -notcontains 'autotest.log') { Remove-Item 'autotest.log' -ErrorAction SilentlyContinue }
}
Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
if ($bad) { "FAIL $($bad -join '; ')"; exit 1 }
'PASS'
