# Decodes a FreeSpace 1 table or mission encrypted with Volition's old scheme: signature 0xDEADBEEF
# (chars packed into 7 bits, then XOR with the byte offset) or 0xCACACACA (XOR only).  Same algorithm
# as the #else branch of code/Parse/Encrypt.cpp.  Files without either signature are copied unchanged.
# Usage: powershell -File tools\fs1_decrypt.ps1 <in> <out>
# For reading FS1 data during development only: never commit the output (it is FreeSpace data).
param([string]$In, [string]$Out)
$b = [IO.File]::ReadAllBytes($In)
$id = [BitConverter]::ToInt32($b, 0)
if ($id -ne 0xdeadbeef -and $id -ne 0xcacacaca) { [IO.File]::WriteAllBytes($Out, $b); return }
$n = $b.Length - 4
$s = New-Object byte[] $n
for ($i = 0; $i -lt $n; $i++) { $s[$i] = $b[$i + 4] -bxor ($i -band 0xff) }
if ($id -eq 0xcacacaca) { [IO.File]::WriteAllBytes($Out, $s); return }
$t = New-Object Collections.Generic.List[byte]
$so = 0
$runs = [Math]::Ceiling($n / 7.0)
for ($r = 0; $r -lt $runs; $r++) {
	# a run is 8 chars packed into 7 bytes
	$t.Add([byte](($s[$so] -shr 1) -band 0x7f)); $so++
	$done = $false
	for ($k = 2; $k -le 7; $k++) {
		if ($so -ge $n) { $done = $true; break }
		$lo = ($s[$so] -shr $k) -band ((1 -shl (8 - $k)) - 1)
		$hi = ($s[$so - 1] -shl (8 - $k)) -band (0x7f -bxor ((1 -shl (8 - $k)) - 1))
		$t.Add([byte](($lo -bor $hi) -band 0x7f)); $so++
	}
	if ($done) { break }
	# 8th char: the low 7 bits of the run's last byte.  Like Encrypt.cpp, step past that byte only if the char is not 0.
	$so--
	$last = $s[$so] -band 0x7f
	if ($last -gt 0) { $t.Add([byte]$last); $so++ }
}
[IO.File]::WriteAllBytes($Out, $t.ToArray())
