"""Install the FreeSpace 2 demo from its installer image without running the installer.

Usage:  python -I tools\\demo_install.py <setup image folder> <target folder> [--force]

The demo's InstallShield 5 setup keeps every installed file loose on the image; data1.cab holds only
the file table (which file goes in which folder, plus empty folders to create). This script reads that
table, copies each file to the same place under the target, and gives the three programs their .exe
names as setup.ins does (the .ex_ files on this image are plain, uncompressed Windows programs, not
SZDD-compressed, so no expand step is needed; an SZDD file is expanded with Windows' expand.exe).

The setup image is only read. The registry key setup writes (HKLM\\SOFTWARE\\Volition\\Freespace2Demo),
the shortcuts and the uninstall entry are not made; the project's builds read fs2.ini instead
(docs/SETTINGS.md). See docs/FS2_DEMO.md.
"""

import os
import shutil
import struct
import subprocess
import sys

# setup.ins copies these under their .ex_ names and renames them (strings in setup.ins, in this order)
RENAMES = {
    'freespace2demo.ex_': 'FreeSpace2Demo.exe',   # the launcher
    'fs2demo.ex_': 'FS2Demo.exe',                 # the game
    'updatelauncher.ex_': 'UpdateLauncher.exe',
}

FILE_INVALIDATED = 8    # file-table entry that only names a folder to create


def read_manifest(cab_path):
    """(folders to create, [(folder, name, size)]) from an InstallShield 5 data1.cab header."""
    d = open(cab_path, 'rb').read()
    u32 = lambda o: struct.unpack_from('<I', d, o)[0]
    def cstr(o):
        return d[o:d.index(b'\0', o)].decode('latin-1')

    sig, version, _vol, desc = struct.unpack_from('<4sIII', d, 0)
    if sig != b'ISc(' or version != 0x01000004:
        raise SystemExit('%s: not an InstallShield 5 cabinet (version %08x)' % (cab_path, version))
    table = desc + u32(desc + 0x0c)
    ndirs = u32(desc + 0x1c)
    nfiles = u32(desc + 0x28)
    dirs = [cstr(table + u32(table + 4 * i)) for i in range(ndirs)]
    folders, files = [], []
    for i in range(nfiles):
        fd = table + u32(table + 4 * (ndirs + i))
        name_off, dir_index = u32(fd), u32(fd + 4)
        flags = struct.unpack_from('<H', d, fd + 8)[0]
        size = u32(fd + 10)
        if flags & FILE_INVALIDATED:
            folders.append(dirs[dir_index])
        else:
            files.append((dirs[dir_index], cstr(table + name_off), size))
    return folders, files


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    force = '--force' in sys.argv
    if len(args) != 2:
        raise SystemExit(__doc__)
    src, dst = (os.path.abspath(a) for a in args)
    if os.path.normcase(dst).startswith(os.path.normcase(src) + os.sep) or os.path.normcase(src) == os.path.normcase(dst):
        raise SystemExit('target must be outside the setup image')
    if os.path.isdir(dst) and os.listdir(dst) and not force:
        raise SystemExit('%s is not empty (use --force to copy over it)' % dst)

    folders, files = read_manifest(os.path.join(src, 'data1.cab'))
    for f in folders + ['']:
        os.makedirs(os.path.join(dst, f), exist_ok=True)

    for folder, name, size in files:
        source = os.path.join(src, folder, name)
        actual = os.path.getsize(source)
        if actual != size:
            raise SystemExit('%s: %d bytes, the file table says %d' % (source, actual, size))
        target = os.path.join(dst, folder, RENAMES.get(name.lower(), name))
        with open(source, 'rb') as f:
            magic = f.read(4)
        if magic == b'SZDD':
            subprocess.check_call(['expand.exe', source, target])
        else:
            shutil.copy2(source, target)
        print('%-10s %-22s -> %s' % (folder or '.', name, os.path.relpath(target, dst)))

    print('%d files, %d folders installed in %s' % (len(files), len(folders), dst))


if __name__ == '__main__':
    main()
