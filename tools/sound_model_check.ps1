<#
The no-sound check (decision D-031), beside nodraw_check.ps1: replays every recording replay_check.ps1 replays (recordings\autotest and recordings\played) with
FS2_REPLAY_SOUND_MODEL=1, so no sound-state answer comes from the sound device. A sound plays for its file's length
on the recorded clock, a looping one until it is stopped, a channel is always free, and the millisecond and
microsecond clocks are read from the recorded frame clock. Each replay's game-state trace is compared with the
recorded one. A mission that does not MATCH has gameplay that depends on what the sound device answered.

Usage (PowerShell, from anywhere):
  tools\sound_model_check.ps1 [-Out <dir>] [-Missions sm1-01,...] [-GameDir <dir>]

Results go to <Out>\summary.txt (default replays\soundmodel_<date-time>). Release build only; build it first, and
don't run the game from another session while this runs.
#>
param(
	[string]$Out,
	[string[]]$Missions,
	[string]$GameDir = $env:PATH_FS2
)

$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Out) { $Out = Join-Path $repo ("replays\soundmodel_" + (Get-Date -Format 'yyyyMMdd_HHmm')) }
$Missions = $Missions | ForEach-Object { $_ -split ',' } | Where-Object { $_ }

$env:FS2_REPLAY_SOUND_MODEL = '1'
$a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'replay_check.ps1'), '-GameDir', $GameDir, '-Out', $Out, '-CrossCheck', 'none')
if ($Missions) { $a += @('-Missions', ($Missions -join ',')) }
& powershell @a
$code = $LASTEXITCODE
$env:FS2_REPLAY_SOUND_MODEL = $null
exit $code
