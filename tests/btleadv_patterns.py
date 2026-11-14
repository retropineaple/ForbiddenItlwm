#!/usr/bin/env python3
"""Check a BTLEAdvFixup patch table against a real bluetoothd.

Default: the Tahoe 26.7.1 set against this Mac's /usr/sbin/bluetoothd.
  --set 2672 --binary <path>          the single 26.7.2 extendedConnection pattern against a 26.7.2 bluetoothd
  --set 2673 --binary <path>          the Tahoe 26.7.3 set against a 26.7.3 bluetoothd
  --target sequoia --binary <path>   the Sequoia set against a 15.8, 15.8.2 or 15.8.3 bluetoothd
For Tahoe it also checks that the other Tahoe set matches nothing in the binary.

This shows each pattern matches exactly once and changes only what it should. It
doesn't show that the Bluetooth controller accepts extended advertising.
"""
import argparse,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--target',choices=('tahoe','sequoia','sonoma'),default='tahoe')
parser.add_argument('--binary',type=Path,default=Path('/usr/sbin/bluetoothd'))
parser.add_argument('--set',choices=('2671','2672','2673'),default='2671',help='Tahoe only: which build set to check')
args=parser.parse_args()
whole=(root/'BTLEAdvFixup/BTLEAdvPatches.hpp').read_text()
body=whole.split('#if !defined(BTLEADV_SEQUOIA) && !defined(BTLEADV_SONOMA)',1)[1]
tahoe,rest=body.split('\n#elif defined(BTLEADV_SEQUOIA)\n',1)
sequoia,sonoma=rest.split('\n#else\n',1)
source={'tahoe':tahoe,'sequoia':sequoia,'sonoma':sonoma}[args.target]
suffix='' if args.target!='tahoe' or args.set=='2671' else args.set
other='2673' if args.target=='tahoe' and args.set=='2671' else ''
def table(name, sfx=None):
    name=name.replace('Find','Find'+(suffix if sfx is None else sfx)).replace('Replace','Replace'+(suffix if sfx is None else sfx))
    body=re.search(r'%s\[\] = \{(.*?)\};'%name,source,re.S).group(1)
    return bytes(int(x,16) for x in re.findall(r'0x[0-9a-f]{2}',body))
# Read the x86_64 slice directly. lipo writing from a mapped copy of this file failed
# once with "Bad address" on a cold page cache, while a plain read was fine.
raw=args.binary.read_bytes()
assert raw[:4]==bytes.fromhex('cafebabe')
binary=None
for i in range(int.from_bytes(raw[4:8],'big')):
    entry=raw[8+20*i:28+20*i]
    if int.from_bytes(entry[:4],'big')==0x1000007:
        offset=int.from_bytes(entry[8:12],'big');binary=raw[offset:offset+int.from_bytes(entry[12:16],'big')]
assert binary,'no x86_64 slice'
# 26.7.2 (25G332) has one site of its own, and its window is longer than the other patterns, so
# the bytes identify that build without a build string. 26.7.1 and 26.7.3 have their own
# extendedConnection patterns, so this one must match nothing there.
if args.set=='2672':
    find,replace=table('extendedConnectionFind','2672'),table('extendedConnectionReplace','2672')
    if binary.count(find)==0 and binary.count(replace)==1:
        binary=binary.replace(replace,find)
        print('extendedConnection2672: this read returned the patched page, checked against the original bytes')
    if binary.count(find)!=1:
        raise SystemExit('2672 pattern matches %d times: not a 26.7.2 bluetoothd'%binary.count(find))
    assert len(find)==len(replace)
    offset=binary.index(find)
    assert offset//4096==(offset+len(find)-1)//4096,'pattern crosses a page, Lilu may miss it'
    assert binary.count(replace)==0
    changed=[i for i in range(len(find)) if find[i]!=replace[i]]
    # Only the movzx and the two padding bytes change; the calls around it stay.
    assert changed==list(range(12,19)),changed
    assert find[12:19]==bytes.fromhex('0fb6bd76ffffff') and replace[12:19]==bytes.fromhex('bf010000009090')
    print('BTLEAdvFixup 26.7.2 extendedConnection pattern: passed against %s (no controller test)'%args.binary)
    raise SystemExit
# With the kext loaded, a read can return pages it already patched, from the page cache.
# Put those back so the rest of this check always sees the original bytes.
for name in ('advertising','scanning','randomAddress','extendedConnection'):
    find,replace=table(name+'Find'),table(name+'Replace')
    if binary.count(find)==0 and binary.count(replace)==1:
        binary=binary.replace(replace,find)
        print('%s: this read returned the patched page, checked against the original bytes'%name)
queries={'tahoe':('ff9118090000','ff91a8090000'),'sequoia':('ff91c0070000','ff9148080000'),'sonoma':('ff9100070000','ff9170070000')}[args.target]
checks=[('advertising',bytes.fromhex(queries[0])),('scanning',bytes.fromhex(queries[1]))]
for name,query in checks:
    find,replace=table(name+'Find'),table(name+'Replace')
    assert len(find)==len(replace)
    if binary.count(find)!=1:
        raise SystemExit('%s pattern matches %d times: not the expected bluetoothd build'%(name,binary.count(find)))
    offset=binary.index(find)
    assert offset//4096==(offset+len(find)-1)//4096,'pattern crosses a page, Lilu may miss it'
    assert binary.count(replace)==0
    changed=[i for i in range(len(find)) if find[i]!=replace[i]]
    # Only the je (2 to 7) and the capability query (19 to 24) change.
    assert set(changed)<=set(range(2,8))|set(range(19,25)),changed
    assert find[2:4]==bytes.fromhex('0f84') and replace[2:8]==b'\x90'*6
    assert find[19:25]==query and replace[19:25]==bytes.fromhex('b80100000090')
find,replace=table('randomAddressFind'),table('randomAddressReplace')
if binary.count(find)!=1:
    raise SystemExit('randomAddress pattern matches %d times'%binary.count(find))
offset=binary.index(find)
assert offset//4096==(offset+len(find)-1)//4096,'pattern crosses a page'
assert len(find)==len(replace) and find[:9]==replace[:9] and binary.count(replace)==0
assert find[9:11]==bytes.fromhex('31ff') and find[11]==0xe8 and replace[9:]==b'\x90'*7
find,replace=table('extendedConnectionFind'),table('extendedConnectionReplace')
if binary.count(find)!=1:
    raise SystemExit('extendedConnection pattern matches %d times'%binary.count(find))
offset=binary.index(find)
assert offset//4096==(offset+len(find)-1)//4096,'pattern crosses a page'
assert len(find)==len(replace) and find[7:]==replace[7:] and binary.count(replace)==0
assert find[:3]==bytes.fromhex('0fb6bd') and replace[:7]==bytes.fromhex('bf010000009090')
if args.target=='tahoe':
    # The other Tahoe set must match nothing here, or it could patch the wrong place.
    for name in ('advertising','scanning','randomAddress','extendedConnection'):
        for kind in ('Find','Replace'):
            assert binary.count(table(name+kind, other))==0, ('other set matches', name, kind)
label=args.target+(' '+args.set if args.target=='tahoe' else '')
print('BTLEAdvFixup %s patterns: passed against %s (no controller test)'%(label,args.binary))
