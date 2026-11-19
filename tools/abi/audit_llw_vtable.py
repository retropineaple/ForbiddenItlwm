#!/usr/bin/env python3
"""Check the compiled virtual table of ItlLLWInterface (llw0) against Apple's IO80211InfraProtocol on Sequoia.

llw0 is built on the Wi-Fi interface's class chain. A virtual that doesn't match a base slot's signature does not
override it: the compiler adds a new slot and leaves the old one pure, and the first call through it crashes the
kernel. This checks, from the compiled object (no kernel collection needed):

  * the table has exactly as many slots as Apple's IO80211InfraProtocol (nothing was appended, nothing moved);
  * no slot is still ___cxa_pure_virtual;
  * every slot we override has Apple's slot's exact method name and signature, with our class in place of
    Apple's, and every slot we don't override points at the very symbol Apple's table has.

  python3 tools/abi/audit_llw_vtable.py [--build build]
The reference is tools/abi/native-vtables-llw-24H232.json (made with kc_vtable.py from the 24H232 kernel collection).
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, default=root.parents[1] / 'build')
args = parser.parse_args()

candidates = sorted((args.build / 'obj').glob('AirportLLWInterface-*.o'))
if not candidates:
    sys.exit('no AirportLLWInterface object in %s/obj (build the sequoia target first)' % args.build)
raw = candidates[0].read_bytes()

sections = []
position = 32
symtab = None
for _ in range(struct.unpack_from('<I', raw, 16)[0]):
    cmd, size = struct.unpack_from('<II', raw, position)
    if cmd == 0x19:
        for i in range(struct.unpack_from('<I', raw, position + 64)[0]):
            q = position + 72 + i * 80
            addr, length, offset, _align, reloff, nreloc = struct.unpack_from('<QQIIII', raw, q + 32)
            sections.append(dict(addr=addr, length=length, offset=offset, reloff=reloff, nreloc=nreloc))
    if cmd == 2:
        symtab = struct.unpack_from('<IIII', raw, position + 8)
    position += size
symoff, nsyms, stroff, strsize = symtab
symbols = []
for i in range(nsyms):
    strx, _type, sect, _desc, value = struct.unpack_from('<IBBHQ', raw, symoff + i * 16)
    name = raw[stroff + strx:raw.find(b'\0', stroff + strx, stroff + strsize)].decode()
    symbols.append(dict(name=name, value=value, section=sect))

table = next((s for s in symbols if s['name'] == '__ZTV15ItlLLWInterface'), None)
if not table:
    sys.exit('__ZTV15ItlLLWInterface not found: is ITL_AWDL_LLW defined for this target?')
section = sections[table['section'] - 1]
relocations = {}
for i in range(section['nreloc']):
    addr, info = struct.unpack_from('<II', raw, section['reloff'] + i * 8)
    if (info >> 27) & 1:                       # external relocation: a symbol
        relocations[addr] = symbols[info & 0xffffff]['name']
    else:                                      # local: the pointer holds an address inside the object
        value = struct.unpack_from('<Q', raw, section['offset'] + addr)[0]
        names = [s['name'] for s in symbols if s['section'] and s['value'] == value]
        relocations[addr] = names[0] if names else '0x%x' % value

reference = json.loads((root / 'native-vtables-llw-24H232.json').read_text())['IO80211InfraProtocol']['entries']
# The table ends with a null entry that carries no relocation in our object.
while reference and reference[-1]['symbol'] == '0':
    reference.pop()
first = table['value'] - section['addr'] + 16      # skip offset-to-top and RTTI
actual = []
slot = 0
while (first + slot) in relocations:
    actual.append(relocations[first + slot])
    slot += 8
problems = []
if len(actual) != len(reference):
    problems.append('slot count %d, Apple has %d' % (len(actual), len(reference)))
overrides = 0


def after_class(mangled):
    """The method part of a mangled member name: what follows the class name (its length is spelled out)."""
    m = re.match(r'__ZN(K?)(\d+)', mangled)
    if not m:
        return None
    return mangled[m.end() + int(m.group(2)):]


for index, name in enumerate(actual[:len(reference)]):
    native = reference[index]['symbol']
    if name == '___cxa_pure_virtual':
        problems.append('slot %#x is still pure' % (index * 8))
    elif after_class(name) is not None and name.startswith(('__ZN15ItlLLWInterface', '__ZNK15ItlLLWInterface')):
        overrides += 1
        # A handler slot is pure in Apple's table, so there is no native name to hold ours to: the slot count
        # and the absence of pure slots cover it. Every other override must be Apple's method and signature.
        if native != '___cxa_pure_virtual' and after_class(name) != after_class(native):
            problems.append('slot %#x: ours %s, native %s' % (index * 8, name, native))
    elif name != native:
        problems.append('slot %#x: inherited %s, native %s' % (index * 8, name, native))

print(json.dumps(dict(slots=len(actual), native=len(reference), overridden=overrides, problems=len(problems)), indent=1))
for line in problems[:25]:
    print('  ' + line)
sys.exit(1 if problems else 0)
