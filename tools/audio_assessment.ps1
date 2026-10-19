<#
The audio assessment listening session (docs/AUDIO_ASSESSMENT.md on PrePortCleanup). Each scene recording in
recordings\audio is replayed twice at normal speed, once with Soundcard=DirectSound and once with Soundcard=EAX
(DirectSound3D, with EAX reverb when DSOAL is given), in a random order called A and B. One scene, chosen at
random, plays the same backend twice as a check on the listening; the listener is not told which.

  tools\audio_assessment.ps1 [-Dsoal <folder with the Win32 dsound.dll, dsoal-aldrv.dll, alsoft.ini>]
                             [-Scenes AudioA,...] [-GameDir <dir>] [-Out <dir>]

Build the listening exe first (FS2vs2022r.exe in the game folder). Every replay uses FS2_REPLAY_SOUND_MODEL=1, so
gameplay does not depend on the sound device and both runs of a scene show the same events, and -replay_pace, so
it plays at the speed it was recorded. Master volume comes from the recording (replay refuses another) and the
music volume with the recording's pilot, so both are the same for both runs; the order file says so.

For each scene the listener answers A, B or S (same) and may add a note. Answers go to <Out>\answers.txt as they
are given; the order, the backends and the check pair go to <Out>\order.txt, to be opened afterwards. DSOAL's
files are copied beside the exe only for the EAX runs and always removed again.
#>
param(
	[string]$Dsoal,
	[string[]]$Scenes,
	[string]$GameDir = $env:PATH_FS2,
	[string]$Out
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
$exe = Join-Path $GameDir 'FS2vs2022r.exe'
if (-not (Test-Path $exe)) { throw "No $exe; build Release first" }
if (-not $Out) { $Out = Join-Path $repo ('replays\audio_' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$recDir = Join-Path $repo 'recordings\audio'
$Scenes = @($Scenes | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if (-not $Scenes) { $Scenes = @(Get-ChildItem $recDir -Filter 'Audio*.fs2rec' | ForEach-Object { $_.BaseName }) }
if (-not $Scenes) { throw "No recordings in $recDir" }
$dsoalFiles = 'dsound.dll', 'dsoal-aldrv.dll', 'alsoft.ini'
if ($Dsoal) { foreach ($f in $dsoalFiles) { if (-not (Test-Path (Join-Path $Dsoal $f))) { throw "No $f in $Dsoal" } } }
foreach ($f in $dsoalFiles) { if (Test-Path (Join-Path $GameDir $f)) { throw "$f is already in $GameDir; remove it first" } }

# one settings file per backend, made from the game's own so the screen size matches the recordings
$baseIni = Get-Content (Join-Path $GameDir 'fs2.ini')
$ini = @{}
foreach ($b in 'DirectSound', 'EAX') {
	$ini[$b] = Join-Path $Out "fs2_$b.ini"
	$baseIni | ForEach-Object { if ($_ -match '^Soundcard=') { "Soundcard=$b" } else { $_ } } | Set-Content -Encoding ascii $ini[$b]
}

function Play-Scene([string]$Scene, [string]$Backend, [string]$Label) {
	$rec = Join-Path $recDir "$Scene.fs2rec"
	$trace = Join-Path $Out "$Scene`_$Label.trace"
	$useDsoal = $Dsoal -and ($Backend -eq 'EAX')
	if ($useDsoal) { foreach ($f in $dsoalFiles) { Copy-Item (Join-Path $Dsoal $f) $GameDir } }
	$env:FS2_REPLAY_SOUND_MODEL = '1'
	try {
		$p = Start-Process $exe -WorkingDirectory $GameDir -PassThru `
			-ArgumentList "-window -autotest -replay `"$rec`" -replay_pace -trace `"$trace`" -ini `"$($ini[$Backend])`""
		$p.WaitForExit()
		$p.ExitCode
	} finally {
		$env:FS2_REPLAY_SOUND_MODEL = $null
		if ($useDsoal) { foreach ($f in $dsoalFiles) { Remove-Item (Join-Path $GameDir $f) -ErrorAction SilentlyContinue } }
	}
}

$rng = New-Object Random
$check = $Scenes[$rng.Next($Scenes.Count)]
$order = Join-Path $Out 'order.txt'
$answers = Join-Path $Out 'answers.txt'
"Audio assessment $(Get-Date -Format 'yyyy-MM-dd HH:mm'), exe $exe ($((Get-Item $exe).LastWriteTime)), DSOAL: $(if ($Dsoal) { $Dsoal } else { 'no' })" | Set-Content $order
"Audio assessment $(Get-Date -Format 'yyyy-MM-dd HH:mm'): A better, B better or S same, per scene" | Set-Content $answers

Write-Host "$($Scenes.Count) scenes. Each plays twice, A then B. Listen, then answer A, B or S (same)."
foreach ($s in $Scenes) {
	$pair = if ($rng.Next(2)) { @('DirectSound', 'EAX') } else { @('EAX', 'DirectSound') }
	if ($s -eq $check) { $one = $pair[$rng.Next(2)]; $pair = @($one, $one) }
	$notes = [regex]::Match((Get-Content -Raw (Join-Path $repo "data\missions\$s.fs2")), 'scene \w: ([^\r\n]*)').Groups[1].Value
	Write-Host "`n$s`: $notes"
	$codes = @()
	foreach ($i in 0, 1) {
		$label = 'AB'[$i]
		Read-Host "Press Enter to play $label" | Out-Null
		$codes += Play-Scene $s $pair[$i] $label
	}
	$a = ''
	while ($a -notmatch '^[ABS]$') { $a = (Read-Host 'A, B or S').Trim().ToUpper() }
	$note = Read-Host 'Note (Enter for none)'
	"$s`t$a`t$note" | Add-Content $answers
	$checkText = if ($s -eq $check) { "`tcheck pair: same backend twice" } else { '' }
	"$s`tA=$($pair[0])`tB=$($pair[1])`texit $($codes -join ',')$checkText" | Add-Content $order
}

"Volumes: as recorded for every run (replay refuses another master volume; the music volume comes with the recording's pilot)" | Add-Content $order
Write-Host "`nDone. Answers: $answers`nOrder (open after): $order"
