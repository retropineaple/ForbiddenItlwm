#!/usr/bin/env python3
"""Check exact 25G342 patch sites; an explicit binary may contain patched pages."""
import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('addressResend', 'rpaHook', 'rpaSave',
         'falAdd', 'falHelper', 'falRemove')


def check(binary):
    header = ROOT / 'BTLEAdvFixup/BTLEAdvExtraPatches26_7_3.hpp'
    assert header.exists(), '25G342 pattern table missing'
    source = header.read_text()
    raw = Path(binary).read_bytes()
    if raw[:4] == bytes.fromhex('cafebabe'):
        for i in range(struct.unpack_from('>I', raw, 4)[0]):
            cpu, _, offset, size, _ = struct.unpack_from('>5I', raw, 8 + 20*i)
            if cpu == 0x01000007:
                raw = raw[offset:offset+size]
                break
        else:
            raise AssertionError('x86_64 slice missing')
    assert raw[:4] == bytes.fromhex('cffaedfe'), 'expected x86_64 Mach-O'
    print('x86_64 SHA256:', hashlib.sha256(raw).hexdigest())
    sites = {}
    replacements = {}
    for name in NAMES:
        def table(suffix):
            m = re.search(r'\b' + name + suffix + r'\[\]\s*=\s*\{(.*?)\}', source, re.S)
            if not m and 'using BTLEAdvExtra2672::' + name + suffix + ';' in source:
                shared = (ROOT / 'BTLEAdvFixup/BTLEAdvExtraPatches26_7.hpp').read_text()
                m = re.search(r'\b' + name + suffix + r'\[\]\s*=\s*\{(.*?)\}', shared, re.S)
            assert m, name + suffix + ' missing'
            return bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', m[1]))
        find, replace = table('Find'), table('Replace')
        assert find and len(find) == len(replace) and find != replace, name
        original = [m.start() for m in re.finditer(re.escape(find), raw)]
        patched = [m.start() for m in re.finditer(re.escape(replace), raw)]
        offsets = original + patched
        assert len(offsets) == 1, (name, 'original', original, 'patched', patched)
        offset = offsets[0]
        sites[name] = offset
        replacements[name] = replace
        assert offset // 4096 == (offset + len(find)-1) // 4096, name + ' crosses page'
        print(name, hex(offset), 'original=' + str(len(original)), 'patched=' + str(len(patched)))
    def relative(name, instruction, displacement, length):
        code = replacements[name]
        return sites[name] + instruction + length + struct.unpack_from('<i', code, instruction + displacement)[0]
    # Independently checked helper entries and sixteen 24-byte cache slots.
    assert relative('rpaHook', 14, 1, 5) == sites['rpaSave'] + 2
    assert relative('falAdd', 5, 1, 5) == sites['falHelper'] + 10
    assert relative('falRemove', 6, 1, 5) == sites['falHelper'] + 10
    for caller, helper in (('rpaHook', 'rpaSave'), ('falAdd', 'falHelper'), ('falRemove', 'falHelper')):
        assert sites[caller] // 4096 == sites[helper] // 4096, 'helper not in caller page'
    assert relative('rpaSave', 18, 3, 7) == 0xb52e80
    assert relative('falHelper', 10, 3, 7) == 0xb52e80
    # Ensure that cache lies outside all allocated sections and in writable
    # zero-fill segment tail padding, not in a relocated 25G332 address.
    cursor = 32
    for _ in range(struct.unpack_from('<I', raw, 16)[0]):
        cmd, size = struct.unpack_from('<II', raw, cursor)
        if cmd == 0x19 and raw[cursor+8:cursor+24].rstrip(b'\0') == b'__DATA':
            vm, length, fileoff, filesize, _, protection, count, _ = struct.unpack_from('<4Q4I', raw, cursor+24)
            end = max(struct.unpack_from('<Q', raw, cursor+72+80*i+32)[0] +
                      struct.unpack_from('<Q', raw, cursor+72+80*i+40)[0] for i in range(count))
            assert protection & 2
            assert end <= 0x100b52e80 and 0x100b52e80 + 16*24 <= vm + length
            assert vm + filesize <= 0x100b52e80
            break
        cursor += size
    else:
        raise AssertionError('writable cache segment missing')
    print('25G342: six optional patch pairs verified (offline, not controller behavior)')


if __name__ == '__main__':
    check(sys.argv[1] if len(sys.argv) > 1 else '/usr/sbin/bluetoothd')
