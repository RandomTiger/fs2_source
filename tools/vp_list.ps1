# Lists or extracts a VP archive (FreeSpace 1 or 2).
# Usage: powershell -File tools\vp_list.ps1 <file.vp>                       lists path and size of each file
#        powershell -File tools\vp_list.ps1 <file.vp> -OutDir <dir> [-Match <regex>]   extracts files whose path matches
# Extract into a scratch folder outside the repo: VP contents are FreeSpace data and are never committed.
param([string]$Vp, [string]$OutDir = "", [string]$Match = "")
$fs = [IO.File]::OpenRead($Vp); $br = New-Object IO.BinaryReader($fs)
$sig = [Text.Encoding]::ASCII.GetString($br.ReadBytes(4)); $ver = $br.ReadInt32(); $diroff = $br.ReadInt32(); $n = $br.ReadInt32()
if ($sig -ne 'VPVP') { $br.Close(); throw "not a VP: $sig" }
$fs.Seek($diroff, 'Begin') | Out-Null
$path = @(); $list = @()
for ($i = 0; $i -lt $n; $i++) {
	$off = $br.ReadInt32(); $size = $br.ReadInt32(); $nb = $br.ReadBytes(32); $ts = $br.ReadInt32()
	$name = [Text.Encoding]::ASCII.GetString($nb).Split([char]0)[0]
	# a zero-size entry is a directory; ".." goes back up one
	if ($name -eq '..') { if ($path.Count -gt 1) { $path = @($path[0..($path.Count-2)]) } else { $path = @() }; continue }
	if ($size -eq 0) { $path += $name; continue }
	$list += [pscustomobject]@{ Path = (($path + $name) -join '\'); Size = $size; Off = $off }
}
if ($OutDir) {
	foreach ($e in $list) {
		if ($Match -and $e.Path -notmatch $Match) { continue }
		$dst = Join-Path $OutDir $e.Path; New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
		$fs.Seek($e.Off, 'Begin') | Out-Null; [IO.File]::WriteAllBytes($dst, $br.ReadBytes($e.Size))
	}
} else { "VP $Vp version $ver entries $n"; $list | ForEach-Object { "{0}`t{1}" -f $_.Path, $_.Size } }
$br.Close()
