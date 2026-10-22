# Captures every audio scene with both Soundcard settings, one capture.py run per take, and retries a take that came
# back silent (the loopback stream sometimes delivers only zeros). Throwaway branch AudioTrial only.
# Usage: tools\audio_ab\capture_all.ps1 -Out <dir> [-Python <python.exe>] [-GameDir <dir>]
param(
	[Parameter(Mandatory)][string]$Out,
	[string]$Python = 'python',
	[string]$GameDir = $env:PATH_FS2
)
$repo = Split-Path (Split-Path $PSScriptRoot)
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
foreach ($s in 'AudioA', 'AudioB', 'AudioC', 'AudioD', 'AudioE', 'AudioF', 'AudioG') {
	foreach ($b in 'DirectSound', 'EAX') {
		foreach ($try in 1..3) {
			$env:CAPTURE_BACKENDS = $b
			$line = & $Python -W ignore "$PSScriptRoot\capture.py" $repo $GameDir $Out $s 2>&1 | Where-Object { "$_" -like "$s *" }
			"$line (try $try)"
			if ("$line" -notlike '*peak 0.000*') { break }
		}
	}
}
Remove-Item Env:CAPTURE_BACKENDS -ErrorAction SilentlyContinue
'DONE'
