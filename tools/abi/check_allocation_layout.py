"""Check a concrete cross-translation-unit ABI invariant in an x86_64 kext.

Compare allocation sizes in the PCI probe against each class's MetaClass
allocator. This detects the observed stale-header failure, not every ABI bug.
Unknown code generation fails closed rather than claiming consistency.
"""
from pathlib import Path
import argparse, json, re, subprocess


def functions(text):
    result = {}; current = None
    for line in text.splitlines():
        match = re.fullmatch(r'(__Z[^\s:]+):', line)
        if match:
            current = match[1]; result[current] = []
        elif current:
            result[current].append(line)
    return result


def allocations(lines):
    result = []
    for i, line in enumerate(lines):
        if not re.search(r'\bcallq?\s+__ZN8OSObjectnwEm\b', line):
            continue
        if not i:
            raise ValueError('Allocation has no immediate size argument')
        match = re.search(r'\bmovl\s+\$(0x[0-9a-fA-F]+|[0-9]+),\s*%edi\b', lines[i-1])
        if not match:
            raise ValueError('Unsupported allocation size instruction')
        following = lines[i+1:i+9]
        result.append((int(match[1], 0), following))
    return result


def check(binary):
    output = subprocess.check_output(['/usr/bin/otool', '-tvV', str(binary)], text=True)
    funcs = functions(output)
    probe = '__ZN19IOPCIEDeviceWrapper5probeEP9IOServicePi'
    if probe not in funcs:
        raise ValueError('Expected PCI probe symbol missing')
    seen = {}
    for size, following in allocations(funcs[probe]):
        matches = [name for name in ['ItlIwx', 'ItlIwm', 'ItlIwn']
                   if any('__ZN6'+name+'C1Ev' in line for line in following)]
        if len(matches) != 1 or matches[0] in seen:
            raise ValueError('Ambiguous or duplicate probe constructor association')
        seen[matches[0]] = size
    rows = []
    for name in ['ItlIwx', 'ItlIwm', 'ItlIwn']:
        symbol = '__ZNK6'+name+'9MetaClass5allocEv'
        if name not in seen or symbol not in funcs:
            raise ValueError('Expected allocation path missing: '+name)
        meta = allocations(funcs[symbol])
        if len(meta) != 1:
            raise ValueError('Unsupported MetaClass allocator: '+name)
        rows.append({'class': name, 'probeBytes': seen[name],
                     'metaClassBytes': meta[0][0], 'equal': seen[name] == meta[0][0]})
    return {'binary': str(Path(binary).resolve()), 'status': 'pass' if all(row['equal'] for row in rows) else 'fail',
            'allocationPaths': rows,
            'limits': 'Checks matched x86_64 direct allocation sizes only. Does not prove every field offset, '
                      'firmware compatibility, boot success or throughput. Also verify fresh build roots and core-header dependencies.'}


if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('binary'); ap.add_argument('--output', required=True)
    args = ap.parse_args()
    try:
        report = check(args.binary)
    except (ValueError, subprocess.CalledProcessError) as exc:
        report = {'status': 'unsupported-or-error', 'error': str(exc)}
    Path(args.output).write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report['status'] == 'pass' else 1)
