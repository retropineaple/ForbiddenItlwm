#!/usr/bin/env python3
"""Summarise a capture of awdl0 traffic per flow: does a flow run steady and then stop dead, or stutter from the start?

  python3 -I tools/diag/awdl_flow.py big.pcap          reads the file with tcpdump -nn -tt -r (no root needed to read)
  python3 -I tools/diag/awdl_flow.py --text lines.txt  reads `tcpdump -nn -tt` text instead (for testing)

Capture it with, for example (root needed to capture, not to read):
  sudo tcpdump -i awdl0 -nn -s 0 -w big.pcap 'ip6 and not udp port 5353'

For each of the five largest TCP or UDP flows it prints the clock times of its first and last packet, packets and
bytes each way, the longest gaps, how many 0.25 s buckets were empty, and a one-line timeline. A flow that ran steady
and then ended shows a dense timeline and a short gap list. One that stutters shows many empty buckets and long gaps.

The capture is taken above the driver, so it shows what macOS handed to awdl0 (and what arrived), not what went over
the air. Packets macOS rejects before the interface (EMSGSIZE) never appear. See docs/DEBUGGING.md.
"""
import re
import subprocess
import sys
import time
from typing import Dict, List, Tuple

TCP = re.compile(r'^(\d+\.\d+) IP6 (\S+)\.(\d+) > (\S+)\.(\d+): Flags \[([^\]]*)\].*?length (\d+)')
UDP = re.compile(r'^(\d+\.\d+) IP6 (\S+)\.(\d+) > (\S+)\.(\d+): UDP, length (\d+)')
BUCKET = 0.25


class Flow:
    def __init__(self) -> None:
        self.times: List[float] = []
        self.sizes: List[int] = []
        self.sent: Dict[Tuple[str, str], List[int]] = {}
        self.syn = self.fin = self.rst = 0


def read_lines() -> List[str]:
    if sys.argv[1] == '--text':
        return open(sys.argv[2]).read().splitlines()
    out = subprocess.run(['tcpdump', '-nn', '-tt', '-r', sys.argv[1]], capture_output=True, text=True)
    return out.stdout.splitlines()


def clock(t: float) -> str:
    return time.strftime('%H:%M:%S', time.localtime(t))


def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    flows: Dict[Tuple[str, Tuple[str, str], Tuple[str, str]], Flow] = {}
    first = last = None
    for line in read_lines():
        m = TCP.match(line)
        proto, flags = 'tcp', ''
        if m:
            t, src, sport, dst, dport, flags, n = m.groups()
        else:
            m = UDP.match(line)
            if not m:
                continue
            t, src, sport, dst, dport, n = m.groups()
            proto = 'udp'
        ts, size = float(t), int(n)
        ends = sorted([(src, sport), (dst, dport)])
        f = flows.setdefault((proto, ends[0], ends[1]), Flow())
        f.times.append(ts)
        f.sizes.append(size)
        counts = f.sent.setdefault((src, sport), [0, 0])
        counts[0] += 1
        counts[1] += size
        if 'S' in flags and '.' not in flags:
            f.syn += 1
        f.fin += 'F' in flags
        f.rst += 'R' in flags
        first = ts if first is None else min(first, ts)
        last = ts if last is None else max(last, ts)
    if first is None or last is None:
        sys.exit('no IPv6 TCP/UDP packets found')
    print('packets span %.1f s (%s to %s)' % (last - first, clock(first), clock(last)))
    for (proto, a, b), f in sorted(flows.items(), key=lambda kv: -sum(kv[1].sizes))[:5]:
        ts = f.times
        dur = ts[-1] - ts[0]
        gaps = sorted((y - x for x, y in zip(ts, ts[1:])), reverse=True)
        hist = [0] * (int(dur / BUCKET) + 1)
        for t in ts:
            hist[int((t - ts[0]) / BUCKET)] += 1
        top = max(1, max(hist))
        line = ''.join(' .:-=+*#%@'[min(9, h * 9 // top)] for h in hist[:120])
        print('\n%s  %s.%s <-> %s.%s' % (proto.upper(), a[0][-19:], a[1], b[0][-19:], b[1]))
        print('  %s to %s (%.2f s), packets %d, bytes %d' % (clock(ts[0]), clock(ts[-1]), dur, len(ts), sum(f.sizes)))
        for (who, port), (pk, by) in f.sent.items():
            print('    %s.%s sent %d packets, %d bytes' % (who[-19:], port, pk, by))
        print('  longest gaps (s): %s   empty 0.25 s buckets: %d of %d'
              % (', '.join('%.2f' % g for g in gaps[:5]), sum(1 for h in hist if h == 0), len(hist)))
        if proto == 'tcp':
            print('  SYN %d  FIN %d  RST %d' % (f.syn, f.fin, f.rst))
        print('  timeline (each char 0.25 s, density):')
        print('  |%s|' % line)


if __name__ == '__main__':
    main()
