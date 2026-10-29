<#
Typed text through Godot's input (SteamIntegration, D-20261029-091007): Steam's own keyboard and overlay send text as OS text events
(a character and no physical key). The host maps each character onto the game's key path; this checks it on the pilot screen's
callsign box with known answers, with no Steam client and no device.

Per config: stock `godot --headless` + fs2godot + FS2game64<c>.dll, a live game (no autotest, no replay) with no pilots, and
FS2_TEXT_INJECT=1 (godot\runner.gd TEXT_SCRIPT) types:
  1  "Gone", Escape (text), in the box the game opens with                                                       -> the game cancels: no pilot
     (it also stops the game taking the first pilot straight to the main hall)
  2  'c', "Abcdefghijklmnopqrstuvwxyz1" as text events in one Godot frame, Enter as a keycode, no physical key  -> the longest name (27)
  3  'c', "Zyxwvutsrqponmlkjihgfedcba98" (28) as text, Enter as a text control character                      -> the game keeps 27
  4  'c', "Steam Pilotx", Backspace (text), "_9", Enter                                                           -> space, Backspace, shift
  5  C, "Key Boardx", Backspace, Enter on a physical keyboard (shift held for capitals; the 'y' as a key without its character
     followed by the character as its own text event)                                                             -> no character twice
  6  Escape on the pilot screen                                                                                   -> the game quits
Pass = the game quits through its own quit (exit 1, the game's quit code outside autotest), all script steps ran, and the single-player pilots are exactly the four names (case-sensitive), no Gone.

The players folder of GameDir is moved aside for the run and put back after. Use a scratch game folder of your own.
Usage: tools\godot_text_check.ps1 [-Configs r,d] [-Out dir] [-GameDir dir] [-Godot exe]
Exit 0 all pass, 1 any fail.
#>
param(
	[string[]]$Configs = @('r', 'd'),
	[string]$Out,
	[string]$GameDir = $env:PATH_FS2,
	[string]$Godot	# default: Godot_v4.7.2-stable_win64_console.exe in $env:GODOT_ENGINE_4_7
)
$repo = Split-Path $PSScriptRoot
if (-not $GameDir) { throw 'Set PATH_FS2 or pass -GameDir' }
if (-not $Godot) { if (-not $env:GODOT_ENGINE_4_7) { throw 'GODOT_ENGINE_4_7 is not set, and no -Godot given' }; $Godot = Join-Path $env:GODOT_ENGINE_4_7 'Godot_v4.7.2-stable_win64_console.exe' }
if (-not $Out) { $Out = Join-Path $repo ('replays\godot_text_' + (Get-Date -Format 'yyyyMMdd_HHmm')) }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$proj = Join-Path $repo 'godot'
$Configs = $Configs | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
foreach ($f in @($Godot, (Join-Path $proj 'bin\fs2godot.dll')) + ($Configs | ForEach-Object { Join-Path $GameDir "FS2game64$_.dll" })) { if (-not (Test-Path $f)) { throw "Missing $f" } }
if (-not (Test-Path (Join-Path $proj '.godot\extension_list.cfg'))) { & $Godot --headless --path $proj --import | Out-Null }
$expect = @('Abcdefghijklmnopqrstuvwxyz1', 'Key Board', 'Steam Pilot_9', 'Zyxwvutsrqponmlkjihgfedcba9') | Sort-Object -CaseSensitive
$steps = 18	# TEXT_SCRIPT entries in runner.gd
$summary = Join-Path $Out 'summary.txt'
Remove-Item $summary -ErrorAction SilentlyContinue

function Wait-NoGame { while (Get-Process FS2vs2022*, fs2run, Godot*, FS2Godot* -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 } }

$players = Join-Path $GameDir 'data\players'
$aside = Join-Path $GameDir 'data\players_text_check_aside'
if (Test-Path $aside) { throw "$aside exists: a previous run did not put the players folder back; restore it first" }
$fail = 0; $pass = 0
Rename-Item $players $aside
try {
	foreach ($c in $Configs) {
		Remove-Item $players -Recurse -Force -ErrorAction SilentlyContinue
		New-Item -ItemType Directory -Force (Join-Path $players 'single'), (Join-Path $players 'multi') | Out-Null
		$pre = Join-Path $Out "text_$c"
		Wait-NoGame
		$env:FS2_DLL = Join-Path $GameDir "FS2game64$c.dll"; $env:FS2_ROOT = $GameDir; $env:FS2_CMD = '-window -headless'
		$env:FS2_TEXT_INJECT = '1'; $env:FS2_PACE = '0'
		$p = Start-Process $Godot -ArgumentList "--headless --path `"$proj`"" -WorkingDirectory $GameDir -PassThru -NoNewWindow -RedirectStandardOutput "$pre.out.txt" -RedirectStandardError "$pre.err.txt"
		$null = $p.Handle
		$code = if ($p.WaitForExit(600000)) { $p.ExitCode } else { Stop-Process $p -Force; 'TIMEOUT' }
		Remove-Item Env:FS2_DLL, Env:FS2_ROOT, Env:FS2_CMD, Env:FS2_TEXT_INJECT, Env:FS2_PACE -ErrorAction SilentlyContinue
		$ran = @(Select-String "$pre.out.txt" -Pattern '^text: step ').Count
		$got = @(Get-ChildItem (Join-Path $players 'single') -Filter *.plr | ForEach-Object { $_.BaseName }) | Sort-Object -CaseSensitive
		$multi = @(Get-ChildItem (Join-Path $players 'multi') -Filter *.plr).Count
		$quit = @(Select-String "$pre.out.txt" -Pattern '^runner: game finished, exit code 1,').Count
		$ok = ($code -eq 1) -and ($quit -eq 1) -and ($ran -eq $steps) -and ($multi -eq 0) -and ((($got -join '|') -ceq ($expect -join '|')))
		$line = "text ($c): exit=$code quit=$quit steps=$ran/$steps pilots=[$($got -join ', ')] multi=$multi"
		if ($ok) { $pass++ } else { $fail++; $line = "FAIL $line expected=[$($expect -join ', ')]" }
		$line | Add-Content $summary
		$line
	}
} finally {
	Remove-Item $players -Recurse -Force -ErrorAction SilentlyContinue
	Rename-Item $aside $players
}
"DONE pass=$pass fail=$fail" | Add-Content $summary
"DONE pass=$pass fail=$fail"
if ($fail) { exit 1 }
