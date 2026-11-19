#!/usr/bin/env python3
"""Carve one kext out of a macOS kernel collection so otool and nm can read it.

On macOS 11 and later Apple's kexts (IO80211Family, AppleBCMWLANCoreMac, ...) exist only inside the kernel
collections, and otool cannot open a fileset entry by name. Each entry's own Mach-O header keeps file offsets
that are absolute within the collection, so a copy of the collection whose first bytes are replaced by that
entry's header is a Mach-O that the usual tools read.

  python3 tools/abi/kc_carve.py --list [collection]
  python3 tools/abi/kc_carve.py com.apple.iokit.IO80211Family out.bin [collection]

The default collection is /System/Library/KernelCollections/BootKernelExtensions.kc; the other one is
SystemKernelExtensions.kc in the same folder. Then, for example:

  otool -arch x86_64 -tV out.bin > all.s        # the whole text, with symbol names
  nm -arch x86_64 out.bin | c++filt | grep ...

Read-only on the collection. The copy is as large as the collection (tens to hundreds of MB): put it in /tmp.
"""
import shutil
import struct
import sys

DEFAULT = '/System/Library/KernelCollections/BootKernelExtensions.kc'
LC_FILESET_ENTRY = 0x80000035


def entries(path):
    with open(path, 'rb') as f:
        magic, _cpu, _sub, _type, ncmds, sizeofcmds, _flags, _res = struct.unpack('<IiiIIIII', f.read(32))
        if magic != 0xfeedfacf:
            sys.exit('%s is not a 64-bit Mach-O' % path)
        cmds = f.read(sizeofcmds)
    found = {}
    offset = 0
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', cmds, offset)
        if cmd == LC_FILESET_ENTRY:
            vmaddr, fileoff, name_offset, _reserved = struct.unpack_from('<QQII', cmds, offset + 8)
            name = cmds[offset + name_offset:offset + size].split(b'\0')[0].decode()
            found[name] = (vmaddr, fileoff)
        offset += size
    return found


def main():
    args = sys.argv[1:]
    if args and args[0] == '--list':
        for name, (vmaddr, fileoff) in sorted(entries(args[1] if len(args) > 1 else DEFAULT).items()):
            print('%-60s vm=%#x fileoff=%#x' % (name, vmaddr, fileoff))
        return
    if len(args) < 2:
        sys.exit(__doc__)
    name, out = args[0], args[1]
    collection = args[2] if len(args) > 2 else DEFAULT
    found = entries(collection)
    if name not in found:
        sys.exit('%s is not in %s (try --list, or the other collection)' % (name, collection))
    vmaddr, fileoff = found[name]
    with open(collection, 'rb') as f:
        f.seek(fileoff)
        header = f.read(32)
        sizeofcmds = struct.unpack('<IiiIIIII', header)[5]
        f.seek(fileoff)
        blob = f.read(32 + sizeofcmds)
    shutil.copyfile(collection, out)
    with open(out, 'r+b') as o:
        o.write(blob)
    print('%s: vm=%#x fileoff=%#x, header %d bytes -> %s' % (name, vmaddr, fileoff, len(blob), out))


if __name__ == '__main__':
    main()
