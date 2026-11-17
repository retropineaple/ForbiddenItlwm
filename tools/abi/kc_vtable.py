#!/usr/bin/env python3
"""Read a C++ virtual table out of a kext carved from a kernel collection (see kc_carve.py).

  python3 tools/abi/kc_vtable.py <carved.bin> <mangled vtable symbol> [--collection <kc>] [--json]

for example

  python3 tools/abi/kc_vtable.py /tmp/fam.bin __ZTV20IO80211InfraProtocol

prints one line per slot: the slot offset (as the compiler counts it, from the first function pointer) and
the symbol the slot points at. Pointers in a kernel collection are chained fixups (x86_64 kernel cache
format): the low 30 bits are an offset from the collection's base address. A slot that points into another
kext (the kernel, IOSkywalkFamily) is named from that kext's symbols when --also <carved.bin> is given, and
printed as an address otherwise.
"""
import argparse
import json
import struct
import subprocess

LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2


def load(path):
    raw = open(path, 'rb')
    magic, _c, _s, _t, ncmds, sizeofcmds, _f, _r = struct.unpack('<IiiIIIII', raw.read(32))
    assert magic == 0xfeedfacf, path
    cmds = raw.read(sizeofcmds)
    segments = []
    offset = 0
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', cmds, offset)
        if cmd == LC_SEGMENT_64:
            name = cmds[offset + 8:offset + 24].split(b'\0')[0].decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', cmds, offset + 24)
            segments.append((name, vmaddr, vmsize, fileoff, filesize))
        offset += size
    return raw, segments


def symbols(path):
    out = subprocess.run(['nm', '-arch', 'x86_64', path], capture_output=True, text=True).stdout
    by_name, by_addr = {}, {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in 'TtSsDdBb':
            address = int(parts[0], 16)
            by_name[parts[2]] = address
            # Prefer a function symbol when several names share an address.
            if address not in by_addr or parts[1] in 'Tt':
                by_addr[address] = parts[2]
    return by_name, by_addr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    parser.add_argument('vtable')
    parser.add_argument('--collection', default='/System/Library/KernelCollections/BootKernelExtensions.kc')
    parser.add_argument('--also', action='append', default=[], help='another carved kext to name foreign slots from')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()

    # The base the 30-bit targets are relative to: the collection's own first segment.
    _, kc_segments = load(args.collection)
    base = min(s[1] for s in kc_segments if s[2])

    raw, segments = load(args.binary)
    by_name, by_addr = symbols(args.binary)
    for other in args.also:
        by_addr = {**symbols(other)[1], **by_addr}
    start = by_name[args.vtable]
    # The table ends where the next symbol begins.
    following = sorted(a for a in by_name.values() if a > start)
    end = following[0] if following else start + 0x4000
    segment = next(s for s in segments if s[1] <= start < s[1] + s[2])
    raw.seek(segment[3] + start - segment[1])
    data = raw.read(end - start)

    entries = []
    for index in range(2, len(data) // 8):   # skip offset-to-top and RTTI
        value = struct.unpack_from('<Q', data, index * 8)[0]
        target = base + (value & 0x3fffffff) if value else 0
        entries.append(dict(slot=(index - 2) * 8, symbol=by_addr.get(target, '%#x' % target if target else '0')))
    if args.json:
        print(json.dumps(dict(vtable=args.vtable, base='%#x' % base, entries=entries), indent=1))
    else:
        for e in entries:
            print('%#06x %s' % (e['slot'], e['symbol']))


if __name__ == '__main__':
    main()
