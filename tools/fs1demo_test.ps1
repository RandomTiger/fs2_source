<#
Tests the FreeSpace 1 demo's installer reading (code\CFile\CabReader.cpp, code\CFile\DataSetFs1.cpp; docs\FS1_DEMO.md),
and that the FreeSpace 2 demo's extraction (code\CFile\DataSetInstall.cpp), whose inflater and zip reader it shares, still works.

It builds a small test program from those files and DataSetInstall.cpp, then, from the original fsdemo10.exe at run time:
  1. lists and extracts every file in data1.cab with the game's reader,
  2. does the same with an independent reader (.NET's zip and deflate, and the cabinet layout read here
     in C#), and compares the two file by file (size and SHA-256),
  3. runs dataset_fs1_extract into a fresh folder and checks it made fs1demo with only the Data files, that
     dataset_fs1_detect accepts it and refuses the FS1 retail install, and that a second extract refuses to
     write into the existing folder,
  4. with -Fs2Installer, makes the FS2 demo's folder with dataset_select_folder and compares every file with
     .NET's extraction of the same zip entries.
The 16-bit InstallShield setup cannot run on 64-bit Windows, so the independent reader stands in for it.

Usage (PowerShell, from anywhere):
  tools\fs1demo_test.ps1 [-Installer <fsdemo10.exe>] [-Out <dir>] [-Retail <FS1 install>] [-Fs2Installer <fs2demo10.exe>]

-Installer defaults to fsdemo10.exe in $env:FS1DEMO_INSTALLER's folder, or pass it.  -Retail defaults to $env:PATH_FS1.
Everything goes to <Out> (default $env:TEMP\fs1demo_test), never the repo: the demo's files may not be
redistributed.  Exit code 0 when every check passes.
#>
param(
	[string]$Installer = $env:FS1DEMO_INSTALLER,
	[string]$Out = (Join-Path $env:TEMP 'fs1demo_test'),
	[string]$Retail = $env:PATH_FS1,
	[string]$Fs2Installer
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
if (-not $Installer -or -not (Test-Path $Installer)) { throw 'Pass -Installer <fsdemo10.exe> (or set FS1DEMO_INSTALLER)' }
if (Test-Path $Out) { Remove-Item $Out -Recurse -Force }
New-Item -ItemType Directory -Force $Out | Out-Null
$fail = 0
function Check($ok, $what) { if ($ok) { "ok    $what" } else { "FAIL  $what"; $script:fail++ } }

# --- build the test program
$main = @'
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "CabReader.h"
#include "DataSetFs1.h"
#include "DataSetInstall.h"
int main(int argc, char **argv)
{
	char err[512];
	if ((argc == 4) && !strcmp(argv[1], "all")) {
		unsigned char *data, *out; int len; cab_archive cab; int i;
		if (dataset_zip_read(argv[2], "data1.cab", &data, &len, err, sizeof(err)) || cab_open(&cab, data, len, err, sizeof(err))) { printf("ERROR %s\n", err); return 1; }
		for (i = 0; i < cab.num_entries; i++) {
			char path[MAX_PATH]; FILE *fp;
			out = (unsigned char *)malloc(cab.entries[i].expanded + 1);
			if (cab_extract(&cab, i, out, err, sizeof(err))) { printf("ERROR %s\n", err); return 1; }
			sprintf(path, "%s\\%s", argv[3], cab.entries[i].path);
			for (char *p = path + strlen(argv[3]) + 1; *p; p++) if (*p == '\\') { *p = 0; CreateDirectory(path, NULL); *p = '\\'; }
			fp = fopen(path, "wb"); fwrite(out, 1, cab.entries[i].expanded, fp); fclose(fp); free(out);
			printf("FILE %s\t%u\n", cab.entries[i].path, cab.entries[i].expanded);
		}
		return 0;
	}
	if ((argc == 3) && !strcmp(argv[1], "extract")) {
		int r = dataset_fs1_extract(argv[2], err, sizeof(err));
		printf("EXTRACT %d %s\n", r, r ? err : "");
		return 0;
	}
	if ((argc == 3) && !strcmp(argv[1], "select")) {
		char dir[MAX_PATH];
		int r = dataset_select_folder(argv[2], "fs2demo", DATA_SET_UNKNOWN, dir, MAX_PATH, err, sizeof(err));
		printf("SELECT %d %s\n", r, r ? err : dir);
		return 0;
	}
	if ((argc == 3) && !strcmp(argv[1], "detect")) {
		printf("DETECT %d\n", dataset_fs1_detect(argv[2]));
		return 0;
	}
	return 2;
}
'@
$src = Join-Path $Out 'fs1demo_test_main.cpp'
Set-Content -Path $src -Value $main -Encoding ascii
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars32.bat'
$exe = Join-Path $Out 'fs1demo_test.exe'
$cfile = Join-Path $repo 'code\CFile'
$build = cmd /c "set `"PATH=${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;%PATH%`" && `"$vcvars`" >nul && cl /nologo /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /I`"$cfile`" `"$src`" `"$cfile\CabReader.cpp`" `"$cfile\DataSetFs1.cpp`" `"$cfile\DataSetInstall.cpp`" /Fe`"$exe`" /Fo`"$Out\\`" user32.lib gdi32.lib 2>&1"
if (-not (Test-Path $exe)) { $build; throw 'test program did not build' }
Check ($build -notmatch 'warning') 'test program builds with no warning'

# --- 1. the game's reader: every file
$mine = Join-Path $Out 'cab_game'
New-Item -ItemType Directory -Force $mine | Out-Null
$list = & $exe all $Installer $mine
$files = @($list | Where-Object { $_ -like 'FILE *' } | ForEach-Object { $f = $_.Substring(5) -split "`t"; [pscustomobject]@{ Path = $f[0]; Size = [long]$f[1] } })
Check (($LASTEXITCODE -eq 0) -and ($files.Count -gt 0)) "game reader lists and extracts every file ($($files.Count))"
$list | Where-Object { $_ -like 'ERROR *' }

# --- 2. the independent reader
$cs = @'
using System; using System.IO; using System.IO.Compression; using System.Text; using System.Collections.Generic;
public static class Fs1DemoRef {
	static uint U32(byte[] b, int o) { return BitConverter.ToUInt32(b, o); }
	static string Str(byte[] b, int o) { int e = o; while (b[e] != 0) e++; return Encoding.ASCII.GetString(b, o, e - o); }
	public static List<string> Extract(string installer, string outDir) {
		var names = new List<string>();
		byte[] b;
		using (var z = ZipFile.OpenRead(installer)) {
			var e = z.GetEntry("data1.cab");
			using (var s = e.Open()) { var ms = new MemoryStream(); s.CopyTo(ms); b = ms.ToArray(); }
		}
		int cdo = (int)U32(b, 12), ft = cdo + (int)U32(b, cdo + 0x0c), dirs = (int)U32(b, cdo + 0x1c), files = (int)U32(b, cdo + 0x28);
		for (int i = 0; i < files; i++) {
			int d = ft + (int)U32(b, ft + (dirs + i) * 4);
			int nameOff = (int)U32(b, d), dir = (int)U32(b, d + 4), flags = BitConverter.ToUInt16(b, d + 8);
			int exp = (int)U32(b, d + 10), comp = (int)U32(b, d + 14), off = (int)U32(b, d + 0x26);
			if ((flags & 8) != 0 || nameOff == 0) continue;
			string dn = Str(b, ft + (int)U32(b, ft + dir * 4)), path = dn.Length > 0 ? dn + "\\" + Str(b, ft + nameOff) : Str(b, ft + nameOff);
			var o = new MemoryStream();
			if ((flags & 4) != 0) {
				for (int q = off; q < off + comp; ) { int len = BitConverter.ToUInt16(b, q); q += 2; using (var ds = new DeflateStream(new MemoryStream(b, q, len), CompressionMode.Decompress)) ds.CopyTo(o); q += len; }
			} else o.Write(b, off, exp);
			string full = Path.Combine(outDir, path);
			Directory.CreateDirectory(Path.GetDirectoryName(full));
			File.WriteAllBytes(full, o.ToArray());
			names.Add(path);
		}
		return names;
	}
}
'@
Add-Type -TypeDefinition $cs -ReferencedAssemblies System.IO.Compression, System.IO.Compression.FileSystem
$ref = Join-Path $Out 'cab_reference'
$refNames = [Fs1DemoRef]::Extract($Installer, $ref)
Check ($refNames.Count -eq $files.Count) "both readers find the same number of files ($($refNames.Count))"
foreach ($f in $files) {
	$a = Join-Path $mine $f.Path; $b = Join-Path $ref $f.Path
	$same = (Test-Path $b) -and ((Get-Item $a).Length -eq $f.Size) -and ((Get-FileHash $a).Hash -eq (Get-FileHash $b).Hash)
	Check $same "$($f.Path): $($f.Size) bytes, identical to the reference"
}

# --- 3. extraction into a data set folder, detection, and the never-overwrite rule
$exeDir = Join-Path $Out 'exe_dir'
New-Item -ItemType Directory -Force $exeDir | Out-Null
Copy-Item $Installer (Join-Path $exeDir 'fsdemo10.exe')
$r = & $exe extract $exeDir
Check ($r -eq 'EXTRACT 0 ') "dataset_fs1_extract makes fs1demo ($r)"
$made = @(Get-ChildItem (Join-Path $exeDir 'fs1demo') -Recurse -File | ForEach-Object { $_.FullName.Substring((Join-Path $exeDir 'fs1demo').Length + 1) })
$wanted = @($files | Where-Object { $_.Path -like 'Data\*' } | ForEach-Object { $_.Path })
Check ((Compare-Object $made $wanted -SyncWindow 0) -eq $null) "fs1demo holds exactly the Data files ($($made -join ', '))"
foreach ($w in $wanted) { Check ((Get-FileHash (Join-Path $exeDir "fs1demo\$w")).Hash -eq (Get-FileHash (Join-Path $ref $w)).Hash) "fs1demo\$w identical to the reference" }
Check (-not (Test-Path (Join-Path $exeDir 'fs1demo.partial'))) 'no fs1demo.partial left behind'
Check ((& $exe detect (Join-Path $exeDir 'fs1demo')) -eq 'DETECT 1') 'dataset_fs1_detect accepts fs1demo'
if ($Retail) { Check ((& $exe detect $Retail) -eq 'DETECT 0') "dataset_fs1_detect refuses FS1 retail ($Retail)" }
Check ((& $exe detect $exeDir) -eq 'DETECT 0') 'dataset_fs1_detect refuses a folder with no data'
$r = & $exe extract $exeDir
Check ($r -like 'EXTRACT 1 *already there*') "a second extract refuses the existing folder ($r)"

# --- 4. the FS2 demo's extraction, through the shared inflater
if ($Fs2Installer) {
	Add-Type -AssemblyName System.IO.Compression.FileSystem
	$fs2Dir = Join-Path $Out 'fs2_exe_dir'
	New-Item -ItemType Directory -Force $fs2Dir | Out-Null
	Copy-Item $Fs2Installer (Join-Path $fs2Dir 'fs2demo10.exe')
	$r = & $exe select $fs2Dir
	Check ($r -like "SELECT 0 *fs2demo") "dataset_select_folder makes fs2demo ($r)"
	$fs2Ref = Join-Path $Out 'fs2_reference'
	$z = [IO.Compression.ZipFile]::OpenRead($Fs2Installer)
	$want = @($z.Entries | Where-Object { ($_.FullName -like 'Data/*' -or ($_.FullName -notlike '*/*' -and $_.FullName -like '*.vp')) -and -not $_.FullName.EndsWith('/') })
	foreach ($e in $want) { $p = Join-Path $fs2Ref ($e.FullName -replace '/', '\'); New-Item -ItemType Directory -Force (Split-Path $p) | Out-Null; [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $p, $true) }
	$z.Dispose()
	$made = @(Get-ChildItem (Join-Path $fs2Dir 'fs2demo') -Recurse -File)
	Check ($made.Count -eq $want.Count) "fs2demo holds the $($want.Count) wanted zip entries ($($made.Count))"
	$bad = @($want | Where-Object { $n = $_.FullName -replace '/', '\'; (Get-FileHash (Join-Path $fs2Ref $n)).Hash -ne (Get-FileHash (Join-Path $fs2Dir "fs2demo\$n")).Hash })
	Check ($bad.Count -eq 0) "every fs2demo file identical to .NET's extraction ($($bad.Count) differ)"
}

if ($fail) { "FAIL: $fail check(s)"; exit 1 }
'PASS'
exit 0
